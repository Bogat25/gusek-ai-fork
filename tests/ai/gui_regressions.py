"""Exercise the built Win32 assistant with isolated profiles and loopback fixtures."""
import argparse
import base64
import ctypes as C
from ctypes import wintypes as W
import hashlib
import http.server
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time
import uuid
import zlib

sys.stdout.reconfigure(encoding="utf-8")
sys.stderr.reconfigure(encoding="utf-8")

u = C.WinDLL("user32", use_last_error=True)
k = C.WinDLL("kernel32", use_last_error=True)
CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
u.EnumWindows.argtypes = [CALLBACK, W.LPARAM]
u.EnumChildWindows.argtypes = [W.HWND, CALLBACK, W.LPARAM]
u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.GetDlgCtrlID.argtypes = [W.HWND]
u.GetWindowLongW.argtypes = [W.HWND, C.c_int]
u.IsWindowEnabled.argtypes = [W.HWND]
u.IsWindow.argtypes = [W.HWND]
u.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.GetMenu.argtypes = [W.HWND]
u.GetMenu.restype = W.HMENU
u.GetMenuState.argtypes = [W.HMENU, W.UINT, W.UINT]
u.GetMenuState.restype = W.UINT
u.GetMenuStringW.argtypes = [W.HMENU, W.UINT, W.LPWSTR, C.c_int, W.UINT]
u.AttachThreadInput.argtypes = [W.DWORD, W.DWORD, W.BOOL]
u.GetKeyboardState.argtypes = [C.POINTER(C.c_ubyte)]
u.SetKeyboardState.argtypes = [C.POINTER(C.c_ubyte)]
u.PeekMessageW.argtypes = [C.POINTER(W.MSG), W.HWND, W.UINT, W.UINT, W.UINT]
class GuiThreadInfo(C.Structure):
    _fields_ = [("cbSize", W.DWORD), ("flags", W.DWORD), ("active", W.HWND),
                ("focus", W.HWND), ("capture", W.HWND), ("menu", W.HWND),
                ("moving", W.HWND), ("caret", W.HWND), ("caret_rect", W.RECT)]
u.GetGUIThreadInfo.argtypes = [W.DWORD, C.POINTER(GuiThreadInfo)]
u.ShowWindow.argtypes = [W.HWND, C.c_int]
u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SendMessageTimeoutW.argtypes = [
    W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
u.SendMessageTimeoutW.restype = W.LPARAM
k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
k.OpenProcess.restype = W.HANDLE
k.VirtualAllocEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, W.DWORD]
k.VirtualAllocEx.restype = C.c_void_p
k.ReadProcessMemory.argtypes = [
    W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
k.VirtualFreeEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD]
k.CloseHandle.argtypes = [W.HANDLE]
FENCE = chr(96) * 3
MODEL_DATA = bytes(range(256)) * 4096
CHECKS = 0


def shortcut(target, key, changed, repeat=False, alt=False):
    """Exercise the real event loop without taking foreground focus."""
    msg = W.MSG()
    u.PeekMessageW(C.byref(msg), None, 0, 0, 0)  # Create this thread's message queue.
    current = k.GetCurrentThreadId()
    owner = u.GetWindowThreadProcessId(target, None)
    if not u.AttachThreadInput(current, owner, True):
        raise C.WinError(C.get_last_error())
    saved = (C.c_ubyte * 256)()
    try:
        if not u.GetKeyboardState(saved):
            raise C.WinError(C.get_last_error())
        state = (C.c_ubyte * 256).from_buffer_copy(saved)
        state[0x11], state[0x10], state[0x12] = 0x80, 0x80, 0x80 if alt else 0
        if not u.SetKeyboardState(state):
            raise C.WinError(C.get_last_error())
        if not u.PostMessageW(target, 0x0100, ord(key.upper()), 1 | ((1 << 30) if repeat else 0)):
            raise C.WinError(C.get_last_error())
        time.sleep(0.1)
        wait_for(changed)
    finally:
        u.PostMessageW(target, 0x0101, ord(key.upper()), (1 << 30) | (1 << 31) | 1)
        u.SetKeyboardState(saved)
        u.AttachThreadInput(current, owner, False)


class ClipboardFixture:
    """Preserve clipboard handles opaquely while publishing synthetic fixtures."""
    def __enter__(self):
        self.ole = C.WinDLL('ole32')
        self.ole.OleDuplicateData.argtypes = [W.HANDLE, C.c_ushort, C.c_uint]
        self.ole.OleDuplicateData.restype = W.HANDLE
        u.OpenClipboard.argtypes = [W.HWND]
        u.OpenClipboard.restype = W.BOOL
        u.DestroyWindow.argtypes = [W.HWND]
        u.GetClipboardData.argtypes = [C.c_uint]
        u.GetClipboardData.restype = W.HANDLE
        u.SetClipboardData.argtypes = [C.c_uint, W.HANDLE]
        u.SetClipboardData.restype = W.HANDLE
        k.GlobalAlloc.argtypes = [C.c_uint, C.c_size_t]
        k.GlobalAlloc.restype = W.HANDLE
        k.GlobalLock.argtypes = [W.HANDLE]
        k.GlobalLock.restype = C.c_void_p
        k.GlobalUnlock.argtypes = [W.HANDLE]
        k.GlobalFree.argtypes = [W.HANDLE]
        k.GlobalFree.restype = W.HANDLE
        u.CreateWindowExW.restype = W.HWND
        u.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD,
                                    C.c_int, C.c_int, C.c_int, C.c_int,
                                    W.HWND, W.HMENU, W.HINSTANCE, C.c_void_p]
        self.owner = u.CreateWindowExW(0, 'STATIC', 'Clipboard fixture', 0, 0, 0, 1, 1,
                                       None, None, None, None)
        if not self.owner:
            raise C.WinError(C.get_last_error())
        self.saved = []
        try:
            self.open()
            try:
                fmt = u.EnumClipboardFormats(0)
                while fmt:
                    handle = u.GetClipboardData(fmt)
                    duplicate = self.ole.OleDuplicateData(handle, fmt, 0) if handle else None
                    if not duplicate:
                        raise RuntimeError('Cannot preserve a clipboard format for this test')
                    self.saved.append((fmt, duplicate))
                    fmt = u.EnumClipboardFormats(fmt)
            finally:
                u.CloseClipboard()
        except BaseException:
            u.DestroyWindow(self.owner)
            raise
        return self

    def open(self):
        try:
            wait_for(lambda: u.OpenClipboard(self.owner), 5)
        except TimeoutError:
            raise C.WinError(C.get_last_error()) from None

    def publish(self, fmt, payload):
        self.open()
        try:
            if not u.EmptyClipboard():
                raise C.WinError(C.get_last_error())
            handle = k.GlobalAlloc(2, len(payload))
            try:
                pointer = k.GlobalLock(handle)
                if not pointer:
                    raise C.WinError(C.get_last_error())
                C.memmove(pointer, payload, len(payload))
                k.GlobalUnlock(handle)
                if not u.SetClipboardData(fmt, handle):
                    raise C.WinError(C.get_last_error())
                handle = None  # Ownership passes to Windows on success.
            finally:
                if handle:
                    k.GlobalFree(handle)
        finally:
            u.CloseClipboard()

        # Windows clipboard services may briefly reopen newly published data.
        # Allow that handoff before injecting paste into another process.
        time.sleep(0.4)

    def __exit__(self, *unused):
        try:
            self.open()
            try:
                if not u.EmptyClipboard():
                    raise C.WinError(C.get_last_error())
                for fmt, handle in self.saved:
                    if not u.SetClipboardData(fmt, handle):
                        raise C.WinError(C.get_last_error())
            finally:
                u.CloseClipboard()
        finally:
            u.DestroyWindow(self.owner)


def png_pixels(data):
    """Decode the lossless RGB/RGBA PNG emitted by GDI+ without extra packages."""
    assert data.startswith(b'\x89PNG\r\n\x1a\n')
    offset, compressed = 8, b''
    while offset < len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        kind, chunk = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', chunk)
            assert depth == 8 and color in (2, 6) and interlace == 0
        elif kind == b'IDAT':
            compressed += chunk
        offset += length + 12
    channels = 4 if color == 6 else 3
    stride = width * channels
    packed, prior, rows = zlib.decompress(compressed), bytearray(stride), []
    for y in range(height):
        start = y * (stride + 1)
        mode, row = packed[start], bytearray(packed[start + 1:start + 1 + stride])
        assert mode in range(5)
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            above, corner = prior[x], prior[x - channels] if x >= channels else 0
            p = left + above - corner
            nearest = min((left, above, corner), key=lambda v: abs(p - v))
            predictor = (0, left, above, (left + above) // 2, nearest)[mode]
            row[x] = (row[x] + predictor) & 255
        if channels == 4:
            assert all(row[x] == 255 for x in range(3, stride, 4)), 'Picture must be opaque to the model'
        rows.append(bytes(v for x, v in enumerate(row) if x % channels < 3))
        prior = row
    return width, height, b''.join(rows)


def screenshot_dib(width, height, rgb, v5=False):
    stride = (width * 3 + 3) & ~3
    rows = [rgb[y * width * 3:(y + 1) * width * 3] for y in range(height)]
    if not v5:
        pixels = b''.join(b''.join(row[x:x + 3][::-1] for x in range(0, len(row), 3)) +
                          bytes(stride - width * 3) for row in reversed(rows))
        return 8, struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0) + pixels
    pixels = b''.join(b''.join(row[x:x + 3][::-1] + b'\0' for x in range(0, len(row), 3)) for row in reversed(rows))
    header = bytearray(124)
    struct.pack_into('<IiiHHIIiiII', header, 0, 124, width, height, 1, 32, 3, len(pixels), 0, 0, 0, 0)
    struct.pack_into('<IIIII', header, 40, 0xff0000, 0xff00, 0xff, 0xff000000, 0x73524742)
    return 17, bytes(header) + pixels


def check(condition, description):
    global CHECKS
    if not condition:
        raise AssertionError(description)
    CHECKS += 1
    print("  [PASS]", description, flush=True)


def message(hwnd, code, wp=0, lp=0):
    result = C.c_size_t()
    if not u.SendMessageTimeoutW(hwnd, code, wp, lp, 2, 2500, C.byref(result)):
        raise RuntimeError("GUSEK did not respond to a window message")
    return result.value


def text(hwnd):
    buffer = C.create_unicode_buffer(65536)
    message(hwnd, 0x000D, len(buffer), C.cast(buffer, C.c_void_p).value)
    return buffer.value


def window_class(hwnd):
    buffer = C.create_unicode_buffer(256)
    u.GetClassNameW(hwnd, buffer, len(buffer))
    return buffer.value


def focus(hwnd):
    info = GuiThreadInfo()
    info.cbSize = C.sizeof(info)
    thread = u.GetWindowThreadProcessId(hwnd, None)
    if not u.GetGUIThreadInfo(thread, C.byref(info)):
        raise C.WinError(C.get_last_error())
    return info.focus


def windows(parent=None, pid=None):
    result = []

    @CALLBACK
    def callback(hwnd, unused):
        owner = W.DWORD()
        u.GetWindowThreadProcessId(hwnd, C.byref(owner))
        if pid is None or owner.value == pid:
            result.append(hwnd)
        return True

    if parent is None:
        u.EnumWindows(callback, 0)
    else:
        u.EnumChildWindows(parent, callback, 0)
    return result


def wait_for(fn, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = fn()
        if result:
            return result
        time.sleep(0.03)
    raise TimeoutError("GUI condition did not complete")


def editor_bytes(hwnd, pid):
    # Scintilla pointer messages need memory in the owning process. WM_GETTEXT
    # cannot reliably distinguish its byte buffer from a Unicode Win32 buffer.
    process = k.OpenProcess(0x438, False, pid)
    if not process:
        raise C.WinError(C.get_last_error())
    remote = k.VirtualAllocEx(process, None, 65536, 0x3000, 4)
    if not remote:
        k.CloseHandle(process)
        raise C.WinError(C.get_last_error())
    try:
        message(hwnd, 2182, 65536, remote)  # SCI_GETTEXT
        buffer = C.create_string_buffer(65536)
        size = C.c_size_t()
        if not k.ReadProcessMemory(process, remote, buffer, 65536, C.byref(size)):
            raise C.WinError(C.get_last_error())
        return buffer.value
    finally:
        k.VirtualFreeEx(process, remote, 0, 0x8000)
        k.CloseHandle(process)


class Fixture(http.server.BaseHTTPRequestHandler):
    def log_message(self, *unused):
        pass

    def do_GET(self):
        if self.path in ("/model.gguf", "/wrong-range.gguf", "/ignored-range.gguf"):
            offset = int(self.headers.get("Range", "bytes=0-").split("=")[1].split("-")[0])
            if self.path == "/ignored-range.gguf":
                offset = 0
            self.send_response(206 if offset else 200)
            self.send_header("Content-Length", str(len(MODEL_DATA) - offset))
            if offset:
                self.send_header("Content-Range", "bytes %d-%d/%d" %
                                 (0 if self.path == "/wrong-range.gguf" else offset,
                                  len(MODEL_DATA) - 1, len(MODEL_DATA)))
            self.end_headers()
            try:
                for start in range(offset, len(MODEL_DATA), 16384):
                    self.wfile.write(MODEL_DATA[start:start + 16384])
                    self.wfile.flush()
                    time.sleep(0.015)
            except OSError:
                pass
            return
        body = b'{"status":"ok"}'
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def chunk(self, data):
        self.wfile.write(("%x\r\n" % len(data)).encode() + data + b"\r\n")
        self.wfile.flush()

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])).decode())
        question = request["messages"][-1]["content"]
        if isinstance(question, list):
            question = next((p["text"] for p in question if p["type"] == "text"), "")
        self.server.requests.append(request)
        if question == "FAIL500":
            body = b'{"error":{"message":"fixture backend failed"}}'
            self.send_response(500)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Connection", "close")
        self.end_headers()
        if question == "HANG":
            self.server.waiting.set()
            self.server.release.wait(10)
        answer = "ANSWER_FOR_" + question
        if question == "FRAGMENTED":
            answer += " árvíztűrő Ελληνικά 测试 😀"
        if question == "UNICODE":
            answer = FENCE + "gmpl\n# 测试\nvar x >= 0;\n" + FENCE
        record = ("data: " + json.dumps(
            {"choices": [{"delta": {"content": answer}}]}, ensure_ascii=False) + "\n\n").encode()
        try:
            if question == "FRAGMENTED":
                for offset in range(0, len(record), 7):
                    self.chunk(record[offset:offset + 7])
            else:
                self.chunk(record)
            if question != "TRUNCATED":
                self.chunk(b"data: [DONE]\n\n")
            self.wfile.write(b"0\r\n\r\n")
            self.wfile.flush()
        except OSError:
            pass


class App:
    def __init__(self, stage, root, name, port, missing=False, real_model=None, document=None,
                 bad_hash=False, extra=None, overrides=None, disabled=False, missing_reader=False):
        self.root = root / name
        self.root.mkdir()
        model = self.root / "fixture-model.gguf"
        if not missing:
            model.write_bytes(b"fixture; the loopback backend is reused")
        if real_model:
            model = real_model
        values = {
            "model": str(model), "vision": "no", "host": "127.0.0.1", "port": str(port),
            "autostart": "no", "startup_timeout": "60", "request_timeout": "5",
            "threads": "4", "n_predict": "128",
            "model_url": "http://127.0.0.1:%d/model.gguf" % port,
            "model_bytes": str(len(MODEL_DATA)), "model_sha256": hashlib.sha256(MODEL_DATA).hexdigest(),
        }
        if real_model:
            values["autostart"] = "yes"
            values["model_sha256"] = "00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4"
            values["model_bytes"] = str(real_model.stat().st_size)
            values["startup_timeout"] = "120"
            values["request_timeout"] = "60"
            values["n_predict"] = "256"
            values["temperature"] = "0.2"
        if bad_hash:
            values["model_sha256"] = "0" * 64
        if overrides:
            values.update(overrides)
        if values["vision"] == "yes":
            values.setdefault("vision_model", str(self.root / "fixture-reader.gguf"))
            values.setdefault("vision_url", "http://127.0.0.1:%d/model.gguf" % port)
            values.setdefault("vision_bytes", str(len(MODEL_DATA)))
            values.setdefault("vision_sha256", hashlib.sha256(MODEL_DATA).hexdigest())
            if not missing_reader and not real_model:
                Path(values["vision_model"]).write_bytes(b"fixture reader; existing backend is reused")
        if disabled:
            values["enabled"] = "no"
        (self.root / "GusekAI.ini").write_text(
            "".join("%s=%s\n" % item for item in values.items()), encoding="utf-8")
        os.environ["GUSEK_AI_DATA"] = str(self.root)
        os.environ["SciTE_HOME"] = str(stage)
        config_root = self.root / "editor-config"
        config_root.mkdir()
        os.environ["SciTE_USERHOME"] = str(config_root)
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        command = [str(stage / "gusek.exe"), "-check.if.already.open=0",
                   "-save.session=0", "-save.position=0", "-save.recent=0", "-ai.visible=1"]
        if extra:
            command.extend(extra)
        if document:
            command.append(str(document))
        self.process = subprocess.Popen(command, cwd=str(stage), startupinfo=startup)
        try:
            self.main = wait_for(lambda: next(
                (h for h in windows(pid=self.process.pid) if window_class(h) == "SciTEWindow"), None))
            u.ShowWindow(self.main, 0)
            check(u.GetMenuState(u.GetMenu(self.main), 470, 0) == 0xFFFFFFFF,
                  name + ": no assistant menu entry")
            self.hotkey = values.get("hotkey", "T")
            for argument in extra or []:
                if argument.startswith("-ai.hotkey="):
                    self.hotkey = argument.split("=", 1)[1]
            if disabled:
                shortcut(self.main, self.hotkey, lambda: True)
                check(not any(window_class(h) == "GusekAiPaneClass" for h in windows(self.main)),
                      "Disabled AI creates no pane or runtime")
                return
            self.pane = wait_for(lambda: next(
                (h for h in windows(self.main) if window_class(h) == "GusekAiPaneClass"), None))
            check(not self.visible(), name + ": assistant starts closed despite ai.visible=1")
            self.toggle(self.main)
            self.controls = {u.GetDlgCtrlID(h): h for h in windows(self.pane)}
            self.before_transcript = text(self.controls[3102])
            check(all(i in self.controls for i in (3101, 3102, 3104, 3105, 3106)),
                  name + ": actual assistant controls exist")
        except BaseException:
            self.close()
            raise

    def visible(self):
        return bool(u.GetWindowLongW(self.pane, -16) & 0x10000000)

    def toggle(self, target=None, repeat=False, alt=False):
        expected = self.visible() if repeat or alt else not self.visible()
        shortcut(target or self.controls[3104], self.hotkey,
                 lambda: self.visible() == expected, repeat, alt)
        check(self.visible() == expected, "Configured shortcut toggles the pane from its actual controls")

    def send(self, question):
        buffer = C.create_unicode_buffer(question)
        message(self.controls[3104], 0x000C, 0, C.cast(buffer, C.c_void_p).value)
        before = text(self.controls[3102])
        self.before_transcript = before
        u.PostMessageW(self.pane, 0x0111, 3105, 0)
        # Posted UI commands can open modal dialogs. Completion below waits for
        # the current answer, not merely an enabled button during UI dispatch.
        wait_for(lambda: not u.IsWindowEnabled(self.controls[3105]) or
                 text(self.controls[3102]) != before or
                 any(window_class(h) == "#32770" for h in windows(pid=self.process.pid)))

    def attach(self, path):
        u.PostMessageW(self.pane, 0x0111, 3205, 0)
        dialog = wait_for(lambda: next((h for h in windows(pid=self.process.pid)
                                      if window_class(h) == "#32770" and "Download" not in text(h)), None))
        wait_for(lambda: u.GetWindowLongW(dialog, -16) & 0x10000000)
        time.sleep(0.15)
        filename = next(h for h in windows(dialog) if window_class(h) == "Edit" and
                        u.GetDlgCtrlID(h) in (1152, 1148))
        buffer = C.create_unicode_buffer(str(path))
        message(filename, 0x000C, 0, C.cast(buffer, C.c_void_p).value)
        ok = next(h for h in windows(dialog) if u.GetDlgCtrlID(h) == 1 and window_class(h) == "Button")
        u.PostMessageW(filename, 0x0100, 13, 1)
        u.PostMessageW(filename, 0x0101, 13, 1)
        try:
            wait_for(lambda: not u.IsWindow(dialog))
            time.sleep(0.05)
            wait_for(lambda: u.GetWindowLongW(self.controls[3103], -16) & 0x10000000)
        except TimeoutError:
            print("Attachment status:", text(self.controls[3101]), flush=True)
            print("Own dialogs:", [(window_class(h), text(h)) for h in windows(pid=self.process.pid)
                                  if window_class(h) == "#32770"], flush=True)
            raise

    def complete(self, seconds=10):
        def finished():
            if not u.IsWindowEnabled(self.controls[3105]):
                return False
            status = text(self.controls[3101])
            transcript = text(self.controls[3102])
            fresh = transcript[len(self.before_transcript):]
            if status == "Ready" and "GUSEK assistant" in fresh and fresh.rsplit("GUSEK assistant", 1)[-1].strip():
                return (transcript,)
            if status.startswith(("Model error:", "Model server returned", "Model stream ended",
                                  "Request failed", "Stream failed")):
                return (transcript,)
            return False
        return wait_for(finished, seconds)[0]

    def dialog(self, answer):
        dialog = wait_for(lambda: next(
            (h for h in windows(pid=self.process.pid)
             if window_class(h) == "#32770" and "Download" in text(h)), None))
        u.PostMessageW(dialog, 0x0111, answer, 0)

    def close(self, graceful=False):
        if self.process.poll() is None:
            if graceful:
                u.PostMessageW(self.main, 0x0010, 0, 0)
                try:
                    self.process.wait(timeout=3)
                    check(self.process.returncode == 0, "GUSEK exits cleanly with active work")
                except subprocess.TimeoutExpired:
                    print("Shutdown own dialogs:", [(window_class(h), text(h)) for h in windows(pid=self.process.pid)
                                                   if window_class(h) == "#32770"], flush=True)
                    for dialog in windows(pid=self.process.pid):
                        if window_class(dialog) == "#32770":
                            print("Shutdown explanation:", [text(h) for h in windows(dialog)
                                                            if window_class(h) == "Static"], flush=True)
                    raise
                finally:
                    if self.process.poll() is None:
                        self.process.terminate()
            else:
                self.process.terminate()
            self.process.wait(timeout=5)


def exercise(stage, root, fixtures):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Fixture)
    server.requests = []
    server.waiting = threading.Event()
    server.release = threading.Event()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    app = None
    try:
        app = App(stage, root, "chat", server.server_port)
        visible = lambda: bool(u.GetWindowLongW(app.pane, -16) & 0x10000000)
        check(visible(), "Shortcut shows assistant pane")
        check(focus(app.main) == app.controls[3104], "Opening the pane focuses the question box")
        u.PostMessageW(app.controls[3104], 0x0100, 9, 1)
        wait_for(lambda: focus(app.main) == app.controls[3105])
        check(True, "Tab reaches the enabled Send button from the question box")
        buffer = C.create_unicode_buffer("KEYBOARD SEND")
        message(app.controls[3104], 0x000C, 0, C.cast(buffer, C.c_void_p).value)
        u.PostMessageW(app.controls[3105], 0x0100, 13, 1)
        wait_for(lambda: "ANSWER_FOR_KEYBOARD SEND" in text(app.controls[3102]))
        app.complete()
        check(True, "Enter activates a focused assistant button")
        message(app.pane, 0x0111, 3109)
        app.toggle()
        check(not visible(), "Shortcut hides assistant pane from the question box")
        check(window_class(focus(app.main)) == "Scintilla" and
              u.GetDlgCtrlID(focus(app.main)) == 350, "Closing the assistant returns focus to the editor")
        app.toggle(app.main)
        app.send("FIRST")
        check("ANSWER_FOR_FIRST" in app.complete(), "First streamed answer is visible")
        app.send("SECOND")
        app.complete()
        check([m["role"] for m in server.requests[-1]["messages"]] ==
              ["system", "user", "assistant", "user"], "Follow-up includes both prior turns")
        check(server.requests[-1]["messages"][2]["content"] == "ANSWER_FOR_FIRST",
              "History retains the actual assistant response")
        app.send("FRAGMENTED")
        check("ANSWER_FOR_FRAGMENTED árvíztűrő Ελληνικά 测试 😀" in app.complete(),
              "Split HTTP chunks and Unicode preserve the whole answer")
        app.send("FAIL500")
        app.complete()
        check("500" in text(app.controls[3101]), "HTTP failure remains visible")
        check("fixture backend failed" in text(app.controls[3101]), "Backend error includes its useful explanation")
        app.send("TRUNCATED")
        app.complete()
        check("before completion" in text(app.controls[3101]), "Incomplete SSE stream fails visibly")
        app.send("RECOVERED")
        check("ANSWER_FOR_RECOVERED" in app.complete(), "Another request succeeds after HTTP failure")
        app.send("HANG")
        wait_for(server.waiting.is_set)
        start = time.monotonic()
        message(app.pane, 0x0111, 3106)
        check(time.monotonic() - start < 1.5, "Stop interrupts a stalled stream promptly")
        check(u.IsWindowEnabled(app.controls[3105]), "Send recovers after Stop")
        server.waiting.clear()
        app.send("HANG")
        wait_for(server.waiting.is_set)
        start = time.monotonic()
        message(app.pane, 0x0111, 3109)
        check(time.monotonic() - start < 1.5, "New Chat directly cancels an active stream")
        server.release.set()
        app.send("NEW")
        transcript = app.complete()
        check("ANSWER_FOR_NEW" in transcript and "ANSWER_FOR_HANG" not in transcript,
              "Cancelled replies cannot enter the new chat")
        check(len(server.requests[-1]["messages"]) == 2, "New Chat clears model history")
        app.send("UNICODE")
        app.complete()
        editor = next(h for h in windows(app.main)
                      if window_class(h) == "Scintilla" and u.GetDlgCtrlID(h) == 350)
        check(message(editor, 2137) == 0, "Fixture begins in a legacy encoded document")
        message(app.pane, 0x0111, 3108)
        check(message(editor, 2137) == 65001, "Unrepresentable code opens a UTF-8 document")
        check(editor_bytes(editor, app.process.pid) == "# 测试\nvar x >= 0;".encode(),
              "To editor preserves every Unicode character")
        app.close()
        app = App(stage, root, "disabled", server.server_port, disabled=True)
        app.close()
        with socket.socket() as free:
            free.bind(("127.0.0.1", 0))
            unused_port = free.getsockname()[1]
        app = App(stage, root, "no-autostart", unused_port)
        app.send("NO AUTOMATIC SERVER")
        app.complete()
        check("autostart" in text(app.controls[3101]).lower(), "autostart=no does not launch a server")
        app.close()
        course = root / "árvíztűrő-课程"
        course.mkdir()
        (course / "课程.md").write_text("UNICODE_COURSE_SENTINEL optimization", encoding="utf-8")
        (course / "README.md").write_text("EXCLUDED_README_SENTINEL", encoding="utf-8")
        app = App(stage, root, "árvíztűrő-测试", server.server_port,
                  overrides={"context_dir": str(course), "hotkey": "K"})
        app.toggle(repeat=True)
        app.toggle(alt=True)
        app.toggle()
        app.toggle(app.main)
        (app.root / "system_prompt.txt").write_text("CUSTOM_PROMPT_SENTINEL", encoding="utf-8")
        app.send("optimization")
        app.complete()
        system = server.requests[-1]["messages"][0]["content"]
        (app.root / "sent-system.txt").write_text(system, encoding="utf-8")
        check("CUSTOM_PROMPT_SENTINEL" in system and "UNICODE_COURSE_SENTINEL" in system,
              "Unicode data and course paths load editable prompts and notes")
        check("EXCLUDED_README_SENTINEL" not in system, "Course instructions exclude README scaffolding")
        app.close()
        app = App(stage, root, "pictures", server.server_port, overrides={"vision": "yes"})
        picture = fixtures / "picture-42.png"
        app.attach(picture)
        strip = app.controls[3103]
        message(strip, 0x0202, 0, 10 | (25 << 16))
        viewer = wait_for(lambda: next((h for h in windows(pid=app.process.pid)
                                      if window_class(h) == "GusekAiImageViewer"), None))
        message(viewer, 0x0100, 27)
        check(not any(window_class(h) == "GusekAiImageViewer" for h in windows(pid=app.process.pid)),
              "Attachment opens a picture viewer and Escape closes it")
        app.attach(fixtures / "picture-42.jpg")
        rc = W.RECT()
        u.GetClientRect(strip, C.byref(rc))
        tile = min(150, rc.right // 2)
        message(strip, 0x0202, 0, (tile - 5) | (5 << 16))
        app.send("")
        app.complete()
        parts = server.requests[-1]["messages"][-1]["content"]
        check(sum(p["type"] == "image_url" for p in parts) == 1,
              "Per-picture removal keeps the remaining attachment")
        check(any(p["type"] == "text" and p["text"] for p in parts), "Image-only send supplies a useful question")
        check(not (u.GetWindowLongW(strip, -16) & 0x10000000), "Send transfers pictures from the attachment strip")
        for i in range(5):
            app.attach(picture)
        app.send("NEW PICTURES")
        app.complete()
        messages = server.requests[-1]["messages"]
        (app.root / "picture-requests.json").write_text(json.dumps(messages, ensure_ascii=False, indent=2), encoding="utf-8")
        total = sum(p["type"] == "image_url" for m in messages if isinstance(m["content"], list) for p in m["content"])
        check(total == 4 and isinstance(messages[1]["content"], str) and "picture" in messages[1]["content"],
              "Picture follow-up retains only the four newest images and marks older images")
        app.send("TEXT FOLLOW-UP")
        app.complete()
        check(isinstance(server.requests[-1]["messages"][-3]["content"], list),
              "Text follow-up still includes the previous picture turn")
        app.close()
        app = App(stage, root, "reader-only", server.server_port,
                  overrides={"vision": "yes"}, missing_reader=True)
        base_before = (app.root / "fixture-model.gguf").read_bytes()
        app.attach(picture)
        app.dialog(7)
        message(app.pane, 0x0111, 3207)
        app.send("TEXT WITHOUT READER")
        check("ANSWER_FOR_TEXT WITHOUT READER" in app.complete(), "Declining the picture reader preserves text chat")
        app.attach(picture)
        app.dialog(6)
        wait_for(lambda: "Model ready" in text(app.controls[3101]), 15)
        check((app.root / "models/fixture-reader.gguf").read_bytes() == MODEL_DATA and
              (app.root / "fixture-model.gguf").read_bytes() == base_before,
              "Reader-only installation verifies the reader and preserves the base model")
        app.send("PICTURE AFTER READER")
        check("ANSWER_FOR_PICTURE AFTER READER" in app.complete(), "Picture works after reader installation without restarting GUSEK")
        app.close()
        app = App(stage, root, "clipboard-pictures", server.server_port, overrides={"vision": "yes"})
        with ClipboardFixture() as clipboard:
            width, height = 160, 96
            rgb = b''.join(b'\xff\0\0' if x < width // 2 else b'\0\0\xff'
                           for y in range(height) for x in range(width))
            for v5, command in ((False, 0x0102), (True, 0x0302)):
                message(app.pane, 0x0111, 3109)
                clipboard.publish(*screenshot_dib(width, height, rgb, v5))
                message(app.controls[3104], command, 22 if command == 0x0102 else 0)
                check(bool(u.GetWindowLongW(app.controls[3103], -16) & 0x10000000),
                      "Screenshot paste attaches a picture through " + ("native Ctrl+V" if command == 0x0102 else "WM_PASTE"))
                check(text(app.controls[3104]) == "", "Screenshot paste keeps the question box free of embedded OLE pictures")
                app.send("DESCRIBE SCREENSHOT")
                app.complete()
                parts = server.requests[-1]["messages"][-1]["content"]
                urls = [p["image_url"]["url"] for p in parts if p["type"] == "image_url"]
                check(len(urls) == 1, "Pasted screenshot is sent exactly once in the current question")
                received = png_pixels(base64.b64decode(urls[0].split(',', 1)[1], validate=True))
                check(received == (width, height, rgb), "Screenshot request preserves its actual dimensions and colored pixels")
            app.send("SCREENSHOT FOLLOW-UP")
            app.complete()
            prior = server.requests[-1]["messages"][-3]["content"]
            check(any(p["type"] == "image_url" and p["image_url"]["url"] == urls[0] for p in prior),
                  "Follow-up retains the exact pasted screenshot bytes after the attachment strip clears")
            message(app.pane, 0x0111, 3109)
            plain = "Plain clipboard text: árvíztűrő 测试"
            clipboard.publish(13, (plain + '\0').encode('utf-16le'))
            message(app.controls[3104], 0x0102, 22)
            check(text(app.controls[3104]) == plain and not (u.GetWindowLongW(app.controls[3103], -16) & 0x10000000),
                  "Native Ctrl+V still pastes Unicode text without creating a picture")
        app.close()
        legacy_document = root / "legacy.mod"
        original = b"# keep existing model\nvar y;\n"
        legacy_document.write_bytes(original)
        app = App(stage, root, "full-tabs", server.server_port,
                  document=legacy_document, extra=["-buffers=1"])
        app.send("UNICODE")
        app.complete()
        editor = next(h for h in windows(app.main)
                      if window_class(h) == "Scintilla" and u.GetDlgCtrlID(h) == 350)
        message(app.pane, 0x0111, 3108)
        check(editor_bytes(editor, app.process.pid) == original,
              "Encoding fallback preserves the original when no new tab is available")
        check(message(editor, 2137) == 0, "Existing document save encoding remains unchanged")
        app.close()
        app = App(stage, root, "declined", server.server_port, missing=True)
        app.send("FIRST RUN")
        app.dialog(7)
        wait_for(lambda: "declined" in text(app.controls[3101]).lower())
        check(u.IsWindowEnabled(app.controls[3105]), "Declining download leaves Send available")
        message(app.pane, 0x0111, 3109)
        app.send("RETRY")
        app.dialog(7)
        check(u.IsWindowEnabled(app.controls[3105]), "Download can be offered again after New Chat")
        app.close()
        app = App(stage, root, "download", server.server_port, missing=True)
        app.send("INSTALL")
        app.dialog(6)
        part = app.root / "models/fixture-model.gguf.part"
        wait_for(lambda: part.exists() and part.stat().st_size >= 65536)
        message(app.pane, 0x0111, 3106)
        check(part.exists() and 0 < part.stat().st_size < len(MODEL_DATA),
              "Stop keeps a resumable model part")
        check(u.IsWindowEnabled(app.controls[3105]), "Send recovers after download pause")
        app.send("RESUME")
        app.dialog(6)
        wait_for(lambda: "Model ready" in text(app.controls[3101]), 15)
        target = app.root / "models/fixture-model.gguf"
        check(target.read_bytes() == MODEL_DATA, "Resumed first-run model matches its verified fixture")
        app.send("AFTER DOWNLOAD")
        check("ANSWER_FOR_AFTER DOWNLOAD" in app.complete(),
              "Inference succeeds after download without restarting GUSEK")
        app.close()
        app = App(stage, root, "bad-download", server.server_port, missing=True, bad_hash=True)
        app.send("INSTALL")
        app.dialog(6)
        wait_for(lambda: "checksum" in text(app.controls[3101]).lower(), 15)
        check(u.IsWindowEnabled(app.controls[3105]), "Send recovers after a failed download")
        check(not (app.root / "models/fixture-model.gguf").exists(),
              "A bad model checksum cannot become an installed model")
        app.close()
        for name, route in (("bad-range", "/wrong-range.gguf"), ("ignored-range", "/ignored-range.gguf"),
                            ("corrupt-complete-part", "/model.gguf")):
            app = App(stage, root, name, server.server_port, missing=True,
                      overrides={"model_url": "http://127.0.0.1:%d%s" % (server.server_port, route)})
            part = app.root / "models/fixture-model.gguf.part"
            part.write_bytes(b"X" * len(MODEL_DATA) if name == "corrupt-complete-part" else MODEL_DATA[:65536])
            app.send("RESUME DOWNLOAD")
            app.dialog(6)
            if name == "bad-range":
                wait_for(lambda: "range" in text(app.controls[3101]).lower(), 15)
                check(u.IsWindowEnabled(app.controls[3105]) and not (app.root / "models/fixture-model.gguf").exists(),
                      "Invalid Content-Range fails visibly without installing a corrupt model")
            else:
                wait_for(lambda: "Model ready" in text(app.controls[3101]), 15)
                check((app.root / "models/fixture-model.gguf").read_bytes() == MODEL_DATA,
                      name + ": download restarts and verifies the complete model")
            app.close()
        document = root / "solver.mod"
        document.write_text('printf {i in 1..2000} "line %d\\n", i;\nend;\n', encoding="utf-8")
        server.waiting.clear()
        server.release.clear()
        app = App(stage, root, "solver", server.server_port, document=document)
        app.send("HANG")
        wait_for(server.waiting.is_set)
        message(app.main, 0x0111, 303)  # Explicitly run this test's own MathProg fixture.
        for unused in range(40):
            message(app.pane, 0x0111, 3202)  # Attach while the solver can still write.
            buffer = C.create_unicode_buffer("")
            if "line 2000" in text(app.controls[3104]):
                break
            message(app.controls[3104], 0x000C, 0, C.cast(buffer, C.c_void_p).value)
            time.sleep(0.03)
        check("line 2000" in text(app.controls[3104]),
              "Solver output can be attached safely during an active model request")
        message(app.pane, 0x0111, 3106)
        app.close()
        server.release.set()
        server.waiting.clear()
        server.release.clear()
        app = App(stage, root, "shutdown", server.server_port)
        app.send("HANG")
        wait_for(server.waiting.is_set)
        app.close(graceful=True)
        app = None
        (root / "requests.json").write_text(json.dumps(server.requests, ensure_ascii=False, indent=2),
                                            encoding="utf-8")
    finally:
        server.release.set()
        if app:
            app.close()
        server.shutdown()
        server.server_close()


def real_inference(stage, root, model, reader, fixtures):
    class ReaderFixture(http.server.BaseHTTPRequestHandler):
        def log_message(self, *unused):
            pass

        def do_GET(self):
            offset = int(self.headers.get("Range", "bytes=0-").split("=")[1].split("-")[0])
            size = reader.stat().st_size
            self.send_response(206 if offset else 200)
            self.send_header("Content-Length", str(size - offset))
            if offset:
                self.send_header("Content-Range", "bytes %d-%d/%d" % (offset, size - 1, size))
            self.end_headers()
            try:
                with reader.open("rb") as stream:
                    stream.seek(offset)
                    while data := stream.read(65536):
                        self.wfile.write(data)
            except OSError:
                pass

    source = http.server.ThreadingHTTPServer(("127.0.0.1", 0), ReaderFixture)
    threading.Thread(target=source.serve_forever, daemon=True).start()
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    app = App(stage, root, "real-model", port, real_model=model, missing_reader=True,
              overrides={"vision": "yes", "vision_model": str(root / "real-model/models/reader.gguf"),
                         "vision_url": "http://127.0.0.1:%d/reader.gguf" % source.server_port,
                         "vision_bytes": str(reader.stat().st_size),
                         "vision_sha256": "cd88edcf8d031894960bb0c9c5b9b7e1fea6ebee02b9f7ce925a00d12891f864"})
    try:
        wait_for(lambda: u.IsWindowEnabled(app.controls[3105]), 120)
        check(text(app.controls[3101]) == "Ready", "Actual CPU model warms up without blocking the UI")
        app.send("Create a complete GNU MathProg linear program with exactly one scalar "
                 "decision variable named x. Declare x with var BEFORE defining the objective, maximize x, and constrain "
                 "x <= 5. Include solve; and end;. Use no sets, indexed quantities, or "
                 "parameters. Reply only with one fenced mod code block.")
        transcript = app.complete(120)
        (app.root / "transcript.txt").write_text(transcript, encoding="utf-8")
        (app.root / "status.txt").write_text(text(app.controls[3101]), encoding="utf-8")
        check("var " in transcript and "maximize " in transcript and "solve;" in transcript,
              "Actual GUSEK CPU inference produces MathProg code")
        message(app.pane, 0x0111, 3108)
        editor = next(h for h in windows(app.main)
                      if window_class(h) == "Scintilla" and u.GetDlgCtrlID(h) == 350)
        code = editor_bytes(editor, app.process.pid)
        model_file = app.root / "response.mod"
        model_file.write_bytes(code)
        result = subprocess.run([str(stage / "glpsol.exe"), "--math", str(model_file)],
                                capture_output=True, timeout=15,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        (app.root / "solver.log").write_bytes(result.stdout + result.stderr)
        check(result.returncode == 0 and b"OPTIMAL" in result.stdout,
              "Test explicitly solves the generated MathProg model")
        message(editor, 2014)  # Mark this test's scratch buffer clean.
        picture = app.root / "number.png"
        picture.write_bytes((fixtures / "picture-42.png").read_bytes())
        app.attach(picture)
        app.dialog(6)
        wait_for(lambda: "Model ready" in text(app.controls[3101]), 120)
        check((app.root / "models/reader.gguf").stat().st_size == reader.stat().st_size,
              "Reader-only download installs the actual pinned projector")
        with socket.socket() as sock:
            sock.settimeout(1)
            check(sock.connect_ex(("127.0.0.1", port)) != 0,
                  "Installing the reader stops the text-only owned server for restart")
        app.send("Read the two-digit number printed in the picture. Reply with that number only.")
        transcript = app.complete(180)
        answer = transcript.rsplit("GUSEK assistant", 1)[-1]
        check("42" in answer, "Actual CPU vision reads the number from the attached picture")
        app.send("What is half the number in that picture? Reply with the result only.")
        transcript = app.complete(120)
        answer = transcript.rsplit("GUSEK assistant", 1)[-1]
        check("21" in answer, "Actual picture history supports a later text-only follow-up")
        message(app.pane, 0x0111, 3109)
        with ClipboardFixture() as clipboard:
            width, height, pixels = png_pixels((fixtures / "clipboard-number.png").read_bytes())
            clipboard.publish(*screenshot_dib(width, height, pixels))
            message(app.controls[3104], 0x0102, 22)
            check(bool(u.GetWindowLongW(app.controls[3103], -16) & 0x10000000),
                  "Native screenshot paste attaches to the actual CPU vision conversation")
        app.send("Read the two-digit number printed in the picture. Reply with that number only.")
        transcript = app.complete(180)
        answer = transcript.rsplit("GUSEK assistant", 1)[-1]
        check(answer.strip() == "68", "Actual CPU vision reads a different number from the pasted screenshot")
        app.send("What is half the number in that picture? Reply with the result only.")
        transcript = app.complete(120)
        answer = transcript.rsplit("GUSEK assistant", 1)[-1]
        check(answer.strip() == "34", "Actual pasted screenshot remains visible to the model in a follow-up")
        (app.root / "vision-transcript.txt").write_text(transcript, encoding="utf-8")
        app.send("Explain how the constraint bounds the optimum.")
        wait_for(lambda: "Answering" in text(app.controls[3101]), 10)
        app.close(graceful=True)
        with socket.socket() as sock:
            sock.settimeout(1)
            check(sock.connect_ex(("127.0.0.1", port)) != 0,
                  "Closing GUSEK stops its owned real model server")
    finally:
        app.close()
        source.shutdown()
        source.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--real-model", type=Path)
    parser.add_argument("--real-reader", type=Path)
    parser.add_argument("--fixtures", type=Path)
    options = parser.parse_args()
    root = options.work_root / ("gui-" + uuid.uuid4().hex)
    root.mkdir(parents=True)
    (root / "tmp").mkdir()
    os.environ["TEMP"] = os.environ["TMP"] = str(root / "tmp")
    if options.real_model:
        if not options.real_reader:
            parser.error("--real-model requires --real-reader")
        real_inference(options.stage, root, options.real_model, options.real_reader,
                       options.fixtures or options.work_root / "native")
    else:
        exercise(options.stage, root, options.fixtures or options.work_root / "native")
    print("GUI regression checks passed:", CHECKS, flush=True)

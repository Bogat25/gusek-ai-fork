"""Exercise the built Win32 assistant with isolated profiles and loopback fixtures."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import http.server
import json
import os
from pathlib import Path
import socket
import subprocess
import threading
import time
import uuid

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
        if self.path == "/model.gguf":
            offset = int(self.headers.get("Range", "bytes=0-").split("=")[1].split("-")[0])
            self.send_response(206 if offset else 200)
            self.send_header("Content-Length", str(len(MODEL_DATA) - offset))
            if offset:
                self.send_header("Content-Range", "bytes %d-%d/%d" %
                                 (offset, len(MODEL_DATA) - 1, len(MODEL_DATA)))
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
        self.server.requests.append(request)
        if question == "FAIL500":
            self.send_response(500)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
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
            self.chunk(b"data: [DONE]\n\n")
            self.wfile.write(b"0\r\n\r\n")
            self.wfile.flush()
        except OSError:
            pass


class App:
    def __init__(self, stage, root, name, port, missing=False, real_model=None, document=None, bad_hash=False, extra=None):
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
            values["request_timeout"] = "60"
            values["n_predict"] = "256"
            values["temperature"] = "0.2"
        if bad_hash:
            values["model_sha256"] = "0" * 64
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
                   "-save.session=0", "-save.position=0", "-save.recent=0"]
        if extra:
            command.extend(extra)
        if document:
            command.append(str(document))
        self.process = subprocess.Popen(command, cwd=str(stage), startupinfo=startup)
        try:
            self.main = wait_for(lambda: next(
                (h for h in windows(pid=self.process.pid) if window_class(h) == "SciTEWindow"), None))
            u.ShowWindow(self.main, 0)
            message(self.main, 0x0111, 470)
            self.pane = wait_for(lambda: next(
                (h for h in windows(self.main) if window_class(h) == "GusekAiPaneClass"), None))
            self.controls = {u.GetDlgCtrlID(h): h for h in windows(self.pane)}
            check(all(i in self.controls for i in (3101, 3102, 3104, 3105, 3106)),
                  name + ": actual assistant controls exist")
        except BaseException:
            self.close()
            raise

    def send(self, question):
        buffer = C.create_unicode_buffer(question)
        message(self.controls[3104], 0x000C, 0, C.cast(buffer, C.c_void_p).value)
        message(self.pane, 0x0111, 3105)

    def complete(self, seconds=10):
        wait_for(lambda: u.IsWindowEnabled(self.controls[3105]), seconds)
        return text(self.controls[3102])

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
                finally:
                    if self.process.poll() is None:
                        self.process.terminate()
            else:
                self.process.terminate()
            self.process.wait(timeout=5)


def exercise(stage, root):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Fixture)
    server.requests = []
    server.waiting = threading.Event()
    server.release = threading.Event()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    app = None
    try:
        app = App(stage, root, "chat", server.server_port)
        visible = lambda: bool(u.GetWindowLongW(app.pane, -16) & 0x10000000)
        check(visible(), "Menu shows assistant pane")
        message(app.main, 0x0111, 470)
        check(not visible(), "Menu hides assistant pane")
        message(app.main, 0x0111, 470)
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


def real_inference(stage, root, model):
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    app = App(stage, root, "real-model", port, real_model=model)
    try:
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
        app.send("Explain how the constraint bounds the optimum.")
        wait_for(lambda: "Answering" in text(app.controls[3101]), 10)
        app.close(graceful=True)
        with socket.socket() as sock:
            sock.settimeout(1)
            check(sock.connect_ex(("127.0.0.1", port)) != 0,
                  "Closing GUSEK stops its owned real model server")
    finally:
        app.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--real-model", type=Path)
    options = parser.parse_args()
    root = options.work_root / ("gui-" + uuid.uuid4().hex)
    root.mkdir(parents=True)
    (root / "tmp").mkdir()
    os.environ["TEMP"] = os.environ["TMP"] = str(root / "tmp")
    if options.real_model:
        real_inference(options.stage, root, options.real_model)
    else:
        exercise(options.stage, root)
    print("GUI regression checks passed:", CHECKS, flush=True)

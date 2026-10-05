"""
Fake OpenAI / llama-server compatibility server for GUSEK AI Assistant testing.
Listens on the configured port (default 28713) on 127.0.0.1.
Supports:
  - GET /health
  - POST /v1/chat/completions (SSE streaming)
  - GET /file/ (HTTP Range downloads)
"""
import sys
import json
import socket
import threading
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 28713
RUNNING = True

PIECES = [
    None,  # role-only delta
    "<thi", "nk>", "The user asks for a linear programming example in MathProg.", "</thi", "nk>\n",
    "Here is a simple **MathProg** optimization model:\n\n",
    "```mod\n",
    "# Decision variable\n",
    "var x >= 0;\n",
    "maximize obj: 5 * x;\n",
    "s.t. limit: x <= 20;\n",
    "solve;\n",
    "```\n\n",
    "Notice that the objective value will be 100 with x = 20. Unicode check: é, á, ő, ű and 😀.",
]

PIECES_NOCODE = [
    "In MathProg, parameters represent fixed numerical problem data, ",
    "whereas decision variables are optimized by the solver.",
]

DOWNLOAD_DATA = bytes((i * 17 + 5) & 0xFF for i in range(1024 * 1024))  # 1 MB deterministic data

def sse_record(piece):
    if piece is None:
        delta = {"role": "assistant"}
    else:
        delta = {"content": piece}
    obj = {
        "id": "chatcmpl-test",
        "object": "chat.completion.chunk",
        "created": int(time.time()),
        "model": "qwen3.5-4b",
        "choices": [{"index": 0, "delta": delta, "finish_reason": None}]
    }
    return "data: " + json.dumps(obj, ensure_ascii=False) + "\n\n"

def chunk_data(data):
    return ("%x\r\n" % len(data)).encode('ascii') + data + b"\r\n"

def handle_client(conn, addr):
    global RUNNING
    conn.settimeout(10.0)
    buf = b""
    try:
        while b"\r\n\r\n" not in buf:
            chunk = conn.recv(4096)
            if not chunk:
                conn.close()
                return
            buf += chunk

        head, _, rest = buf.partition(b"\r\n\r\n")
        lines = head.split(b"\r\n")
        req_line = lines[0].decode('latin1', errors='replace')
        parts = req_line.split(" ")
        if len(parts) < 2:
            conn.close()
            return
        method, path = parts[0], parts[1]

        # 1. Health check
        if method == "GET" and path == "/health":
            body = b'{"status":"ok"}'
            resp = (b"HTTP/1.1 200 OK\r\n"
                    b"Content-Type: application/json\r\n"
                    b"Content-Length: " + str(len(body)).encode('ascii') + b"\r\n"
                    b"Connection: close\r\n\r\n" + body)
            conn.sendall(resp)
            conn.close()
            return

        # 2. File download with Range support
        if method == "GET" and path.startswith("/file/"):
            range_header = None
            for h in lines[1:]:
                hl = h.decode('latin1', errors='replace')
                if hl.lower().startswith("range:"):
                    range_header = hl.split(":", 1)[1].strip()

            start_byte = 0
            if range_header and range_header.startswith("bytes="):
                val = range_header[6:].split("-")[0]
                if val:
                    start_byte = int(val)

            total_len = len(DOWNLOAD_DATA)
            if start_byte >= total_len:
                conn.sendall(b"HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                conn.close()
                return

            slice_data = DOWNLOAD_DATA[start_byte:]
            status = b"206 Partial Content" if start_byte > 0 else b"200 OK"
            cr_hdr = b""
            if start_byte > 0:
                end_byte = total_len - 1
                cr_hdr = f"Content-Range: bytes {start_byte}-{end_byte}/{total_len}\r\n".encode('ascii')
            resp_head = (b"HTTP/1.1 " + status + b"\r\n"
                         b"Content-Type: application/octet-stream\r\n"
                         + cr_hdr +
                         b"Content-Length: " + str(len(slice_data)).encode('ascii') + b"\r\n"
                         b"Connection: close\r\n\r\n")
            conn.sendall(resp_head)
            for i in range(0, len(slice_data), 16384):
                conn.sendall(slice_data[i:i + 16384])
            conn.close()
            return

        # 3. Chat completion
        if method == "POST" and path == "/v1/chat/completions":
            content_length = 0
            for h in lines[1:]:
                hl = h.decode('latin1', errors='replace')
                if hl.lower().startswith("content-length:"):
                    content_length = int(hl.split(":", 1)[1].strip())

            body_bytes = rest
            while len(body_bytes) < content_length:
                chunk = conn.recv(4096)
                if not chunk:
                    break
                body_bytes += chunk

            try:
                req_obj = json.loads(body_bytes.decode('utf-8'))
            except Exception:
                req_obj = {}

            # Check for simulated 500 error request
            req_str = body_bytes.decode('utf-8', errors='replace')
            if "FAIL500" in req_str:
                err_body = b'{"error":{"message":"Simulated internal server error","code":500}}'
                resp = (b"HTTP/1.1 500 Internal Server Error\r\n"
                        b"Content-Type: application/json\r\n"
                        b"Content-Length: " + str(len(err_body)).encode('ascii') + b"\r\n"
                        b"Connection: close\r\n\r\n" + err_body)
                conn.sendall(resp)
                conn.close()
                return

            # Choose pieces
            pieces = PIECES_NOCODE if "NOCODE" in req_str else PIECES

            resp_head = (b"HTTP/1.1 200 OK\r\n"
                         b"Content-Type: text/event-stream\r\n"
                         b"Transfer-Encoding: chunked\r\n"
                         b"Connection: close\r\n\r\n")
            conn.sendall(resp_head)

            # Send chunks with small delay
            for p in pieces:
                chunk_payload = sse_record(p).encode('utf-8')
                conn.sendall(chunk_data(chunk_payload))
                time.sleep(0.01)

            # Done signal
            conn.sendall(chunk_data(b"data: [DONE]\n\n"))
            conn.sendall(b"0\r\n\r\n")
            conn.close()
            return

        # 4. Quit endpoint
        if method == "POST" and path == "/quit":
            RUNNING = False
            conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK")
            conn.close()
            return

        conn.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
        conn.close()
    except Exception as ex:
        try:
            conn.close()
        except Exception:
            pass

def main():
    global RUNNING
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", PORT))
    server.listen(10)
    server.settimeout(1.0)
    print(f"Fake llama-server listening on http://127.0.0.1:{PORT}")

    while RUNNING:
        try:
            conn, addr = server.accept()
            t = threading.Thread(target=handle_client, args=(conn, addr))
            t.daemon = True
            t.start()
        except socket.timeout:
            continue
        except Exception:
            break

    server.close()
    print("Fake llama-server stopped.")

if __name__ == "__main__":
    main()

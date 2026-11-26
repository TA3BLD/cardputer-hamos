#!/usr/bin/env python3
"""HamOS PC program: control the Cardputer from the computer over USB.

    pip install pyserial
    python hamos_pc.py

It waits for the Cardputer on USB, connects as soon as it is plugged in (and
again after it is unplugged and plugged back), and opens the control page
http://localhost:8073 in the web browser.

    --port /dev/ttyACM0     use this serial port instead of looking for it
    --http 8073             web page port
    --no-browser            do not open the browser

The page itself (hamos.html, next to this file) also works on its own in
Chrome or Edge, straight over USB (Web Serial), without this program.
"""
import argparse
import json
import os
import queue
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial is missing:  pip install pyserial")

ESPRESSIF_VID = 0x303A
HERE = os.path.dirname(os.path.abspath(__file__))


class Link:
    """The serial connection: requests by id, screen lines to listeners."""

    def __init__(self, port_name):
        self.port_name = port_name
        self.port = None
        self.lock = threading.Lock()
        self.waiting = {}            # id -> [event, reply]
        self.next_id = 1
        self.listeners = set()
        self.listeners_lock = threading.Lock()
        self.device = None           # last "hello"
        threading.Thread(target=self.run, daemon=True).start()

    # ---- port handling ----
    def find(self):
        if self.port_name:
            return self.port_name
        for p in serial.tools.list_ports.comports():
            if p.vid == ESPRESSIF_VID:
                return p.device
        return None

    def run(self):
        while True:
            name = self.find()
            if not name:
                time.sleep(1)
                continue
            try:
                self.port = serial.Serial(name, 115200, timeout=0.2)
                self.port.reset_input_buffer()
            except (serial.SerialException, OSError):
                self.port = None
                time.sleep(1)
                continue
            print("Cardputer connected on", name)
            threading.Thread(target=self.greet, daemon=True).start()
            self.read_lines()
            print("Cardputer disconnected")
            self.port = None
            self.device = None
            self.broadcast("#disconnected")
            for w in list(self.waiting.values()):
                w[0].set()
            time.sleep(1)

    def greet(self):
        time.sleep(0.3)
        reply = self.request({"cmd": "hello"}, timeout=3)
        if reply.get("ok"):
            self.device = reply
            self.broadcast("#connected " + json.dumps(reply))

    def read_lines(self):
        buf = b""
        while True:
            try:
                chunk = self.port.read(4096)
            except (serial.SerialException, OSError, TypeError, AttributeError):
                return
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                if text.startswith("{"):
                    try:
                        reply = json.loads(text)
                    except ValueError:
                        continue
                    w = self.waiting.get(reply.get("id"))
                    if w:
                        w[1] = reply
                        w[0].set()
                elif text.startswith("~"):
                    self.broadcast(text)

    # ---- requests ----
    def request(self, req, timeout=5):
        if not self.port:
            return {"ok": False, "error": "Cardputer not connected"}
        with self.lock:
            rid = self.next_id
            self.next_id += 1
        req = dict(req, id=rid)
        w = [threading.Event(), None]
        self.waiting[rid] = w
        try:
            with self.lock:
                self.port.write((json.dumps(req, ensure_ascii=False) + "\n").encode("utf-8"))
        except (serial.SerialException, OSError, AttributeError):
            self.waiting.pop(rid, None)
            return {"ok": False, "error": "Cardputer not connected"}
        w[0].wait(timeout)
        self.waiting.pop(rid, None)
        return w[1] or {"ok": False, "error": "no answer from the Cardputer"}

    # ---- screen lines to the web pages ----
    def broadcast(self, line):
        with self.listeners_lock:
            for q in list(self.listeners):
                try:
                    q.put_nowait(line)
                except queue.Full:
                    pass


def make_handler(link):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send(self, code, body, kind):
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                with open(os.path.join(HERE, "hamos.html"), "rb") as f:
                    self.send(200, f.read(), "text/html; charset=utf-8")
            elif self.path == "/events":
                self.events()
            elif self.path == "/link":
                body = json.dumps({"connected": link.port is not None, "device": link.device}).encode()
                self.send(200, body, "application/json")
            else:
                self.send(404, b"not found", "text/plain")

        def do_POST(self):
            if self.path != "/api":
                return self.send(404, b"not found", "text/plain")
            # Only the page served from here may use the API (not other web sites).
            origin = self.headers.get("Origin")
            if origin and origin not in ("http://localhost:%d" % self.server.server_port,
                                         "http://127.0.0.1:%d" % self.server.server_port):
                return self.send(403, b"forbidden", "text/plain")
            length = int(self.headers.get("Content-Length", 0))
            try:
                req = json.loads(self.rfile.read(length) or b"{}")
            except ValueError:
                return self.send(400, b'{"ok":false,"error":"bad JSON"}', "application/json")
            reply = link.request(req, timeout=8 if req.get("cmd", "").startswith("files.") else 5)
            reply.pop("id", None)
            self.send(200, json.dumps(reply, ensure_ascii=False).encode("utf-8"), "application/json")

        def events(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            q = queue.Queue(maxsize=20000)
            with link.listeners_lock:
                link.listeners.add(q)
            try:
                first = "#connected " + json.dumps(link.device) if link.device else "#disconnected"
                self.wfile.write(("data: %s\n\n" % first).encode())
                self.wfile.flush()
                while True:
                    try:
                        lines = [q.get(timeout=15)]
                    except queue.Empty:
                        self.wfile.write(b": keep-alive\n\n")
                        self.wfile.flush()
                        continue
                    while not q.empty() and len(lines) < 500:
                        lines.append(q.get_nowait())
                    self.wfile.write("".join("data: %s\n\n" % l for l in lines).encode("utf-8"))
                    self.wfile.flush()
            except OSError:
                pass
            finally:
                with link.listeners_lock:
                    link.listeners.discard(q)

    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (found automatically when left out)")
    ap.add_argument("--http", type=int, default=8073, help="web page port (default 8073)")
    ap.add_argument("--no-browser", action="store_true", help="do not open the web browser")
    args = ap.parse_args()
    link = Link(args.port)
    server = ThreadingHTTPServer(("127.0.0.1", args.http), make_handler(link))
    server.daemon_threads = True
    url = "http://localhost:%d" % args.http
    print("HamOS PC: %s  (waiting for the Cardputer on USB; Ctrl+C to quit)" % url)
    if not args.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

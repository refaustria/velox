#!/usr/bin/env python3
"""Stand-in for veloxmaster in tests (task M6-04): prints every heartbeat body it receives, answers 204.

Usage: python3 -I Tools/ci/heartbeat_stub.py [port] [--max N]
Binds 127.0.0.1 only. Each received POST prints one line "HB <iso time> <path> <body>"; other requests get 404.
Stdlib only. Exits after N heartbeats when --max is given.
"""
import sys
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer

MAX_BODY = 8192


def main() -> int:
    args = [a for a in sys.argv[1:]]
    limit = None
    if "--max" in args:
        i = args.index("--max")
        limit = int(args[i + 1])
        del args[i:i + 2]
    port = int(args[0]) if args else 8791
    count = {"n": 0}
    lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):  # noqa: N802
            length = int(self.headers.get("Content-Length") or 0)
            if length > MAX_BODY:
                self.send_response(413)
                self.end_headers()
                return
            body = self.rfile.read(length).decode("utf-8", "replace")
            if self.path != "/api/v1/heartbeat":
                self.send_response(404)
                self.end_headers()
                return
            with lock:
                count["n"] += 1
                n = count["n"]
                now = datetime.now(timezone.utc).strftime("%H:%M:%S")
                print(f"HB {n} {now} {self.headers.get('Content-Type')} {body}", flush=True)
            self.send_response(204)
            self.end_headers()
            if limit is not None and n >= limit:
                threading.Thread(target=self.server.shutdown, daemon=True).start()

        def do_GET(self):  # noqa: N802
            self.send_response(404)
            self.end_headers()

        def log_message(self, fmt, *a):  # silence the default access log
            pass

    server = HTTPServer(("127.0.0.1", port), Handler)
    print(f"heartbeat_stub listening on 127.0.0.1:{port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())

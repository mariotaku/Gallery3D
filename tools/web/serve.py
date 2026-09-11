#!/usr/bin/env python3
"""Serves the web build with the two headers pthreads need.

The loader threads, the decode pool and the network pool are real threads in
the wasm build, and threads there are built on SharedArrayBuffer. A browser
only hands a page SharedArrayBuffer when the page is cross-origin isolated,
which means these two response headers. Without them the module fails to start
and the console blames SharedArrayBuffer rather than the headers.

Python's own http.server sends neither, so this adds them.

    python tools/web/serve.py build-web
"""

import argparse
import functools
import http.server
import os
import socketserver


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # A dev server should never be the reason something looks cached.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        # One line per request, without the timestamp noise.
        print("  %s" % (fmt % args))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", default="build-web",
                        help="what to serve (default: build-web)")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    if not os.path.isdir(args.directory):
        raise SystemExit("no such directory: %s" % args.directory)

    handler = functools.partial(Handler, directory=args.directory)
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("127.0.0.1", args.port), handler) as server:
        print("Serving %s at http://127.0.0.1:%d/ (cross-origin isolated)"
              % (os.path.abspath(args.directory), args.port))
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Serves the web build with caching turned off.

The build uses no threads, so it needs no cross-origin isolation headers and
runs from any static host. This server only adds Cache-Control: no-store.

    python tools/web/serve.py build-web
    python tools/web/serve.py build-web --host 0.0.0.0
"""

import argparse
import functools
import http.server
import os
import socketserver


def local_addresses():
    """The addresses another machine could reach this one on."""
    import socket
    found = []
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        found.append(probe.getsockname()[0])
        probe.close()
    except Exception:
        pass
    return found or ["127.0.0.1"]


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
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
    parser.add_argument("--host", default="127.0.0.1",
                        help="0.0.0.0 to let other machines on the network reach it")
    args = parser.parse_args()

    if not os.path.isdir(args.directory):
        raise SystemExit("no such directory: %s" % args.directory)

    handler = functools.partial(Handler, directory=args.directory)
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer((args.host, args.port), handler) as server:
        print("Serving %s on %s:%d" % (os.path.abspath(args.directory), args.host, args.port))
        if args.host == "0.0.0.0":
            for address in local_addresses():
                print("  http://%s:%d/" % (address, args.port))
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()

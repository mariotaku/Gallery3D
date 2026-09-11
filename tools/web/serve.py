#!/usr/bin/env python3
"""Serves the web build.

The headers here are cross-origin isolation, which the build needed back when
it used threads. It does not any more - fetching and decoding both go through
the browser and answer in callbacks - so the page will run from any static host
without them. They are kept because they cost nothing and would be needed again
the moment anything here wants shared memory.

    python tools/web/serve.py build-web
    python tools/web/serve.py build-web --host 0.0.0.0

One thing that does matter, and is not about headers: the museum's api refuses
any request whose Origin says localhost or 127.0.0.1, so a build served to
yourself on this machine will load its wall and none of its pictures. Serving on
--host 0.0.0.0 and opening the LAN address instead is enough to satisfy it.
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
        # No cross-origin isolation. It was needed when the build used threads,
        # and it actively breaks things now: require-corp refuses any
        # cross-origin response that does not carry a
        # Cross-Origin-Resource-Policy header, and the museum's image server
        # sends Access-Control-Allow-Origin but not that one. With the threads
        # gone there is nothing left that wants shared memory, so this goes.
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

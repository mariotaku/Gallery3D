#!/usr/bin/env python3
"""Runs the web build in headless Chrome for real, and screenshots it.

Chrome's own --screenshot fires as soon as the page loads, and its
--virtual-time-budget fast forwards the clock without waiting for decodes, so
neither of them ever sees the wall: photos decode in real seconds. This drives
the browser over the DevTools protocol instead, waits an actual wall clock
interval, and then captures.

    python tools/web/capture.py --seconds 25 --out shot.png

Needs a server already running, which tools/web/serve.py provides.
"""

import argparse
import base64
import json
import os
import shutil
import subprocess
import tempfile
import time
import urllib.request

import websocket  # pip install websocket-client

CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"


def wait_for_target(port, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            raw = urllib.request.urlopen("http://127.0.0.1:%d/json" % port, timeout=2).read()
            for target in json.loads(raw):
                if target.get("type") == "page" and target.get("webSocketDebuggerUrl"):
                    return target["webSocketDebuggerUrl"]
        except Exception:
            pass
        time.sleep(0.3)
    raise SystemExit("Chrome never offered a debuggable page")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8099/index.html")
    parser.add_argument("--seconds", type=float, default=25.0,
                        help="how long to let it run before capturing")
    parser.add_argument("--out", default="web-shot.png")
    parser.add_argument("--port", type=int, default=9222)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=800)
    args = parser.parse_args()

    profile = tempfile.mkdtemp(prefix="gallery3d-chrome-")
    chrome = subprocess.Popen([
        CHROME,
        "--headless=new",
        "--disable-gpu",
        "--enable-unsafe-swiftshader",
        "--remote-debugging-port=%d" % args.port,
        # Chrome refuses a DevTools websocket from an origin it was not told
        # about, and the client here connects from the debugging port itself.
        "--remote-allow-origins=*",
        "--user-data-dir=" + profile,
        "--window-size=%d,%d" % (args.width, args.height),
        "--no-first-run",
        "--disable-extensions",
        args.url,
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        ws = websocket.create_connection(wait_for_target(args.port), timeout=60)
        message_id = [0]

        def send(method, params=None):
            message_id[0] += 1
            ws.send(json.dumps({"id": message_id[0], "method": method,
                                "params": params or {}}))
            while True:
                reply = json.loads(ws.recv())
                if reply.get("id") == message_id[0]:
                    return reply
                if reply.get("method") == "Runtime.consoleAPICalled":
                    for arg in reply["params"].get("args", []):
                        if "value" in arg:
                            print("  console:", arg["value"])

        send("Runtime.enable")
        print("Running for %.0f real seconds..." % args.seconds)
        deadline = time.time() + args.seconds
        while time.time() < deadline:
            # Pump the socket so console lines arrive as they happen.
            ws.settimeout(0.5)
            try:
                event = json.loads(ws.recv())
                if event.get("method") == "Runtime.consoleAPICalled":
                    for arg in event["params"].get("args", []):
                        if "value" in arg:
                            print("  console:", arg["value"])
            except Exception:
                pass
        ws.settimeout(60)

        shot = send("Page.captureScreenshot", {"format": "png"})
        data = shot.get("result", {}).get("data")
        if not data:
            raise SystemExit("no screenshot came back: %s" % shot)
        with open(args.out, "wb") as handle:
            handle.write(base64.b64decode(data))
        print("Wrote %s" % args.out)
        ws.close()
    finally:
        chrome.terminate()
        try:
            chrome.wait(timeout=10)
        except Exception:
            chrome.kill()
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    main()

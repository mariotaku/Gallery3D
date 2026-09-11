#!/usr/bin/env python3
"""Opens the web build and reports why its requests failed, if they did.

Console output alone does not say why a fetch came back with nothing: the
browser reports a blocked request to the network log, not to the page. This
enables the DevTools Network domain as well, so a refusal shows up with the
reason the browser had for it - a CORS header, a policy, a bad origin - rather
than as a bare HTTP 0 in the app's own logging.

    python tools/web/diagnose.py --url http://192.168.1.5:8099/index.html
"""

import argparse
import json
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
    parser.add_argument("--seconds", type=float, default=25.0)
    parser.add_argument("--port", type=int, default=9223)
    parser.add_argument("--headful", action="store_true",
                        help="show the window, for watching it rather than reading about it")
    args = parser.parse_args()

    profile = tempfile.mkdtemp(prefix="gallery3d-diag-")
    command = [
        CHROME,
        "--remote-debugging-port=%d" % args.port,
        "--remote-allow-origins=*",
        "--user-data-dir=" + profile,
        "--window-size=1280,800",
        "--no-first-run",
        "--disable-extensions",
    ]
    if not args.headful:
        command += ["--headless=new", "--disable-gpu", "--enable-unsafe-swiftshader"]
    # Start on a blank page and navigate once the listeners are attached.
    # Passing the url here instead means the page has already loaded, and its
    # first requests, before anything is watching.
    command.append("about:blank")
    chrome = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    requests = {}
    failures = []
    statuses = {}
    try:
        ws = websocket.create_connection(wait_for_target(args.port), timeout=60)
        for index, method in enumerate(("Runtime.enable", "Network.enable", "Log.enable", "Page.enable")):
            ws.send(json.dumps({"id": index + 1, "method": method, "params": {}}))
        time.sleep(0.5)
        ws.send(json.dumps({"id": 90, "method": "Page.navigate", "params": {"url": args.url}}))
        print("navigated to %s" % args.url)

        deadline = time.time() + args.seconds
        ws.settimeout(0.5)
        while time.time() < deadline:
            try:
                event = json.loads(ws.recv())
            except Exception:
                continue
            method = event.get("method")
            params = event.get("params", {})

            if method == "Runtime.consoleAPICalled":
                for arg in params.get("args", []):
                    if "value" in arg:
                        print("  console:", arg["value"])
            elif method == "Runtime.exceptionThrown":
                detail = params.get("exceptionDetails", {})
                print("  EXCEPTION:", detail.get("text"),
                      (detail.get("exception") or {}).get("description", ""))
            elif method == "Network.requestWillBeSent":
                requests[params["requestId"]] = params["request"]["url"]
            elif method == "Network.responseReceived":
                url = requests.get(params["requestId"], "?")
                statuses.setdefault(params["response"]["status"], 0)
                statuses[params["response"]["status"]] += 1
                if params["response"]["status"] != 200:
                    print("  HTTP %s  %s" % (params["response"]["status"], url[:110]))
            elif method == "Network.loadingFailed":
                url = requests.get(params["requestId"], "?")
                failures.append((params.get("blockedReason"), params.get("errorText"), url))

        print()
        print("=== response codes ===")
        for status, count in sorted(statuses.items()):
            print("  %s: %d" % (status, count))
        print("=== failed requests: %d ===" % len(failures))
        seen = set()
        for blocked, error, url in failures:
            key = (blocked, error)
            if key in seen:
                continue
            seen.add(key)
            print("  blockedReason=%s errorText=%s" % (blocked, error))
            print("    e.g. %s" % url[:110])
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

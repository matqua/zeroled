#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Minimal WLED JSON API mock for testing ZeroLED without touching real lights.

    python3 tools/mock_wled.py [--host 127.0.0.1] [--port 80] [--name "Mock Strip"]

Implements GET /json/si, /json/state, /json/info, /json/eff, /json/nodes and POST /json/state
with the subset of semantics the app relies on (on/bri/seg object or array/udpn). Every POST
body is printed so tests can assert on what the app sent.
"""

import argparse
import copy
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

EFFECTS = ["Solid", "Blink", "Breathe", "Wipe", "Wipe Random", "Random Colors", "Sweep",
           "Dynamic", "Colorloop", "Rainbow", "Scan", "RSVD", "Fade", "Theater", "Chase",
           "Aurora", "Fire 2012", "Ripple", "Twinkle", "Meteor"]


def make_state():
    return {
        "on": True, "bri": 128, "transition": 7, "ps": -1, "pl": -1, "mainseg": 0,
        "udpn": {"send": False, "recv": True, "sgrp": 1, "rgrp": 1},
        "seg": [{"id": 0, "start": 0, "stop": 60, "len": 60, "on": True, "bri": 255,
                 "col": [[255, 160, 0], [0, 0, 0], [0, 0, 0]], "fx": 0, "sx": 128, "ix": 128,
                 "pal": 0, "sel": True}],
    }


def make_info(name, ip):
    return {
        "ver": "16.0.1-mock", "vid": 2606300, "name": name, "ip": ip, "mac": "02005e000001",
        "leds": {"count": 60, "pwr": 380, "fps": 42, "maxpwr": 850, "maxseg": 32},
        "live": False, "lm": "", "lip": "", "ws": 0, "fxcount": len(EFFECTS), "palcount": 71,
        "wifi": {"bssid": "00:00:00:00:00:00", "rssi": -55, "signal": 90, "channel": 6},
        "fs": {"u": 12, "t": 983}, "ndc": 0, "arch": "esp32", "core": "mock", "freeheap": 150000,
        "uptime": 3725, "brand": "WLED", "product": "Mock",
    }


class Mock:
    def __init__(self, name, ip):
        self.state = make_state()
        self.info = make_info(name, ip)

    def apply(self, patch):
        if "bri" in patch:
            self.state["bri"] = max(0, min(255, int(patch["bri"])))
        on = patch.get("on")
        if on == "t":
            self.state["on"] = not self.state["on"]
        elif on is not None:
            self.state["on"] = bool(on)
        elif "bri" in patch:
            self.state["on"] = self.state["bri"] > 0
        udpn = patch.get("udpn", {})
        for key in ("send", "sgrp", "rgrp"):
            if key in udpn:
                self.state["udpn"][key] = udpn[key]
        # 16.x semantics: recv is derived from the receive-group mask
        self.state["udpn"]["recv"] = self.state["udpn"]["rgrp"] != 0
        seg = patch.get("seg")
        targets = []
        if isinstance(seg, dict):
            if "id" in seg:
                targets = [(s, seg) for s in self.state["seg"] if s["id"] == seg["id"]]
            else:
                targets = [(s, seg) for s in self.state["seg"] if s.get("sel")]
        elif isinstance(seg, list):
            for entry in seg:
                sid = entry.get("id", self.state["mainseg"])
                targets += [(s, entry) for s in self.state["seg"] if s["id"] == sid]
        for target, fields in targets:
            for key in ("fx", "sx", "ix", "pal"):
                if key in fields:
                    target[key] = int(fields[key])
            if "col" in fields:
                for i, col in enumerate(fields["col"][:3]):
                    target["col"][i] = list(col)[:3]


def handler_for(mock):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *args):  # quiet default logging
            pass

        def send_json(self, payload, status=200):
            body = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = self.path.split("?")[0].rstrip("/")
            if path in ("/json/si", "/json"):
                self.send_json({"state": mock.state, "info": mock.info})
            elif path == "/json/state":
                self.send_json(mock.state)
            elif path == "/json/info":
                self.send_json(mock.info)
            elif path == "/json/eff":
                self.send_json(EFFECTS)
            elif path == "/json/nodes":
                self.send_json({"nodes": []})
            else:
                self.send_json({"error": 3}, 404)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            raw = self.rfile.read(length).decode()
            print("POST", self.path, raw, flush=True)
            try:
                patch = json.loads(raw)
            except json.JSONDecodeError:
                self.send_json({"error": 9}, 400)
                return
            mock.apply(patch)
            if patch.get("v"):
                self.send_json(copy.deepcopy(mock.state))
            else:
                self.send_json({"success": True})

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--name", default="Mock Strip")
    args = parser.parse_args()
    mock = Mock(args.name, args.host)
    server = ThreadingHTTPServer((args.host, args.port), handler_for(mock))
    print(f"mock WLED '{args.name}' on http://{args.host}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()

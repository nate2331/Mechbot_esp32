#!/usr/bin/env python3
"""CLI for the local MechBot bridge."""
import argparse, json, urllib.request

BASE = "http://127.0.0.1:8765"
def request(path, payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(BASE + path, data=data, headers={"Content-Type": "application/json"})
    print(json.dumps(json.load(urllib.request.urlopen(req, timeout=3)), indent=2))
def main():
    p=argparse.ArgumentParser(); sub=p.add_subparsers(dest="command", required=True)
    sub.add_parser("status"); sub.add_parser("settings"); sub.add_parser("stop"); sub.add_parser("save"); sub.add_parser("reset")
    s=sub.add_parser("set"); s.add_argument("key"); s.add_argument("value", type=float)
    a=p.parse_args()
    if a.command=="status": request("/api/status")
    elif a.command=="settings": request("/api/settings")
    elif a.command=="stop": request("/api/stop", {})
    elif a.command=="save": request("/api/settings/save", {})
    elif a.command=="reset": request("/api/settings/reset", {})
    else: request("/api/settings", {"key":a.key,"value":a.value})
if __name__ == "__main__": main()

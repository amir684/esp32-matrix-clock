# Serves web/index.html with a fake clock behind it, to work on the page without the hardware.
#   python tools/webtest/mock_server.py            then open http://127.0.0.1:8765/
#   http://127.0.0.1:8765/?ble&fakeble              the Bluetooth app, talking to fake_ble.js
#                                                   (same packet protocol as src/ble.cpp)
# The live preview shows a real frame when tools/sim/run.ps1 has been run.
import json, os, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PAGE = os.path.join(ROOT, "web", "index.html")
FRAMES = os.path.join(ROOT, "tools", "sim", "out", "frames.txt")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
# a second argument starts that game right away (0 snake .. 3 tetris), to look at the controller
GAME = {"game": int(sys.argv[2]) if len(sys.argv) > 2 else -1, "gameScore": 0, "gameOver": False,
        "games": [{"name": "סנייק", "best": 34}, {"name": "דינוזאור", "best": 12}, {"name": "פלאפי", "best": 0},
                  {"name": "טטריס", "best": 7}]}

S = {"width": 64, "tileW": 32, "corner": 0, "columns": True, "serpentine": True, "chainRtl": False, "altRotated": False,
     "mirrorX": False, "flipY": False, "colorOrder": 0, "maxMilliamps": 2000, "face": 3, "hour24": True,
     "leadingZero": True, "showSeconds": True, "blinkColon": True, "colorMode": 0, "color1": "#FFB050",
     "color2": "#3080FF", "colonColor": "", "brightness": 60, "tickerEvery": 60, "scrollMs": 30,
     "items": {"weekday": True, "hebdate": True, "gregdate": True, "events": True, "parasha": True, "shabbat": True,
               "omer": True, "temp": True, "sun": False, "message": True},
     "gregLong": True, "dayAtSunset": True, "tickerColors": 0, "tickerColor": "#40C0FF", "message": "",
     "messageColor": "#FF40A0", "tempOffset": -1.5, "city": "ירושלים", "lat": 31.778, "lon": 35.22, "israel": True,
     "candleMinutes": 40, "havdalahDeg": 8.5, "parashaScheme": 0, "nightOn": True, "nightBySun": False,
     "nightFrom": 1380, "nightTo": 360, "nightBrightness": 4, "nightColorOn": True, "nightColor": "#FF2000",
     "nightTicker": False, "nightOff": False, "effect": 0, "effectSeconds": 20, "partyBrightness": 160,
     "partySpeed": 5, "partyClock": 1, "partyMinutes": 0, "partyText": "PARTY!", "btnActiveLow": False,
     "wifiMode": 0, "wifiSsid": "", "hostname": "matrixclock", "tz": "IST-2IDT,M3.4.4/26,M10.5.0",
     "ntpServer": "pool.ntp.org", "bleOn": True, "hasWifiPass": False, "hasApPass": False}
ST = {"epoch": 1795005296, "timeValid": True, "time": "14:34:56", "hebDate": "ח' כסלו תשפ\"ז", "weekday": "רביעי",
      "gregDate": "18.11.2026", "temp": 23.5, "ticker": ["יום רביעי", "ח' כסלו תשפ\"ז", "פרשת ויצא"], "rtc": True,
      "ntp": False, "mode": "clock", "effectNow": 0, "power": True, "night": False, "bright": 60, "wifi": False,
      "ip": "192.168.4.1", "ssid": "", "rssi": 0, "ap": "MATRIX-CLOCK", "ble": False, "version": "1.1.0",
      "heap": 150000, "uptime": 3725,
      "effects": ["קשת", "פלזמה", "אש", "גשם דיגיטלי", "קונפטי", "לבה", "אוקיינוס", "יער", "מסיבה", "כוכבים",
                  "סורק", "אקולייזר", "מטאורים", "פעימה", "אדוות", "זיקוקים", "טקסט"]}


def frame():
    if os.path.exists(FRAMES):
        lines = open(FRAMES).read().splitlines()
        if "FRAME 64_info 64" in lines:
            k = lines.index("FRAME 64_info 64")
            return {"w": 64, "px": "".join(lines[k + 1:k + 9])}
    return {"w": 64, "px": "000000" * 512}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write((fmt % args) + "\n")

    def send(self, body, ctype="application/json", code=200):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            page = open(PAGE, "rb").read()
            if "fakeble" in self.path:
                fake = open(os.path.join(HERE, "fake_ble.js"), "rb").read()
                page = page.replace(b"<script>", b"<script>" + fake + b"</script>\n<script>", 1)
            self.send(page, "text/html; charset=utf-8")
        elif path in ("/api/settings", "/api/backup"):
            self.send(S)
        elif path == "/api/status":
            self.send(ST)
        elif path == "/api/game":
            self.send(GAME)
        elif path == "/api/frame":
            self.send(frame())
        elif path == "/api/scan":
            self.send([{"ssid": "Home", "rssi": -51, "secure": True}, {"ssid": "Guests", "rssi": -70, "secure": False}])
        else:
            self.send({"error": "not found"}, code=404)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(n) or b"{}")
        sys.stderr.write("POST %s %s\n" % (self.path, json.dumps(body, ensure_ascii=False)[:200]))
        if self.path == "/api/settings":
            items = body.pop("items", None)
            if items:
                S["items"].update(items)
            S.update(body)
            self.send(S)
        elif self.path == "/api/action" and body.get("do") in ("party", "clock"):
            ST["mode"] = body["do"]
            self.send({"ok": True})
        elif self.path == "/api/action" and body.get("do") == "game":
            GAME.update(game=body.get("game", 0), gameScore=0, gameOver=False)
            self.send({"ok": True})
        elif self.path == "/api/action" and body.get("do") == "gameStop":
            GAME["game"] = -1
            self.send({"ok": True})
        elif self.path == "/api/action" and body.get("do") == "key":
            if body.get("s") and not body.get("r"):
                GAME["gameScore"] += 1
            self.send(GAME)
        else:
            self.send({"ok": True})


print("http://127.0.0.1:%d/   (Bluetooth app: /?ble&fakeble)" % PORT)
HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()

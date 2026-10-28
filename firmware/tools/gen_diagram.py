#!/usr/bin/env python3
"""Generate firmware/diagram.json (the Wokwi circuit) from board_pins.h.

Why a generator: the GPIO numbers are read from board_pins.h, so the
simulated wiring can never disagree with the firmware. Every wire gets an
explicit orthogonal route, and the script checks that no two wires overlap
and no wire runs through a part.

    python tools/gen_diagram.py              # write diagram.json
    python tools/gen_diagram.py --preview p.png   # also render a preview (needs Pillow)

Coordinates are Wokwi pixels. Part pin offsets come from the wokwi-elements /
wokwi-boards sources (pinInfo / board.json).
"""
import argparse
import json
import re
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
PINS_H = FIRMWARE / "components/common/include/board_pins.h"
MM = 3.7795  # Wokwi px per mm

# --------------------------------------------------------------------------
# Part geometry: size (w, h) and pin offsets (x, y) relative to top-left
# --------------------------------------------------------------------------
GEOM = {
    "board-esp32-s3-devkitc-1": (25.527 * MM, 70.057 * MM, {}),   # pins added below
    "wokwi-lcd2004":           (94.05 * MM, 47.5 * MM,
                                {"GND": (4, 32), "VCC": (4, 41.5), "SDA": (4, 51), "SCL": (4, 60.5)}),
    "wokwi-ds1307":            (25.8 * MM, 22.212 * MM,
                                {"GND": (9.5, 15), "5V": (9.5, 25), "SDA": (9.5, 34.5), "SCL": (9.5, 44), "SQW": (9.5, 54)}),
    "wokwi-microsd-card":      (21.6 * MM, 20.4 * MM,
                                {"CD": (76.734, 9.374), "DO": (76.734, 18.862), "GND": (76.734, 28.463),
                                 "SCK": (76.734, 38.178), "VCC": (76.734, 47.628), "DI": (76.734, 57.456),
                                 "CS": (76.734, 66.906)}),
    "wokwi-membrane-keypad":   (70.336 * MM, 91 * MM,
                                {"R1": (100, 338), "R2": (110, 338), "R3": (119.5, 338), "R4": (129, 338),
                                 "C1": (138.5, 338), "C2": (148, 338), "C3": (157.75, 338), "C4": (167.5, 338)}),
    "wokwi-servo":             (45 * MM, 31.63 * MM, {"GND": (0, 50), "V+": (0, 59.5), "PWM": (0, 69)}),
    "wokwi-buzzer":            (17 * MM, 20 * MM, {"1": (27, 84), "2": (37, 84)}),
    "wokwi-led":               (40, 50, {"A": (25, 42), "C": (15, 42)}),
    "wokwi-resistor":          (15.645 * MM, 3 * MM, {"1": (0, 5.65), "2": (58.8, 5.65)}),
    "wokwi-pushbutton":        (17.802 * MM, 12 * MM,
                                {"1.l": (0, 13), "2.l": (0, 32), "1.r": (67, 13), "2.r": (67, 32)}),
    "wokwi-slide-switch":      (8.5 * MM, 9.23 * MM, {"1": (6.5, 34), "2": (16, 34), "3": (25.5, 34)}),
    "wokwi-pir-motion-sensor": (24 * MM, 24.448 * MM, {"VCC": (36.178, 92), "OUT": (45.9175, 92), "GND": (55.6415, 92)}),
    "wokwi-photoresistor-sensor": (45.95 * MM, 16.267 * MM,
                                   {"VCC": (172, 16), "GND": (172, 26), "DO": (172, 35.8), "AO": (172, 45.5)}),
    "wokwi-text":              (0, 0, {}),
}

_LEFT = ["3V3.1", "3V3.2", "RST", "4", "5", "6", "7", "15", "16", "17", "18", "8", "3", "46",
         "9", "10", "11", "12", "13", "14", "5V", "GND.1"]
_RIGHT = ["GND.2", "TX", "RX", "1", "2", "42", "41", "40", "39", "38", "37", "36", "35", "0",
          "45", "48", "47", "21", "20", "19", "GND.3", "GND.4"]
for i, name in enumerate(_LEFT):
    GEOM["board-esp32-s3-devkitc-1"][2][name] = (1.343 * MM, (7.667 + 2.54 * i) * MM)
for i, name in enumerate(_RIGHT):
    GEOM["board-esp32-s3-devkitc-1"][2][name] = (24.19 * MM, (7.667 + 2.54 * i) * MM)


# --------------------------------------------------------------------------
# Diagram model
# --------------------------------------------------------------------------
class Diagram:
    def __init__(self):
        self.parts = {}          # id -> dict(type, left, top, attrs)
        self.wires = []          # (from, to, color, [(x, y) waypoints])

    def part(self, pid, ptype, left, top, **attrs):
        self.parts[pid] = {"type": ptype, "left": round(left, 2), "top": round(top, 2),
                           "attrs": {k: str(v) for k, v in attrs.items()}}
        return pid

    def label(self, pid, left, top, text):
        self.part(pid, "wokwi-text", left, top, text=text)

    def pin(self, ref):
        pid, name = ref.split(":", 1)
        p = self.parts[pid]
        x, y = GEOM[p["type"]][2][name]
        return (p["left"] + x, p["top"] + y)

    def wire(self, a, b, color, *moves):
        """Route a -> b. moves: ('h', x) go horizontally to absolute x,
        ('v', y) go vertically to absolute y. The last leg to b is added
        automatically and must be a straight line."""
        x, y = self.pin(a)
        pts = [(x, y)]
        for axis, val in moves:
            x, y = (val, y) if axis == "h" else (x, val)
            pts.append((x, y))
        bx, by = self.pin(b)
        if abs(bx - x) > 1 and abs(by - y) > 1:     # <1 px jogs are invisible
            raise ValueError(f"{a} -> {b}: last leg is not straight ({x},{y}) -> ({bx},{by})")
        pts.append((bx, by))
        self.wires.append((a, b, color, pts))

    # ---------------- Output ----------------
    def to_json(self):
        parts = [{"type": p["type"], "id": pid, "top": p["top"], "left": p["left"], "attrs": p["attrs"]}
                 for pid, p in self.parts.items()]
        conns = [["esp:TX", "$serialMonitor:RX", "", []], ["esp:RX", "$serialMonitor:TX", "", []]]
        for a, b, color, pts in self.wires:
            instr = []
            for (x0, y0), (x1, y1) in zip(pts, pts[1:-1]):     # final leg is implicit
                if abs(x1 - x0) > 0.01:
                    instr.append(f"h{round(x1 - x0, 2):g}")
                if abs(y1 - y0) > 0.01:
                    instr.append(f"v{round(y1 - y0, 2):g}")
            conns.append([a, b, color, instr])
        return {"version": 1, "author": "CoCIS Smart Control group", "editor": "wokwi",
                "parts": parts, "connections": conns, "dependencies": {}}

    # ---------------- Checks ----------------
    def segments(self):
        for i, (a, b, color, pts) in enumerate(self.wires):
            for p, q in zip(pts, pts[1:]):
                if p != q:
                    yield i, p, q

    def nets(self):
        """Wire index -> net id (wires sharing a pin are the same net)."""
        parent = {}
        find = lambda k: k if parent.setdefault(k, k) == k else find(parent[k])
        for a, b, _, _ in self.wires:
            parent[find(a)] = find(b)
        return [find(a) for a, _, _, _ in self.wires]

    def check(self):
        """Overlaps between different nets and wires through parts are errors;
        crossings are only counted (power rails must cross some signals)."""
        problems, crossings = [], 0
        segs, net = list(self.segments()), self.nets()
        name = lambda i: f"{self.wires[i][0]}->{self.wires[i][1]}"
        for n, (i, p, q) in enumerate(segs):
            for (j, r, s) in segs[n + 1:]:
                if net[i] == net[j]:
                    continue
                kind = _relation(p, q, r, s)
                if kind == "overlap":
                    problems.append(f"overlap: {name(i)} / {name(j)}")
                elif kind == "cross":
                    crossings += 1
        for i, p, q in segs:
            for pid, part in self.parts.items():
                if part["type"] != "wokwi-text" and _through_body(p, q, part):
                    problems.append(f"through {pid}: {name(i)}")
        return problems, crossings


def _relation(p, q, r, s):
    """'overlap' if collinear segments share a stretch, 'cross' if they
    intersect at an interior point, else None."""
    (x1, y1), (x2, y2), (x3, y3), (x4, y4) = p, q, r, s
    h1, h2 = abs(y1 - y2) < 0.01, abs(y3 - y4) < 0.01
    if h1 and h2 and abs(y1 - y3) < 1.5:
        lo, hi = max(min(x1, x2), min(x3, x4)), min(max(x1, x2), max(x3, x4))
        return "overlap" if hi - lo > 0.5 else None
    if not h1 and not h2 and abs(x1 - x3) < 1.5:
        lo, hi = max(min(y1, y2), min(y3, y4)), min(max(y1, y2), max(y3, y4))
        return "overlap" if hi - lo > 0.5 else None
    if h1 != h2:
        (hx1, hx2, hy), (vx, vy1, vy2) = ((x1, x2, y1), (x3, y3, y4)) if h1 else ((x3, x4, y3), (x1, y1, y2))
        if min(hx1, hx2) + 0.5 < vx < max(hx1, hx2) - 0.5 and min(vy1, vy2) + 0.5 < hy < max(vy1, vy2) - 0.5:
            return "cross"
    return None


def _through_body(p, q, part):
    w, h, _ = GEOM[part["type"]]
    inset = 11                                  # pins sit on/near the edge (RTC: 9.5 px in)
    x0, y0 = part["left"] + inset, part["top"] + inset
    x1, y1 = part["left"] + w - inset, part["top"] + h - inset
    if x1 <= x0 or y1 <= y0:
        return False
    (ax, ay), (bx, by) = p, q
    if abs(ay - by) < 0.01:
        return y0 < ay < y1 and min(ax, bx) < x1 and max(ax, bx) > x0
    return x0 < ax < x1 and min(ay, by) < y1 and max(ay, by) > y0


# --------------------------------------------------------------------------
# board_pins.h -> {"PIN_ROOM1_LDR": "4", ...}
# --------------------------------------------------------------------------
def read_pins():
    text = PINS_H.read_text()
    return {k: v for k, v in re.findall(r"#define\s+(PIN_\w+)\s+GPIO_NUM_(\d+)", text)}


# --------------------------------------------------------------------------
# Layout
#
# Rules that keep the signal wiring planar (no signal ever crosses another):
#  * Parts sit on the side of the board their pins face, in the same order
#    as the header pins they use.
#  * Each signal leaves the header horizontally into its own vertical lane.
#    Wires that fan UP use lanes closer to the board the higher their pin;
#    wires that fan DOWN use lanes closer to the board the lower their pin.
#  * Power (red) and ground (black) run on rails/corridors with straight
#    taps; they are the only wires allowed to cross signals.
# --------------------------------------------------------------------------
SIG, PWR, GND, I2C, SPI, KEY = "green", "red", "black", "blue", "orange", "purple"
LANE = 8                        # spacing between parallel wires


def build():
    P = read_pins()
    d = Diagram()
    d.part("esp", "board-esp32-s3-devkitc-1", 0, 0)
    pin = lambda name: f"esp:{P[name]}"
    px = lambda ref: d.pin(ref)[0]
    py = lambda ref: d.pin(ref)[1]
    XL, XR = px("esp:4"), px("esp:1")            # left / right header x

    def lanes_from(origin, step, names):
        return {n: origin + step * i for i, n in enumerate(names)}

    # ---------------------------------------------------------------- top corridors
    # Room power arrives over the top of the board: 3V3 from the left header,
    # GND from the right header's GND.2 (which runs up left of the keypad).
    Y_TOP_GND, Y_TOP_V = -470, -462

    d.label("title", -560, Y_TOP_GND - 60,
            "CoCIS Smart Control & Monitoring - ESP32-S3 (all terminals on one board)")

    # ================================================================ LEFT: rooms
    up = ["PIN_ROOM1_LDR", "PIN_ROOM1_PIR", "PIN_ROOM1_LIGHT",
          "PIN_ROOM2_LDR", "PIN_ROOM2_PIR", "PIN_ROOM2_LIGHT"]
    up_lane = lanes_from(-14, -LANE, up)          # higher pin -> closer lane
    X_RAIL_V, X_RAIL_G = -620, -628               # far-left room supply rails

    def room(r, top, prev=None):
        """One lecture room row. prev = (ldr, pir, tap_y) of the room above,
        whose supply this room chains from."""
        ldr, pir, led, res = (f"room{r}_{s}" for s in ("ldr", "pir", "lamp", "lamp_r"))
        w_ldr = GEOM["wokwi-photoresistor-sensor"][0]
        x_ldr = -100 - w_ldr
        d.part(ldr, "wokwi-photoresistor-sensor", x_ldr, top + 10, lux=100)
        x_pir = x_ldr - 40 - GEOM["wokwi-pir-motion-sensor"][0]
        d.part(pir, "wokwi-pir-motion-sensor", x_pir, top, delayTime=3)
        y_pins = top + 92
        x_led = x_pir - 120
        d.part(led, "wokwi-led", x_led, y_pins - 42, color="white")
        y_lamp = y_pins + 22                      # lamp resistor row
        d.part(res, "wokwi-resistor", px(f"{led}:A"), y_lamp - 5.65, value=220)

        # --- signals (planar fan into the up-lanes)
        L = lambda n: up_lane[f"PIN_ROOM{r}_{n}"]
        d.wire(pin(f"PIN_ROOM{r}_LDR"), f"{ldr}:AO", SIG, ("h", L("LDR")), ("v", py(f"{ldr}:AO")))
        y_out = y_pins + 12
        d.wire(pin(f"PIN_ROOM{r}_PIR"), f"{pir}:OUT", SIG, ("h", L("PIR")), ("v", y_out), ("h", px(f"{pir}:OUT")))
        d.wire(pin(f"PIN_ROOM{r}_LIGHT"), f"{res}:2", SIG, ("h", L("LIGHT")), ("v", y_lamp))
        d.wire(f"{res}:1", f"{led}:A", SIG)

        # --- supply. Room 1: LDR from the top corridors, PIR + lamp from the
        # far-left rails. Room 2 chains from room 1 along the same paths.
        x_vcc, x_gnd = px(f"{ldr}:VCC") + 10, px(f"{ldr}:VCC") + 18
        tap_v, tap_g = y_lamp + 16, y_lamp + 24
        if prev is None:
            d.wire(f"{ldr}:VCC", f"{pir}:VCC", PWR, ("h", x_vcc), ("v", Y_TOP_V), ("h", X_RAIL_V),
                   ("v", tap_v), ("h", px(f"{pir}:VCC")))
            d.wire(f"{ldr}:GND", f"{pir}:GND", GND, ("h", x_gnd), ("v", Y_TOP_GND), ("h", X_RAIL_G),
                   ("v", tap_g), ("h", px(f"{pir}:GND")))
        else:
            p_ldr, p_pir, p_tap_v, p_tap_g = prev
            d.wire(f"{ldr}:VCC", f"{p_ldr}:VCC", PWR, ("h", x_vcc), ("v", py(f"{p_ldr}:VCC")))
            d.wire(f"{ldr}:GND", f"{p_ldr}:GND", GND, ("h", x_gnd), ("v", py(f"{p_ldr}:GND")))
            d.wire(f"{p_pir}:VCC", f"{pir}:VCC", PWR, ("v", p_tap_v), ("h", X_RAIL_V), ("v", tap_v),
                   ("h", px(f"{pir}:VCC")))
            d.wire(f"{p_pir}:GND", f"{pir}:GND", GND, ("v", p_tap_g), ("h", X_RAIL_G), ("v", tap_g),
                   ("h", px(f"{pir}:GND")))
        d.wire(f"{led}:C", f"{pir}:GND", GND, ("v", tap_g), ("h", px(f"{pir}:GND")))

        d.label(f"lbl_room{r}", x_led - 10, top - 30,
                f"LECTURE ROOM {r}  -  lamp (PWM)   |   PIR motion   |   LDR daylight")
        return ldr, pir, tap_v, tap_g

    room2 = room(2, -215, room(1, -400))

    # 3V3 / GND.2 feed the corridors (wires to the room-1 LDR supply pins)
    d.wire("esp:3V3.1", "room1_ldr:VCC", PWR, ("h", -6), ("v", Y_TOP_V), ("h", px("room1_ldr:VCC") + 10),
           ("v", py("room1_ldr:VCC")))
    X_GND2 = XR + 8
    d.wire("esp:GND.2", "room1_ldr:GND", GND, ("h", X_GND2), ("v", Y_TOP_GND), ("h", px("room1_ldr:GND") + 18),
           ("v", py("room1_ldr:GND")))

    # ================================================================ LEFT: cards, SD, buzzer
    down = ["PIN_CARD_BTN_1", "PIN_CARD_BTN_2", "PIN_CARD_BTN_3", "PIN_CARD_BTN_4",
            "PIN_SD_MISO", "PIN_SD_SCK", "PIN_SD_MOSI", "PIN_SD_CS", "PIN_BUZZER", "5V", "GND.1"]
    down_lane = lanes_from(-6, -LANE, list(reversed(down)))     # lower pin -> closer lane
    ref = lambda n: pin(n) if n.startswith("PIN_") else f"esp:{n}"

    x_btn = -205
    cards = [("blue", "Alice (student)"), ("green", "Bob (staff)"),
             ("yellow", "Carol (security)"), ("red", "Admin (PIN 1234)")]
    for k, (color, who) in enumerate(cards):
        bid, top = f"card{k + 1}", 110 + k * 64
        d.part(bid, "wokwi-pushbutton", x_btn, top, color=color, key=k + 1, bounce=0)
        n = down[k]
        d.wire(ref(n), f"{bid}:1.r", SIG, ("h", down_lane[n]), ("v", py(f"{bid}:1.r")))
        d.label(f"lbl_card{k + 1}", x_btn - 205, top + 16, f"[{k + 1}] Card: {who}")
    d.label("lbl_cards", x_btn - 205, 80, "RFID reader (simulated):\none button per card")

    sd_left, sd_top = -205, 380
    d.part("sd", "wokwi-microsd-card", sd_left, sd_top)
    for n, sdpin in zip(down[4:8], ["DO", "SCK", "DI", "CS"]):
        d.wire(ref(n), f"sd:{sdpin}", SPI, ("h", down_lane[n]), ("v", py(f"sd:{sdpin}")))
    d.label("lbl_sd", sd_left - 205, sd_top + 30, "microSD card\nhash-chained event log")

    bz_left, bz_top = -200, 500
    d.part("buzzer", "wokwi-buzzer", bz_left, bz_top, volume=0.2)
    d.wire(ref("PIN_BUZZER"), "buzzer:2", SIG, ("h", down_lane["PIN_BUZZER"]),
           ("v", py("buzzer:2") + 14), ("h", px("buzzer:2")))
    d.label("lbl_buzzer", bz_left - 205, bz_top + 30, "Buzzer (entrance)")

    # ---- left bottom corridors: GND.1 (lowest) and 5V
    Y_GND, Y_5V = py("buzzer:2") + 44, py("buzzer:2") + 34
    d.wire("esp:GND.1", "buzzer:1", GND, ("h", down_lane["GND.1"]), ("v", Y_GND), ("h", px("buzzer:1")))
    x_sd_v, x_sd_g = px("sd:VCC") + 8, px("sd:VCC") + 16
    d.wire("sd:VCC", "esp:5V", PWR, ("h", x_sd_v), ("v", Y_5V), ("h", down_lane["5V"]), ("v", py("esp:5V")))
    d.wire("sd:GND", "buzzer:1", GND, ("h", x_sd_g), ("v", Y_GND), ("h", px("buzzer:1")))
    # card grounds: chained on the buttons' left pins, down into the GND corridor
    x_cg = x_btn - 12
    for k in range(3):
        d.wire(f"card{k + 1}:2.l", f"card{k + 2}:2.l", GND, ("h", x_cg), ("v", py(f"card{k + 2}:2.l")))
    d.wire("card4:2.l", "buzzer:1", GND, ("h", x_cg), ("v", Y_GND), ("h", px("buzzer:1")))
    # room rails close the loop at the bottom-left
    d.wire("room2_pir:GND", "buzzer:1", GND, ("v", room2[3]), ("h", X_RAIL_G),
           ("v", Y_GND), ("h", px("buzzer:1")))

    # ================================================================ RIGHT: keypad (fans up)
    kp_left, kp_top = 120, -390
    d.part("keypad", "wokwi-membrane-keypad", kp_left, kp_top)
    keys = ["R1", "R2", "R3", "R4", "C1", "C2", "C3", "C4"]
    for k in keys:
        n = f"PIN_KEYPAD_{k}"
        d.wire(pin(n), f"keypad:{k}", KEY, ("h", px(f"keypad:{k}")))
    d.label("lbl_keypad", kp_left + 280, kp_top + 120,
            "ENTRANCE keypad\nA = admin login\n0-9 PIN, # submit\n* clear, D logout")

    # ================================================================ RIGHT: column of entrance parts
    rdown = ["PIN_I2C_SDA", "PIN_I2C_SCL", "PIN_DOOR_IN_SERVO", "PIN_DOOR_OUT_SERVO",
             "PIN_BREAK_GLASS", "PIN_LED_GRANTED", "PIN_READER_SELECT", "GND.3", "GND.4"]
    rlane = lanes_from(XR + 8, LANE, list(reversed(rdown)))   # lower pin -> closer lane
    X_RV, X_RG = 300, 308                                       # 5V and GND rails
    x_col = 330                                                 # parts' pin edge

    # LCD: SDA/SCL straight across from the header
    lcd_top = py(pin("PIN_I2C_SDA")) - 51
    d.part("lcd", "wokwi-lcd2004", x_col - 4, lcd_top, pins="i2c")
    d.wire(pin("PIN_I2C_SDA"), "lcd:SDA", I2C)
    d.wire(pin("PIN_I2C_SCL"), "lcd:SCL", I2C)
    d.label("lbl_lcd", x_col + 360, lcd_top + 40,
            "LCD 20x4 (I2C 0x27)\nrow 4: lamp %, P = people,\nS = enough sunlight")

    rtc_top = lcd_top + 200
    d.part("rtc", "wokwi-ds1307", x_col - 9.5, rtc_top)
    for s in ("SDA", "SCL"):
        n = f"PIN_I2C_{s}"
        d.wire(pin(n), f"rtc:{s}", I2C, ("h", rlane[n]), ("v", py(f"rtc:{s}")))
    d.label("lbl_rtc", x_col + 100, rtc_top + 30, "RTC DS1307 (I2C 0x68)\nkeeps time offline")

    servo_top = rtc_top + 110
    for i, (sid, n, text) in enumerate((("door_in", "PIN_DOOR_IN_SERVO", "ENTRY door lock (servo)"),
                                        ("door_out", "PIN_DOOR_OUT_SERVO", "EXIT door lock (servo)"))):
        top = servo_top + i * 130
        d.part(sid, "wokwi-servo", x_col, top)
        d.wire(pin(n), f"{sid}:PWM", SIG, ("h", rlane[n]), ("v", py(f"{sid}:PWM")))
        d.label(f"lbl_{sid}", x_col + 180, top + 50, text)

    glass_top = servo_top + 270
    d.part("break_glass", "wokwi-pushbutton", x_col, glass_top, color="red", key="b", bounce=0)
    d.wire(pin("PIN_BREAK_GLASS"), "break_glass:1.l", SIG, ("h", rlane["PIN_BREAK_GLASS"]),
           ("v", py("break_glass:1.l")))
    d.label("lbl_glass", x_col + 80, glass_top + 14, "[b] BREAK GLASS: emergency, all doors open")

    led_top = glass_top + 70
    d.part("led_granted", "wokwi-led", x_col + 60, led_top, color="green", flip=1)
    y_r = py("led_granted:A") + 14
    d.part("led_granted_r", "wokwi-resistor", px("led_granted:A") - 58.8, y_r - 5.65, value=220)
    d.wire(pin("PIN_LED_GRANTED"), "led_granted_r:1", SIG, ("h", rlane["PIN_LED_GRANTED"]), ("v", y_r))
    d.wire("led_granted_r:2", "led_granted:A", SIG)
    d.label("lbl_led", x_col + 110, led_top + 20, "Access granted LED")

    sw_top = led_top + 90
    d.part("reader_select", "wokwi-slide-switch", x_col + 20, sw_top)
    y_sw = py("reader_select:2") + 22
    d.wire(pin("PIN_READER_SELECT"), "reader_select:2", SIG, ("h", rlane["PIN_READER_SELECT"]),
           ("v", y_sw), ("h", px("reader_select:2")))
    d.label("lbl_switch", x_col + 70, sw_top + 6, "Reader select: left = ENTRY (IN), right = EXIT (OUT)")

    # ---- right power: GND rail (X_RG) from GND.4, 5V rail (X_RV) from the left corridor
    Y_RG = py("reader_select:2") + 60
    chain_g = ["lcd:GND", "rtc:GND", "door_in:GND", "door_out:GND", "break_glass:2.l"]
    for a, b in zip(chain_g, chain_g[1:]):
        d.wire(a, b, GND, ("h", X_RG), ("v", py(b)))
    d.wire("break_glass:2.l", "led_granted:C", GND, ("h", X_RG), ("v", y_r + 10), ("h", px("led_granted:C")))
    d.wire("reader_select:1", "led_granted:C", GND, ("v", py("reader_select:1") + 10), ("h", X_RG),
           ("v", y_r + 10), ("h", px("led_granted:C")))
    d.wire("esp:GND.4", "reader_select:1", GND, ("h", rlane["GND.4"]), ("v", Y_RG), ("h", X_RG),
           ("v", py("reader_select:1") + 10), ("h", px("reader_select:1")))

    chain_v = ["lcd:VCC", "rtc:5V", "door_in:V+", "door_out:V+"]
    for a, b in zip(chain_v, chain_v[1:]):
        d.wire(a, b, PWR, ("h", X_RV), ("v", py(b)))
    Y_5V_R = Y_RG + 10
    d.wire("esp:5V", "door_out:V+", PWR, ("h", down_lane["5V"]), ("v", Y_5V_R), ("h", X_RV),
           ("v", py("door_out:V+")))
    return d, P



def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="write a PNG preview (needs Pillow)")
    args = ap.parse_args()
    d, _ = build()
    problems, crossings = d.check()
    out = FIRMWARE / "diagram.json"
    out.write_text(json.dumps(d.to_json(), indent=2) + "\n")
    print(f"{len(d.parts)} parts, {len(d.wires)} wires, {crossings} crossings -> {out.name}")
    for p in problems:
        print("  !", p)
    if args.preview:
        render(d, args.preview)


def render(d, path, scale=1.6):
    from PIL import Image, ImageDraw
    xs = [p["left"] for p in d.parts.values()] + [p["left"] + GEOM[p["type"]][0] for p in d.parts.values()]
    ys = [p["top"] for p in d.parts.values()] + [p["top"] + GEOM[p["type"]][1] for p in d.parts.values()]
    for _, _, _, pts in d.wires:
        xs += [x for x, _ in pts]
        ys += [y for _, y in pts]
    ox, oy = min(xs) - 40, min(ys) - 40
    W, H = int((max(xs) - ox + 260) * scale), int((max(ys) - oy + 40) * scale)
    img = Image.new("RGB", (W, H), (40, 40, 40))
    g = ImageDraw.Draw(img)
    T = lambda x, y: ((x - ox) * scale, (y - oy) * scale)
    for pid, p in d.parts.items():
        w, h, pins = GEOM[p["type"]]
        x, y = T(p["left"], p["top"])
        if p["type"] == "wokwi-text":
            g.text((x, y), p["attrs"]["text"], fill=(230, 230, 140))
            continue
        g.rectangle([x, y, x + w * scale, y + h * scale], outline=(120, 160, 220), width=2)
        g.text((x + 4, y + 4), pid, fill=(200, 200, 200))
        for px, py in pins.values():
            cx, cy = T(p["left"] + px, p["top"] + py)
            g.ellipse([cx - 2, cy - 2, cx + 2, cy + 2], fill=(255, 220, 0))
    rgb = {"green": (60, 200, 60), "red": (230, 50, 50), "black": (10, 10, 10), "blue": (70, 120, 255),
           "orange": (255, 150, 30), "gray": (170, 170, 170), "purple": (180, 90, 220), "yellow": (240, 230, 60)}
    for _, _, color, pts in d.wires:
        c = rgb.get(color, (255, 255, 255))
        if color == "black":
            c = (150, 150, 150)
        g.line([T(*p) for p in pts], fill=c, width=2)
    img.save(path)
    print("preview ->", path)


if __name__ == "__main__":
    main()

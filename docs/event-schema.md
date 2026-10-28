# Event Schema: the shared contract

Every part of the system (the firmware tasks, the SD card log, the serial CLI and the
cloud dashboard) agrees on the definitions below. Change this file first, then the code.

The C mirror of this document is
[`firmware/components/common/include/cocis_types.h`](../firmware/components/common/include/cocis_types.h).

---

## 1. Identities

| Thing | Type | Notes |
|---|---|---|
| Card UID | `uint32_t`, printed as 8 upper-case hex digits | e.g. `01020304`. A 4-byte card UID is read first byte first; a 7-byte UID keeps its last 4 bytes. |
| Node id | `0` = `HUB` (entrance), `1` = `ROOM1`, `2` = `ROOM2` | |
| Role | `STUDENT`, `STAFF`, `SECURITY`, `ADMIN` | `SECURITY`/`ADMIN` are *privileged* (24/7 access) |
| Door side | `IN` (entry reader/door), `OUT` (exit reader/door) | |

### Demo cards (seeded on first boot)

The demo cards are the preset cards of the Wokwi MFRC522 reader. Pick a card in the
reader's control panel (or with its keyboard shortcut), then **Tap** it (presents it for 0.5 s) or **Hold** it.

| Wokwi card (key) | UID | Name | Role | PIN |
|---|---|---|---|---|
| Blue (`b`) | `01020304` | `Alice` | STUDENT | – |
| Green (`g`) | `11223344` | `Bob` | STAFF | – |
| Yellow (`y`) | `55667788` | `Carol` | SECURITY | – |
| Red (`r`) | `AABBCCDD` | `Admin` | ADMIN | `1234` (PINs are exactly 4 digits) |
| Key fob (`k`) | `C0FFEE99` | (unregistered) | – | – |
| NFC tag (`n`), 7-byte UID `04:11:22:33:44:55:66` | `33445566` | (unregistered) | – | – |

Any UID can also be scanned from the CLI: `tap <UID> in|out`.

---

## 2. Log events (SD card log = cloud ingest payload)

Every event has a per-device, strictly increasing **`seq`**, a local timestamp **`ts`**
(`YYYY-MM-DDTHH:MM:SS`, Africa/Kampala, EAT = UTC+3) and a **`type`**.

| `type` | Fields | Emitted when |
|---|---|---|
| `BOOT` | `reason` | the hub starts |
| `ACCESS` | `uid name role side ok reason` | any card scan at a door (granted or denied) |
| `ROOM` | `room occupied daylight lux` | a room changes occupied ⇄ vacant, or daylight crosses a threshold |
| `LIGHT` | `room level mode lux` | a room's light level changes (0–100 %) |
| `ENERGY` | `room on_wh saved_wh` | every 10 min per room (running totals since boot) |
| `CONFIG` | `key value by` | schedule or time changed |
| `USER` | `action uid name role by` | user added / deleted / enabled / disabled |
| `AUTH` | `uid result` | admin login attempt, logout or timeout |
| `ALERT` | `code detail` | something management must know about |

### Enumerations

- `ACCESS.reason`: `OK`, `OK_PRIVILEGED`, `OK_ESCORT`, `UNKNOWN_CARD`, `DISABLED`,
  `OUTSIDE_HOURS`, `ANTI_PASSBACK`, `LOCKDOWN`
- `LIGHT.mode`: `AUTO`, `ON`, `OFF`
- `AUTH.result`: `CARD_OK`, `OK`, `BAD_PIN`, `LOCKED`, `NOT_ADMIN`, `TIMEOUT`, `LOGOUT`
- `USER.action`: `ADD`, `DEL`, `ENABLE`, `DISABLE`
- `ALERT.code`: `NODE_OFFLINE`, `NODE_ONLINE`, `BREAK_GLASS`, `AFTER_HOURS_EXIT`,
  `SD_FAIL`, `PIN_LOCKOUT`, `CRYPTO_FAIL`, `RFID_FAIL` (a reader is not answering; `detail` = `IN` or `OUT`)

### 2.1 SD card line format (tamper-evident)

One event per line in `/sdcard/LOGS/YYYYMMDD.LOG` (one file per day):

```
<seq>|<ts>|<type>|<k=v k=v ...>|<hash>
000042|2026-10-27T09:15:02|ACCESS|uid=01020304 name=Alice role=STUDENT side=IN ok=1 reason=OK|9f2c...e1
```

- `seq` is zero-padded to 6 digits. Field values never contain spaces or `|`.
- `hash = SHA-256( prev_hash + "|" + <line without the final "|hash"> )`, as 64 lower-case hex digits.
- `prev_hash` for the first line of each file is 64 zeros (the genesis).
- `log verify` recomputes the chain. Editing, inserting or deleting any line breaks every
  hash after it.

### 2.2 Cloud ingest (Day 2 transport, same events)

```http
POST /api/ingest
Content-Type: application/json
X-Device-Id: hub-01
X-Timestamp: 1793086502            # unix seconds; rejected if more than 300 s off server time
X-Signature: <hex HMAC-SHA256(key=device secret, msg = timestamp + "." + raw body)>

{ "deviceId": "hub-01",
  "events": [
    { "seq": 42, "ts": "2026-10-27T09:15:02", "type": "ACCESS",
      "data": { "uid": "01020304", "name": "Alice", "role": "STUDENT",
                "side": "IN", "ok": 1, "reason": "OK" },
      "hash": "9f2c...e1" }
  ] }
```

Response `200 { "ackSeq": 42, "inserted": 1, "duplicates": 0 }`. `(deviceId, seq)` is unique,
so re-sending a batch is harmless (idempotent ingest). `data` holds the same `k=v` pairs as the
SD line. Numeric-looking values (`ok`, `room`, `level`, `occupied`, `on_wh`, `saved_wh`) are sent
as numbers.

---

## 3. Node bus messages (between terminals)

`bus_send(dst, &msg)` / `bus_recv(self, &msg, timeout)`. The transport is FreeRTOS queues in the
single-board Wokwi build, and ESP-NOW frames on separate boards (same struct, packed).

| `type` | Direction | Payload |
|---|---|---|
| `MSG_CARD_SCAN` | RFID reader (entry or exit) → HUB | `uid`, `side` |
| `MSG_BREAK_GLASS` | button → HUB | – |
| `MSG_ROOM_STATE` | ROOMn → HUB | `occupied`, `daylight`, `level`, `lux`, `mode`, `on_wh`, `saved_wh` |
| `MSG_HEARTBEAT` | ROOMn → HUB | `uptime_s` |
| `MSG_LIGHT_CMD` | HUB → ROOMn | `mode` (AUTO/ON/OFF) |

Every message carries `src` and a per-sender `seq`. The hub drops any message whose `seq` is not
newer than the last one it saw from that sender (duplicate or replay protection).

---

## 4. Pin map (ESP32-S3-DevKitC-1, Wokwi)

Pins follow the board's physical header order, so the diagram stays tidy (only the readers' shared bus lines cross).
[`board_pins.h`](../firmware/components/common/include/board_pins.h) is the source of truth;
`firmware/tools/gen_diagram.py` generates `diagram.json` from it.

| Header | Function | GPIO | Notes |
|---|---|---|---|
| left | Room 1: LDR / PIR / light | 4 (ADC1_CH3) / 5 / 6 | LDRs need ADC1 |
| left | Room 2: LDR / PIR / light | 7 (ADC1_CH6) / 15 / 16 | |
| left | SD card MISO / SCK / MOSI / CS | 10 / 11 / 12 / 13 | SPI2 via GPIO matrix |
| left | Buzzer | 14 | LEDC |
| right | Entry RFID reader SDA (chip select) | 1 | MFRC522, SPI3 |
| right | RFID SCK / MOSI / MISO (both readers) | 2 / 42 / 41 | SPI3, 4 MHz |
| right | RFID RST (both readers) | 40 | active low |
| right | Exit RFID reader SDA (chip select) | 39 | |
| right | I2C SDA / SCL (LCD2004 @0x27, DS1307 @0x68) | 36 / 35 | shared bus |
| right | Entry door servo / Exit door servo | 48 / 47 | LEDC 50 Hz |
| right | Break-glass button (Wokwi key `e`) | 21 | pull-up, IRQ |
| right | "Access granted" green LED | 20 | |

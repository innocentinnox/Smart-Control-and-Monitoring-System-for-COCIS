# Day 1 Report: CoCIS Smart Control & Monitoring System

**Date:** 27 October 2026 · **Scope:** Day 1 of the two-day plan (core system + dashboard ingest)
**Status:** firmware builds with 0 warnings · 67/67 host unit tests pass · dashboard ingest tested
end to end against Postgres · **not yet run in the Wokwi simulator** (see §7).

---

## 1. What was built today

| Plan item (Day 1) | Status | Where |
|---|---|---|
| Shared contract: events, bus API, pin map | Done | [docs/event-schema.md](event-schema.md) |
| Repo skeleton + Wokwi wiring | Done | [firmware/](../firmware), [diagram.json](../firmware/diagram.json) |
| M1: RFID (buttons) + servo doors + LED | Done | `nodes/rfid.c`, `nodes/door.c`, `ui/ui.c` |
| M1: anti-passback + schedule enforcement | Done | `core_logic/access_policy.c`, `nodes/hub_access.c` |
| M2: PIR + LDR + PWM lights, grace timer | Done | `core_logic/room_logic.c`, `nodes/room_node.c` |
| M2: daylight harvesting + energy stats | Done | `core_logic/room_logic.c` |
| M3: SD gatekeeper task + bus queues | Done | `storage/storage.c`, `bus/bus.c` |
| M3: hash chain + `log verify`, ledger mutex | Done | `storage/storage.c`, `services/occupancy.c` |
| M4: serial CLI (read-only commands) | Done | `cli/cmd_monitor.c` |
| M4: admin login (card + PIN), `schedule set`, users | Done | `core_logic/auth_fsm.c`, `services/auth.c`, `cli/cmd_admin.c` |
| M5: Next.js + Prisma + Neon, `/api/ingest` | Done | [dashboard/](../dashboard) |
| M5: dashboard pages (occupancy, access log, energy) | Done | `dashboard/app/` |
| Evening integration on one ESP32-S3 | Builds and links; **simulator run pending** | `firmware/main/main.c` |

**Pulled forward from Day 2:** break-glass emergency override, after-hours security escort window,
lockdown mode, room-node heartbeats with an offline alert, the RAM fallback when there is no SD
card, and recovering the log chain after a reboot.

---

## 2. Architecture

```
            ┌──────────────────────── one ESP32-S3 (Wokwi) ────────────────────────┐
 buttons ─► │ rfid ─┐                                                              │
 keypad  ─► │ keypad┼─► [bus: HUB inbox] ─► HUB TASK (single authority) ─► door    │
 break   ─► │ glass ┘          ▲     │        │ access_policy  (pure)     ─► ui    │
            │                  │     │        │ auth_fsm       (pure)              │
            │ ROOM1 TASK ──────┘     │        ├─► occupancy ledger (mutex + NVS)   │
            │  room_logic (pure)     │        └─► cocis_log() ─► [queue] ─►        │
            │ ROOM2 TASK ◄───────────┘ LIGHT_CMD          STORAGE TASK (owns SD)   │
            │                                             hash chain, daily files  │
            │ CLI TASK (esp_console) ── reads services, sends bus commands         │
            └──────────────────────────────────────────────────────────────────────┘
```

### Components: one concern each

| Component | Responsibility | May use |
|---|---|---|
| `common` | Types (X-macro enums + string tables), pin map, task table, SHA-256 | – |
| `core_logic` | **Pure rules:** access policy, room lighting, admin 2FA state machine | common |
| `drivers` | I2C bus, LCD2004, DS1307, keypad, buttons, PWM (servo/lamp/buzzer), LDR | common |
| `bus` | Messages between terminals, sequence numbers, duplicate rejection | common |
| `storage` | SD gatekeeper task, hash-chained log, RAM fallback | common |
| `services` | Users, config, occupancy, auth, clock: shared state, each behind its own mutex, persisted in NVS | core_logic, storage |
| `ui` | LCD + buzzer + LED, owned by one task | drivers, services |
| `nodes` | The 3 terminals (hub, rooms) + door + RFID front-end | all of the above |
| `cli` | Serial management menu | services, nodes |

Dependencies only point downward. `core_logic` has no ESP-IDF calls at all, which is why it can be
unit-tested on a PC.

### Keeping the code DRY

- **Enums are declared once.** The same X-macro list generates both the enum and its string table
  (`cocis_types.h`).
- **One logging call.** Every module logs through `cocis_log(type, "k=v ...")`.
- **Shared low-level code.** One PWM layer serves servos, lamps and the buzzer. One button driver
  (ISR → queue → debounce task) serves both the card buttons and the break-glass button. One NVS
  blob helper serves all persistent state.
- **Single sources of truth.** Pins and LEDC channels live only in `board_pins.h`, task stacks and
  priorities only in `task_config.h`, and tunable behaviour only in `main/Kconfig.projbuild`
  (`idf.py menuconfig` → *CoCIS Smart Control*).

---

## 3. How the requirements are met

| Brief § | Requirement | Implementation |
|---|---|---|
| 2.1 | RFID entry/exit, door, records, who is inside | Card scan → `access_decide()` → servo door + LCD/buzzer → ACCESS log line → occupancy ledger. `who` prints the muster list. |
| 2.2 | Lights follow occupancy | PIR + grace timer, dim warning before off, LDR daylight harvesting with hysteresis, PWM fades. |
| 2.3 | 3 coordinated terminals | Hub + 2 room nodes as separate task groups talking only through `bus`. Rooms keep controlling their own lights if the hub is down. |
| 2.4 | SD card storage | `/sdcard/LOGS/YYYYMMDD.LOG`, one gatekeeper task, `fsync` after every line. |
| 2.5 | Serial menus | `status who rooms users log schedule user light lockdown emergency time login logout tap node help` |
| 2.6 | Configurable schedule, only authorised staff | `schedule set 07:00 22:00 mon-sat` requires an admin session (card + PIN). Students and staff can neither enter **nor leave** outside hours. |
| 2.7 | Synchronisation, no races | See §4. |

### Access rules (`core_logic/access_policy.c`, first match wins)

1. An unknown or disabled card is refused.
2. **Anti-passback:** you may enter only if you are outside, and exit only if you are inside. This
   keeps the ledger truthful.
3. **Lockdown:** only security and admin may pass.
4. Within opening hours, everyone passes.
5. Outside hours, security and admin still pass (24/7 roles).
6. Outside hours, an ordinary user may exit **only during an escort window**. The window opens for
   60 s when a security card exits after hours. Otherwise the person is held inside and an
   `AFTER_HOURS_EXIT` alert is raised for security.

**Safety decision (to defend in the presentation):** the **break-glass button** holds both doors
open, overriding every rule, and is logged as an alert. Only an admin can clear it. Being "held
inside after hours" therefore never traps anyone in a fire.

---

## 4. Synchronisation and reliability (requirement 2.7)

| Shared resource | Risk | Mechanism |
|---|---|---|
| Access decisions | Two scans decided at once give an inconsistent ledger | **Single writer:** only the hub task decides and writes the ledger; inputs are queued to it. |
| SD card / SPI | Concurrent writes corrupt FAT, interleave lines | **Gatekeeper task:** only `storage_task` touches the card. Other tasks queue requests; tail and verify are synchronous requests answered via a semaphore. |
| Log sequence numbers | Gaps or duplicates | Assigned only by the gatekeeper. Resumed from the last line on the card after a reboot. |
| Bus sequence numbers | seq 11 overtaking seq 10 from two sender tasks | Per-sender **mutex** around "take next seq + enqueue". *Found and fixed during review today.* |
| Duplicated or replayed frames | Processed twice | Receiver keeps the last seq per sender and drops anything not newer. The count is shown by `status`. |
| Users / config / occupancy / auth | Readers (CLI, LCD) see half an update | One **mutex** per module. Readers get copies. Config is committed to NVS before it becomes live. |
| ADC unit shared by two room tasks | Interleaved conversions | Mutex in `light_sensor.c`. |
| I2C bus (LCD + RTC) | Mixed transactions | IDF `i2c_master` bus lock per transaction. The LCD is used only by the ui task. |
| Door close timer vs. new open | Door closes on a new person | `ledc_set_duty_and_update` (thread-safe), and the timer restarts on every open. |
| ISR → task | Blocking in an ISR | ISRs only `xQueueSendFromISR` or notify a task. Debouncing happens in tasks. |
| Power loss | Lost or half-written state | NVS commits are atomic. Each log line is `fsync`ed. The ledger is persisted on every change. |
| Room node failure | Silent loss of control | 5 s heartbeats. After 15 s of silence the hub raises `NODE_OFFLINE` and the LCD shows it; the room keeps running its lights on its own. |
| SD card missing | Logging stops | Logs continue in a RAM ring buffer and an `SD_FAIL` alert is raised. |

### Tamper-evident log

Each line ends with `SHA-256(prev_hash | line)` (see
[event-schema.md §2.1](event-schema.md#21-sd-card-line-format-tamper-evident)). `log verify`
recomputes the chain and reports the first broken line. Editing, inserting or deleting a line is
detected. Truncating the newest lines is not detected; the Day-2 cloud copy covers that case.

---

## 5. Hardware (Wokwi `diagram.json`)

ESP32-S3-DevKitC-1, LCD2004 (I2C 0x27), DS1307 RTC (I2C 0x68), microSD (SPI2), 4×4 keypad,
2 servos (entry / exit door), buzzer, green "granted" LED, 4 card buttons (keyboard keys `1`–`4`),
a reader-select slide switch (IN/OUT), a break-glass button (key `b`), and per room a PIR, a
photoresistor and a white lamp LED.

The full pin table is in [event-schema.md §4](event-schema.md#4-pin-map-esp32-s3-devkitc-1-wokwi).

**The diagram is generated, not hand-drawn.** `python firmware/tools/gen_diagram.py` reads the GPIO
numbers from `board_pins.h`, places every part beside the header pins it uses, routes each wire
explicitly, and labels each group with `wokwi-text` parts. It also checks the result:
- no two different nets overlap, and no wire runs through a part;
- signal wires never cross each other;
- only power and ground rails cross signals, always at right angles.

Run it with `--preview out.png` (needs Pillow) to render a quick layout check. The diagram's GPIO set
was verified to match `board_pins.h` exactly (30 pins).

**RFID substitution:** Wokwi has no MFRC522 part. The reader is hidden behind `rfid.h`, so a real
reader (or a Wokwi custom chip, which is a Day-2 option) replaces only `rfid.c`.

---

## 6. Testing

| Test | Command | Result |
|---|---|---|
| Firmware build (ESP-IDF v6.0.3, esp32s3, `-Wall -Wextra -Werror` on project code) | `idf.py build` | **Pass.** 34 project files, 0 warnings. Image 0x63370 B, 74 % of the app partition free. |
| Host unit tests: schedule (incl. overnight windows), access rules (every rule + lockdown + escort), room logic (grace, dim warning, daylight hysteresis, override, energy), 2FA (happy path, non-admin card, lockout, timeouts, late PIN, ms wrap-around), enum parsing | `sh firmware/test/host/run_tests.sh` (gcc 14 in Docker) | **67/67 pass** |
| Diagram ↔ pin map consistency | script | **Pass**, 30/30 GPIOs |
| Dashboard: `prisma validate/generate`, `tsc --noEmit`, `next build --webpack` | in `dashboard/` | **Pass** |
| Dashboard ingest against Postgres 16 (Docker) | `scripts/send-sample.mjs` | 1st run 18 inserted · 2nd run 18 duplicates (idempotent) · bad signature → 401 · stale timestamp → 401 · invalid body → 400 |
| Dashboard pages render with data | fetch `/`, `/access`, `/energy` | **Pass.** Shows people inside, rooms, alerts, filters, energy saved. |

---

## 7. Not yet verified / known limitations (honest list)

1. **The firmware has not been run in Wokwi yet.** There was no Wokwi CLI token on this machine,
   so it is build-verified only. **First job tomorrow:** press play in VS Code and run the demo
   script in §8. The most likely things to need tuning:
   - the `lux` mapping of the Wokwi photoresistor (thresholds are in menuconfig);
   - the DS1307 initial time: Wokwi may report UTC; fix with `time set`;
   - the SD card formatting on first mount.
2. The firmware does not send to the dashboard yet. The device-side `cloud_sync` (Wi-Fi + HMAC
   outbox) is Day-2 work. The dev secret `dev-secret-hub-01-change-me` must match on both sides.
3. The dashboard has not been deployed to Vercel or connected to a real Neon database (it was
   tested against local Postgres).
4. On this PC, Windows Application Control blocks some unsigned tool binaries:
   - **ESP-IDF build:** it blocked `objdump` once; simply re-running the build worked.
   - **Next.js:** its default SWC compiler is blocked; use `next dev --webpack`.
5. Room "opening hours" for the energy baseline are read from the hub's config through an injected
   callback. On separate boards this would come from a hub sync message.

---

## 8. Demo script (for the simulator run and the videos)

```
status                          # overview
<press 1>                       # Alice taps at the IN reader   -> Welcome Alice, door opens
<press 1>                       # again at IN                   -> DENIED ANTI_PASSBACK
<flip switch to OUT, press 1>   # Alice leaves                  -> Goodbye Alice
tap DEADBEEF in                 # unregistered card             -> DENIED UNKNOWN_CARD
<click PIR room 1>              # room 1 occupied, light 100 %  (LDR at 100 lux = dark)
<wait 20 s>                     # light dims to 30 % (warning) ... off at 30 s
<move LDR slider to 1000 lux, click PIR>   # occupied but bright -> light stays off
<switch to IN, press 2 and 3>   # Bob and Carol enter while open
login                           # then press 4 (admin card), then: login 1234
schedule set 07:00 08:00 all    # pick a window that has already ended today -> CLOSED
<switch to OUT>
<Bob (2) at OUT>                # DENIED OUTSIDE_HOURS + AFTER_HOURS_EXIT alert
<Carol (3) at OUT>              # security leaves -> escort window opens
<Bob (2) at OUT>                # OK_ESCORT
node 2 offline                  # 15 s later: ALERT NODE_OFFLINE
<press b>                       # break glass: both doors open, siren
emergency clear
log tail 20
log verify                      # OK: hash chain intact
who
```

Keypad alternative for the login: press `A`, tap card 4, type `1234` and `#`.

---

## 9. Ownership map (for individual videos)

| Member | Owns (files to explain) |
|---|---|
| 1. Entrance | `core_logic/access_policy.c`, `nodes/hub_access.c`, `nodes/door.c`, `nodes/rfid.c` |
| 2. Rooms | `core_logic/room_logic.c`, `nodes/room_node.c`, `drivers/light_sensor.c`, `drivers/actuators.c` |
| 3. Storage & sync | `storage/storage.c`, `bus/bus.c`, `services/occupancy.c`, `services/nvs_store.c`, §4 of this report |
| 4. Interfaces & auth | `cli/*`, `core_logic/auth_fsm.c`, `services/auth.c`, `nodes/hub_keypad.c`, `ui/ui.c` |
| 5. Cloud | `dashboard/*` (+ Day 2 device `cloud_sync`) |

---

## 10. Day 2 next steps

1. Run in Wokwi and fix whatever the simulator reveals (§7.1).
2. Device `cloud_sync`: Wi-Fi STA (`Wokwi-GUEST`), outbox cursor on SD, signed batch POST matching
   `dashboard/lib/hmac.ts`.
3. Local HTTP server + embedded web UI on the hub.
4. Closing-time warning: 15 min before close, use `schedule_minutes_to_close()`, which already
   exists and is tested.
5. Report assembly + individual videos.

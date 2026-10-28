# CoCIS Smart Control & Monitoring System

ESP32-S3 access control, occupancy monitoring and automatic lighting for the CoCIS main
entrance and two lecture rooms (Embedded Systems coursework, Group 28, 2026/2027).

- **Entrance:** two MFRC522 RFID readers (entry and exit), two servo door locks, an LCD,
  a buzzer and a break-glass button. Anti-passback, a configurable opening schedule that also
  holds people inside after hours, and a security escort window.
- **Lecture rooms:** a PIR motion sensor and a light sensor per room drive a dimmable lamp,
  with daylight harvesting and energy-saved reporting.
- **Records and management:** a hash-chained (tamper-evident) event log on the SD card, and a
  serial command menu with two-factor admin login (admin card + PIN).

| Folder | What it is |
|---|---|
| [firmware/](firmware/) | ESP-IDF v6 project: the whole system. Runs without any network. |
| [firmware/diagram.json](firmware/diagram.json) | Wokwi circuit (20 parts + labels), generated from `board_pins.h`. |
| [firmware/wokwi/](firmware/wokwi/) | Files of the [online Wokwi project](https://wokwi.com/projects/479125502548914177) (open it and press play), plus a prebuilt backup firmware. |
| [dashboard/](dashboard/) | Optional Next.js + Prisma + Neon dashboard that ingests the device's event log. |
| [docs/report.pdf](docs/report.pdf) | The system operation report (LaTeX sources in [docs/report/](docs/report/)). |
| [docs/event-schema.md](docs/event-schema.md) | The shared contract: events, log line format, bus messages, demo cards, pin map. |

## Build and run (firmware)

```powershell
. C:\Espressif\tools\Microsoft.v6.0.3.PowerShell_profile.ps1   # activate ESP-IDF v6.0.3
cd firmware
idf.py set-target esp32s3     # first time only
idf.py build
```

Then open `firmware/` in VS Code and run **Wokwi: Start Simulator** (reads `wokwi.toml`).
Type `help` in the serial monitor. To scan a card, click a reader, pick a card (Blue = Alice,
Green = Bob, Yellow = Carol, Red = Admin) and press **Tap**. Key `e` is the break-glass button.

## Other tasks (from the repository root)

```sh
make test       # host unit tests (gcc, or the gcc:14 Docker image)
make diagram    # regenerate firmware/diagram.json and the report's wiring figure
make pdf        # build docs/report.pdf (MiKTeX); `make report` does the same
make wokwi-web  # regenerate firmware/wokwi/web/, the files of the online Wokwi project
make wokwi-fw   # rebuild the prebuilt backup firmware/wokwi/cocis_smart_control.uf2.bin
make sim        # run the Wokwi scenarios in firmware/test/wokwi/ (wokwi-cli + WOKWI_CLI_TOKEN)
```

## Dashboard (optional layer)

See [dashboard/README.md](dashboard/README.md).

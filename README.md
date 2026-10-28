# CoCIS Smart Control & Monitoring System

ESP32-S3 access control, occupancy monitoring and automatic lighting for the CoCIS main
entrance and two lecture rooms (BSE III / CS II embedded systems coursework, 2026/2027).

| Folder | What it is |
|---|---|
| [firmware/](firmware/) | ESP-IDF v6 project: the whole system. Runs without any network. |
| [firmware/diagram.json](firmware/diagram.json) | Wokwi circuit (24 parts, wired to match `board_pins.h`). |
| [dashboard/](dashboard/) | Optional Next.js + Prisma + Neon dashboard that ingests the device's event log. |
| [docs/event-schema.md](docs/event-schema.md) | The shared contract: events, log line format, bus messages, pin map. |
| [docs/REPORT.md](docs/REPORT.md) | Day-1 report: architecture, design decisions, test results, status. |

## Build and run (firmware)

```powershell
. C:\Espressif\tools\Microsoft.v6.0.3.PowerShell_profile.ps1   # activate ESP-IDF v6.0.3
cd firmware
idf.py set-target esp32s3     # first time only
idf.py build
```

Then open `firmware/` in VS Code and run **Wokwi: Start Simulator** (reads `wokwi.toml`).
Type `help` in the serial monitor.

## Unit tests (no ESP32 needed)

```sh
sh firmware/test/host/run_tests.sh     # uses gcc, or a gcc Docker image if gcc is missing
```

## Dashboard (optional layer)

See [dashboard/README.md](dashboard/README.md).

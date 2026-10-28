# The system on wokwi.com

Online project: https://wokwi.com/projects/479125502548914177

Open the link and press play. wokwi.com builds the firmware itself with its online ESP-IDF
builder (ESP-IDF v5.3), selected by `"builder": "esp-idf"` on the board in `diagram.json`.
The serial monitor shows the boot log and the `cocis>` prompt, and the LCD shows the clock.

To present a card, click the ENTRY or EXIT reader, pick a card and press **Tap**
(Blue = Alice, Green = Bob, Yellow = Carol, Red = Admin with PIN 1234, Key fob = unregistered).
Type `help` in the serial monitor for the commands.

## What is in this folder

| Path | What it is |
|---|---|
| `web/` | The firmware sources as one flat folder: the files of the online project. |
| `cocis_smart_control.uf2.bin` | The same firmware prebuilt (ESP-IDF v6), as a backup. |

The online builder takes the project's files from one folder, without components, CMake or
Kconfig. The code builds both ways unchanged: settings fall back to the defaults in
`cocis_kconfig.h` when there is no Kconfig, and `cocis_crypto.c` uses the SHA-256 API of
whichever ESP-IDF version builds it.

## Updating the online project after a code change

1. `make wokwi-web` (repository root) regenerates `web/` from `main/` and `components/`.
2. In the online project, upload the changed files with **Upload file(s)…** (a file with the
   same name replaces the old one), then **SAVE**.

`make wokwi-fw` refreshes `cocis_smart_control.uf2.bin` (ESP-IDF environment active). To run
it instead of the online build: click in the code editor, open the command palette (**F1**, or
right-click → **Command Palette**), choose **Upload Firmware and Start Simulation…** and select
the file. An uploaded firmware lasts until the page is reloaded.

In VS Code with the Wokwi extension, open `firmware/` and run **Wokwi: Start Simulator**: it
loads the local ESP-IDF v6 build from `wokwi.toml`.

#!/usr/bin/env python3
"""Copy the firmware sources into one flat folder for the wokwi.com online builder.

wokwi.com builds an ESP-IDF project from the files in the project's editor, all in
one folder (no components, no CMake, no Kconfig). This script gathers every .c / .h
of main/ and components/*/ (sources and include/ headers) into firmware/wokwi/web/,
which can then be added to the online project with "Upload file(s)...".

    python tools/export_wokwi_web.py          # -> firmware/wokwi/web/

The code builds both ways unchanged: settings fall back to cocis_kconfig.h when
there is no Kconfig, and cocis_crypto.c picks the right SHA-256 API for the
ESP-IDF version.
"""
import shutil
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
OUT = FIRMWARE / "wokwi" / "web"


def sources():
    yield from sorted((FIRMWARE / "main").glob("*.c"))
    for comp in sorted((FIRMWARE / "components").iterdir()):
        if comp.is_dir():
            yield from sorted(comp.glob("*.[ch]"))
            yield from sorted((comp / "include").glob("*.h"))


def main():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    seen = {}
    for src in sources():
        if src.name in seen:            # one folder: names must be unique
            raise SystemExit(f"duplicate file name {src.name}: {seen[src.name]} and {src}")
        seen[src.name] = src
        shutil.copy2(src, OUT / src.name)
    print(f"{len(seen)} files -> {OUT.relative_to(FIRMWARE.parent)}")


if __name__ == "__main__":
    main()

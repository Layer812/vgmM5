#!/usr/bin/env python3
"""Cardputer R67 APP-ONLY flasher: inspect actual partition layout before writing.
No erase_flash, no bootloader, no partition/NVS/storage writes or backups.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

EXPECTED = {
    "nvs": (1, 2, 0x9000, 0x5000),
    "otadata": (1, 0, 0xE000, 0x2000),
    "app0": (0, 0x10, 0x10000, 0x5F0000),
    "vgm_swap": (1, 0x82, 0x600000, 0x200000),
}
ARTIFACT = Path(__file__).resolve().parent.parent / "r67_hashed_thumbnails"


def stop(message):
    raise RuntimeError("STOP (no flash): " + message)


def esptool(port, *args):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3",
           "--port", port, "--baud", "460800", *map(str, args)]
    print("ESPTool:", " ".join(cmd[3:]), flush=True)
    subprocess.run(cmd, check=True)


def partitions(raw):
    found = {}
    for offset in range(0, len(raw), 32):
        entry = raw[offset:offset + 32]
        if len(entry) != 32:
            break
        if entry == b"\xff" * 32:
            break
        magic, typ, subtype, addr, length, name, flags = struct.unpack(
            "<HBBII16sI", entry)
        if magic == 0xEBEB:
            # MD5 trailer, not a partition
            break
        if magic != 0x50AA:
            stop("unknown partition table entry at 0x%X" % offset)
        name = name.split(b"\x00", 1)[0].decode("ascii", "replace")
        if name in found:
            stop("duplicate partition: " + name)
        found[name] = (typ, subtype, addr, length)
    return found


def check_layout(table):
    if table != EXPECTED:
        print("Device partition map:", table)
        stop("partition map differs from Cardputer R67's single-OTA layout")
    # Exactly one app at the known address; bootloader cannot select a
    # different OTA app while this exact table remains in place.
    apps = [(name, fields) for name, fields in table.items() if fields[0] == 0]
    if len(apps) != 1 or apps[0][0] != "app0":
        stop("ambiguous startup app")
    return table["app0"][2], table["app0"][3]


def check_otadata(raw):
    if len(raw) != 0x2000:
        stop("otadata read length mismatch")
    if raw == b"\xff" * len(raw):
        print("OTA data: blank, single ota_0 slot")
        return
    # The ota selection table consists of two independent 4 KiB records.
    # With only ota_0 in the partition map, any valid nonzero sequence maps
    # to ota_0. Unknown or explicitly invalid states must not be overridden.
    valid = 0
    for offset in (0, 0x1000):
        rec = raw[offset:offset + 32]
        seq = struct.unpack_from("<I", rec, 0)[0]
        state = struct.unpack_from("<I", rec, 24)[0]
        if seq == 0xFFFFFFFF:
            continue
        if seq == 0:
            stop("unexpected OTA sequence 0")
        if state in (3, 4):
            continue  # invalid / aborted
        if state not in (0, 1, 2, 0xFFFFFFFF):
            stop("unrecognized OTA state 0x%08X" % state)
        valid += 1
    if not valid:
        stop("OTA slot selection is not safely established")
    print("OTA data: only app0/ota_0 exists; startup target confirmed")


def verify_firmware(variant):
    manifest = ARTIFACT / "RESULT.json"
    if not manifest.is_file():
        stop("RESULT.json missing: extract the COMPLETE GitHub Actions artifact")
    report = json.loads(manifest.read_text(encoding="utf-8"))
    info = report.get("variants", {}).get(variant)
    if not info:
        stop("requested variant not present in build manifest")
    filename = info.get("name", "")
    if not filename or Path(filename).name != filename or not filename.endswith(".bin"):
        stop("invalid firmware name")
    binary = ARTIFACT / filename
    if not binary.is_file():
        stop("firmware.bin missing: " + filename)
    data = binary.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != info.get("bytes") or digest.lower() != info.get("sha256", "").lower():
        stop("firmware size/SHA-256 mismatch")
    if len(data) < 0x1000 or data[0] != 0xE9 or not (1 <= data[1] <= 16):
        stop("invalid ESP application image header")
    # esptool's ESP image header places the chip ID at bytes 12..13.
    chip_id = struct.unpack_from("<H", data, 12)[0]
    if chip_id != 9:
        stop("firmware chip ID is not ESP32-S3 (chip ID %d)" % chip_id)
    print("PASS firmware:", filename, len(data), "bytes")
    print("PASS SHA-256:", digest)
    return binary, len(data)


def main():
    p = argparse.ArgumentParser(description="Cardputer R67 validated APP-ONLY flashing")
    p.add_argument("--port", required=True, help="Cardputer serial port, e.g. COM9")
    p.add_argument("--variant", choices=("standard", "classic"), default="standard")
    args = p.parse_args()
    binary, length = verify_firmware(args.variant)
    try:
        import esptool  # noqa: F401
    except ImportError:
        stop("esptool not installed: run python -m pip install esptool")
    with tempfile.TemporaryDirectory(prefix="cardputer_r67_app_only_") as temporary:
        tmp = Path(temporary)
        pt = tmp / "partitions.bin"
        ota = tmp / "otadata.bin"
        # READ ONLY. These temporary inspection files are deleted automatically.
        esptool(args.port, "read_flash", "0x8000", "0x1000", pt)
        table = partitions(pt.read_bytes())
        offset, capacity = check_layout(table)
        if length > capacity:
            stop("firmware exceeds OTA app partition size")
        esptool(args.port, "read_flash", "0xE000", "0x2000", ota)
        check_otadata(ota.read_bytes())
        print("PASS target: app0 (ota_0), offset 0x%X, capacity 0x%X" %
              (offset, capacity))
        print("Writing ONLY application firmware. Never partition table/otadata.")
        esptool(args.port, "--after", "hard_reset", "write_flash",
                hex(offset), binary)
    print("PASS: Cardputer R67", args.variant,
          "APP-ONLY flashed. Device reset to launch the app.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError, ValueError,
            KeyError, json.JSONDecodeError) as exc:
        print("ERROR:", exc, file=sys.stderr)
        sys.exit(1)

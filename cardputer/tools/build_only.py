#!/usr/bin/env python3
"""Build the original v0.98 Cardputer firmware without flashing any device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import traceback
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = ("albums.bin", "tracks.bin", "strings.bin",
         "companies.bin", "composers.bin", "systems.bin")
RECORD_SIZES = {
    "albums.bin": 22, "tracks.bin": 12, "companies.bin": 6,
    "composers.bin": 6, "systems.bin": 6,
}


def git_blob_sha(data):
    header = b"blob " + str(len(data)).encode("ascii") + b"\0"
    return hashlib.sha1(header + data).hexdigest()


def check_sources():
    manifest = json.loads((ROOT / "SOURCE_LOCK.json").read_text(encoding="utf-8"))
    expected = manifest["source_git_blob_sha1"]
    if len(expected) != 45:
        raise RuntimeError("Source lock is incomplete; expected 45 files")
    # Keep the baseline 45-file lock immutable. On an explicitly patched
    # development branch, accept exactly two modified sources plus one header,
    # each pinned by its Git blob SHA in a separate manifest.
    patches = {}
    patch_lock_path = ROOT / "SAFETY_PATCH_LOCK.json"
    if patch_lock_path.is_file():
        patch_lock = json.loads(patch_lock_path.read_text(encoding="utf-8"))
        patches = patch_lock.get("patched_git_blob_sha1", {})
        allowed = {"src/fm_engine.c", "src/pcm_engine.c", "src/sound_safe_math.h"}
        if (patch_lock.get("base_commit") != "2e6f0f9f7ad7a3f5e5544cceb231e60f88cfff64" or set(patches) != allowed):
            raise RuntimeError("Unexpected safety patch lock configuration")
        for name in ("src/fm_engine.c", "src/pcm_engine.c"):
            if patches[name] == expected[name]:
                raise RuntimeError("Safety patch has no change: " + name)
    for name, sha in sorted(expected.items()):
        path = ROOT / name
        accepted = patches.get(name, sha)
        if not path.is_file() or git_blob_sha(path.read_bytes()) != accepted:
            raise RuntimeError("SOURCE LOCK MISMATCH: " + name)
    if patches:
        path = ROOT / "src/sound_safe_math.h"
        if not path.is_file() or git_blob_sha(path.read_bytes()) != patches["src/sound_safe_math.h"]:
            raise RuntimeError("SOURCE LOCK MISMATCH: src/sound_safe_math.h")
    if git_blob_sha((ROOT / "partitions.csv").read_bytes()) != manifest["partitions_git_blob_sha1"]:
        raise RuntimeError("Original partition table changed")
    pio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    for required in ("[env:m5stack-cardputer]", "board = m5stack-stamps3",
                     "m5stack/M5Unified@0.2.14", "m5stack/M5Cardputer@^1.1.1",
                     "platform = espressif32 @ 6.13.0",
                     "board_build.partitions = partitions.csv",
                     "board_build.embed_txtfiles ="):
        if required not in pio:
            raise RuntimeError("Unexpected Cardputer build configuration: " + required)
    for filename in FILES:
        if ("src/" + filename) not in pio:
            raise RuntimeError("Missing embedded DB reference: " + filename)
    print("PASS: Cardputer source lock (43 original + 2 pinned changes + 1 header)" if patches
          else "PASS: original Cardputer sources (45) and partition table are unchanged", flush=True)


def check_db(create):
    paths = [ROOT / "src" / name for name in FILES]
    present = [p.exists() for p in paths]
    if any(present) and not all(present):
        missing = [p.name for p, exists in zip(paths, present) if not exists]
        raise RuntimeError("PARTIAL ONLINE DB: missing " + ", ".join(missing)
                           + ". Provide all six DBs or delete all six for offline mode.")
    if not any(present):
        if not create:
            print("PASS: six DBs absent; offline-only placeholders will be generated for build", flush=True)
            return "offline-pending"
        for path in paths:
            path.write_bytes(b"\x00")
        print("NOTE: created six one-byte empty DB placeholders (offline catalog only)", flush=True)
    contents = {p.name: p.read_bytes() for p in paths}
    if all(data == b"\x00" for data in contents.values()):
        print("DB mode: OFFLINE; local SD playback remains available; Online has no catalog", flush=True)
        return "offline"
    if any(data == b"\x00" for data in contents.values()):
        raise RuntimeError("Mixed empty and real DB files are forbidden; replace ALL six.")
    for name, size in RECORD_SIZES.items():
        if len(contents[name]) == 0 or len(contents[name]) % size != 0:
            raise RuntimeError("DB format/record-size mismatch: " + name)
    if not contents["strings.bin"].endswith(b"\x00"):
        raise RuntimeError("strings.bin requires a trailing NUL")
    print("DB mode: supplied catalog; basic structure verified, content not audited", flush=True)
    return "provided"


def find_pio():
    env = os.environ.get("PIO_EXE", "").strip()
    candidates = [env] if env else []
    w = shutil.which("pio")
    if w:
        candidates.append(w)
    home = Path.home()
    candidates += [
        str(home / ".platformio" / "penv" / "Scripts" / "pio.exe"),
        str(home / ".platformio" / "penv" / "bin" / "pio"),
    ]
    for c in candidates:
        if c and Path(c).is_file():
            return c
    raise RuntimeError("PlatformIO CLI not found. Install/use existing PlatformIO Core, or set PIO_EXE.")


def run_build():
    pio = find_pio()
    cmd = [pio, "run", "--project-dir", str(ROOT), "-e", "m5stack-cardputer"]
    reports = ROOT / "reports"
    reports.mkdir(exist_ok=True)
    logpath = reports / "native_build.log"
    print("BUILD COMMAND:", " ".join(cmd), flush=True)
    print("NO FLASH: pio run only. Never invokes upload or esptool.", flush=True)
    with logpath.open("w", encoding="utf-8", errors="replace", newline="\n") as log:
        with subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, encoding="utf-8", errors="replace", bufsize=1) as proc:
            for line in proc.stdout:
                print(line, end="", flush=True)
                log.write(line)
            return proc.wait()


def save_report(result):
    reports = ROOT / "reports"
    reports.mkdir(exist_ok=True)
    output = reports / "RESULT.json"
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    dest = Path("F:/build/tmp/VGMM5_CARDPUTER_BUILD_SEND_THIS.zip") if os.name == "nt" and Path("F:/build/tmp").is_dir() \
        else ROOT / "VGMM5_CARDPUTER_BUILD_SEND_THIS.zip"
    image = ROOT / ".pio" / "build" / "m5stack-cardputer"
    with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for filename in ("RESULT.json", "native_build.log"):
            p = reports / filename
            if p.exists():
                z.write(p, filename)
        for filename in ("SOURCE_LOCK.json", "platformio.ini", "partitions.csv"):
            z.write(ROOT / filename, filename)
        if result["status"] == "PASS":
            for filename in ("firmware.bin", "firmware.elf"):
                p = image / filename
                if p.is_file():
                    z.write(p, "artifacts/" + filename)
    print("ONE REPORT:", dest, flush=True)


def main():
    argp = argparse.ArgumentParser()
    argp.add_argument("--check-only", action="store_true")
    args = argp.parse_args()
    if args.check_only:
        check_sources()
        check_db(create=False)
        return 0
    result = {"project": "vgmM5 Cardputer v0.98 independent",
              "flash": "NEVER", "native_build": "NOT RUN", "status": "SAFE_STOP"}
    try:
        check_sources()
        result["db_mode"] = check_db(create=True)
        code = run_build()
        result["return_code"] = code
        if code:
            result["native_build"] = "FAIL"
            raise RuntimeError("PlatformIO build failed, rc=" + str(code))
        binpath = ROOT / ".pio" / "build" / "m5stack-cardputer" / "firmware.bin"
        if not binpath.is_file():
            raise RuntimeError("PlatformIO returned success but firmware.bin is missing")
        data = binpath.read_bytes()
        result.update({"native_build": "PASS", "status": "PASS",
                       "firmware_bytes": len(data),
                       "firmware_sha256": hashlib.sha256(data).hexdigest()})
        return 0
    except Exception as exc:
        print("SAFE STOP:", exc, flush=True)
        result["error"] = str(exc)
        traceback.print_exc()
        return 2
    finally:
        save_report(result)


if __name__ == "__main__":
    sys.exit(main())

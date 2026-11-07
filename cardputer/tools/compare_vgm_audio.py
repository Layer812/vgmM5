#!/usr/bin/env python3
"""Real .vgm/.vgz 3-way YM2151/C140 comparison, no recording/distribution.

The input is a locally supplied VGM file. Its bytes never get committed
or uploaded. Only synthesized WAV stems and the JSON analysis are saved.
Original R6 and migrated Cardputer/CoreS3 share the SAME VGM command timeline.
"""
import argparse
from array import array
import gzip
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[2]
CARD = ROOT / "cardputer"
LIB = ROOT / "lib" / "vgmM5lib" / "src"
MAX_VGM_BYTES = 64 * 1024 * 1024


def run_checked(cmd):
    completed = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if completed.returncode:
        raise RuntimeError(
            "Failed (" + str(completed.returncode) + "): " + " ".join(map(str, cmd))
            + "\n" + completed.stdout[-2000:] + completed.stderr[-3500:]
        )
    return completed.stdout.strip()


def unpack_vgm(source: Path, destination: Path):
    with source.open("rb") as f:
        magic = f.read(4)
    if magic[:2] == b"\x1f\x8b":
        with gzip.open(source, "rb") as src, destination.open("wb") as dst:
            remaining = MAX_VGM_BYTES + 1
            while remaining:
                block = src.read(min(65536, remaining))
                if not block:
                    break
                dst.write(block)
                remaining -= len(block)
        if destination.stat().st_size > MAX_VGM_BYTES:
            raise ValueError("Decompressed VGM exceeds 64 MiB")
    elif magic == b"Vgm ":
        if source.stat().st_size > MAX_VGM_BYTES:
            raise ValueError("VGM exceeds 64 MiB")
        shutil.copyfile(source, destination)
    else:
        raise ValueError("Expected VGM or gzip-compressed VGZ")
    with destination.open("rb") as f:
        if f.read(4) != b"Vgm ":
            raise ValueError("Decoded file has no VGM magic")


def build(executable: Path, edition: str, shim: Path, cardputer_gain: int):
    if edition == "R6":
        src_dir = CARD / "src"
        definitions = []
    else:
        src_dir = LIB
        definitions = (
            ["-DIS_CORES3=1"]
            if edition == "CoreS3" else
            ["-DVGMM5_CARDPUTER_VGMM5LIB=1",
             "-DVGMM5_CARDPUTER_C140_GAIN_Q8=" + str(cardputer_gain * 256)]
        )
    return run_checked([
        "gcc", "-std=gnu11", "-O2", "-ffunction-sections",
        "-fdata-sections", *definitions, "-I" + str(shim),
        "-I" + str(src_dir), "-I" + str(CARD / "src"),
        str(CARD / "tests" / "r7_real_vgm_compare.c"),
        str(src_dir / "fm_engine.c"), str(src_dir / "pcm_engine.c"),
        str(src_dir / "ssg_engine.c"),
        "-Wl,--gc-sections", "-lm", "-o", str(executable)
    ])


def create_shims(folder: Path):
    shim = folder / "headers"
    shim.mkdir(parents=True)
    (shim / "esp_attr.h").write_text(
        "#pragma once\n#define IRAM_ATTR\n#define DRAM_ATTR\n"
    )
    for name in ("esp_partition.h", "esp_heap_caps.h", "esp_spi_flash.h"):
        (shim / name).write_text("#pragma once\n")
    return shim


def match_rms(source: Path, destination: Path, target_rms=4000):
    with wave.open(str(source), "rb") as stream:
        assert stream.getframerate() == 44100 and stream.getsampwidth() == 2
        assert stream.getnchannels() == 2
        fmt = stream.getparams()
        raw = stream.readframes(stream.getnframes())
    pcm = array("h")
    pcm.frombytes(raw)
    if sys.byteorder != "little":
        pcm.byteswap()
    if not pcm:
        raise ValueError("No audio to normalize")
    rms = math.sqrt(sum(int(s) ** 2 for s in pcm) / len(pcm))
    peak = max(abs(int(s)) for s in pcm)
    gain = min(target_rms / rms, 28000 / peak) if rms > 0 and peak > 0 else 1.0
    samples = array("h", (
        max(-32768, min(32767, round(int(s) * gain))) for s in pcm
    ))
    if sys.byteorder != "little":
        samples.byteswap()
    with wave.open(str(destination), "wb") as dst:
        dst.setparams(fmt)
        dst.writeframes(samples.tobytes())
    return {"original_rms": rms, "original_peak": peak, "gain": gain}


def main():
    parser = argparse.ArgumentParser(
        description="Compare real YM2151+C140 VGM/VGZ source PCM: Cardputer R6, "
                    "Cardputer R7, CoreS3 R67. No device flash."
    )
    parser.add_argument("file", type=Path, help="VGM/VGZ file you may lawfully use")
    parser.add_argument("--seconds", type=int, default=30, help="1..180 seconds")
    parser.add_argument("--cardputer-gain", type=int, choices=(1, 2, 4, 8), default=1,
                        help="Cardputer R7 C140-only gain relative to CoreS3; default 1")
    parser.add_argument("--output", type=Path, help="output directory")
    parser.add_argument("--zip", action="store_true",
                        help="write WAV + JSON comparison ZIP, excludes source VGM")
    args = parser.parse_args()
    if args.seconds not in range(1, 181):
        parser.error("--seconds must be between 1 and 180")
    if not args.file.is_file():
        parser.error("VGM/VGZ file not found")
    outdir = args.output or args.file.with_suffix("").with_name(
        args.file.stem + "_vgmM5_sound_comparison"
    )
    outdir.mkdir(parents=True, exist_ok=True)
    report = {
        "source": args.file.name, "source_sha256": hashlib.sha256(args.file.read_bytes()).hexdigest(),
        "note": "Same real VGM command stream; only YM2151 and C140. "
                "Board speaker/DAC not measured; input content not distributed.",
        "cardputer_c140_gain": args.cardputer_gain,
        "results": {}
    }
    with tempfile.TemporaryDirectory(prefix="vgmm5_real_vgm_") as folder:
        tmp = Path(folder)
        vgm = tmp / "source.vgm"
        unpack_vgm(args.file, vgm)
        shim = create_shims(tmp)
        for edition in ("R6", "R7", "CoreS3"):
            executable = tmp / edition
            build(executable, edition, shim, args.cardputer_gain)
            output_prefix = outdir / {
                "R6": "Cardputer_R6_original",
                "R7": "Cardputer_R7_vgmM5lib",
                "CoreS3": "CoreS3_R67_vgmM5lib"
            }[edition]
            result = run_checked([
                str(executable), str(vgm), str(output_prefix), str(args.seconds)
            ])
            report["results"][edition] = json.loads(result)
            del executable

    a, b, c = (report["results"][x] for x in ("R6", "R7", "CoreS3"))
    common = ("fm", "c140", "mix")
    exact = {
        stem: b["stems"][stem]["fnv"] == c["stems"][stem]["fnv"]
        for stem in common
    }
    report["r7_vs_cores3_hash_identical"] = exact
    report["r6_vs_r7_hash_identical"] = {
        stem: a["stems"][stem]["fnv"] == b["stems"][stem]["fnv"]
        for stem in common
    }
    if args.cardputer_gain == 1 and not all(exact.values()):
        raise AssertionError("Cardputer R7 must be bit-exact with CoreS3 before speaker output")
    # Additional loudness-matched copies make timbre differences audible
    # without confusing the comparison with the original R6 amplitude.
    normalized = {}
    for edition, base in (
        ("R6", "Cardputer_R6_original"),
        ("R7", "Cardputer_R7_vgmM5lib"),
        ("CoreS3", "CoreS3_R67_vgmM5lib")
    ):
        src = outdir / (base + "_mix.wav")
        dst = outdir / (base + "_mix_loudness_matched.wav")
        normalized[edition] = match_rms(src, dst)
    report["loudness_matching"] = normalized
    (outdir / "AUDIO_COMPARE_RESULT.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    if args.zip:
        target = outdir / "vgmm5_real_vgm_audio_comparison.zip"
        with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED) as z:
            for file in outdir.iterdir():
                if file.is_file() and file != target:
                    z.write(file, file.name)
        print("WAV + analysis only (original VGM not included):", target)
    print("PCM comparison completed:", outdir)
    for stem in common:
        print(stem, "R7/CoreS3 exact:", exact[stem],
              "Original R6 vs R7 exact:", report["r6_vs_r7_hash_identical"][stem])
    return 0


if __name__ == "__main__":
    sys.exit(main())

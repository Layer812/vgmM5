#!/usr/bin/env python3
"""Cardputer R7 shared-vgmM5lib native build. No backup, no flash."""
import hashlib
import json
import os
from pathlib import Path
import sys
import zipfile
from build_only import ROOT, check_db, run_build

def main():
    ini=(ROOT/"platformio.ini").read_text(encoding="utf-8")
    for needed in ("VGMM5_CARDPUTER_VGMM5LIB=1",
                   "symlink://../lib/vgmM5lib",
                   "-<fm_engine.c>", "-<pcm_engine.c>",
                   "-<psg_engine.c>", "-<ssg_engine.c>", "-<scc_engine.c>"):
        if needed not in ini:
            raise RuntimeError("Unexpected Cardputer R7 config: " + needed)
    engine=(ROOT/"src/vgm_engine.cpp").read_text(encoding="utf-8")
    if "#include <vgmM5lib.h>" not in engine or "r31_vgm_bridge_init" not in engine:
        raise RuntimeError("Cardputer R7 audio engine not found")
    catalog=check_db(create=True)
    rc=run_build()
    reports=ROOT/"reports"
    reports.mkdir(exist_ok=True)
    fw=ROOT/".pio/build/m5stack-cardputer/firmware.bin"
    success=rc==0 and fw.is_file() and fw.stat().st_size>100000
    report={"edition":"Cardputer R7 vgmM5lib","device":"M5Stack Cardputer",
            "native_build":"PASS" if success else "FAIL",
            "source_core":"../lib/vgmM5lib (same as CoreS3)",
            "db_mode":catalog,"return_code":rc,"flash":"NOT PERFORMED"}
    if success:
        data=fw.read_bytes()
        report.update({"firmware_bytes":len(data),
                       "firmware_sha256":hashlib.sha256(data).hexdigest()})
    out=reports/"R7_RESULT.json"
    out.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    location=Path("F:/build/tmp/VGMM5_CARDPUTER_R7_VGMM5LIB_BUILD.zip") if (
        os.name=="nt" and Path("F:/build/tmp").is_dir()) else ROOT/"VGMM5_CARDPUTER_R7_VGMM5LIB_BUILD.zip"
    with zipfile.ZipFile(location,"w",compression=zipfile.ZIP_DEFLATED) as z:
        z.write(out,"RESULT.json")
        for doc in ("platformio.ini","partitions.csv"):
            z.write(ROOT/doc,doc)
        log=reports/"native_build.log"
        if log.is_file(): z.write(log,"native_build.log")
        if success:
            for filename in ("firmware.bin","firmware.elf"):
                p=ROOT/".pio/build/m5stack-cardputer"/filename
                if p.is_file(): z.write(p,"artifacts/"+filename)
    print("R7 RESULT:",report["native_build"],"NO FLASH")
    print("R7 ARTIFACT:",location)
    return 0 if success else 2

if __name__=="__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print("Cardputer R7 build failed:",str(e))
        sys.exit(2)

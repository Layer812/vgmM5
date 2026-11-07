#!/usr/bin/env python3
"""Native Cardputer R67 build (standard / optional Classic), never flashes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import zipfile
from build_only import ROOT, check_db, find_pio

def build():
    parser=argparse.ArgumentParser()
    parser.add_argument("--classic",action="store_true",help="C140 Classic 8x")
    args=parser.parse_args()
    env="m5stack-cardputer-classic" if args.classic else "m5stack-cardputer"
    name="Classic" if args.classic else "Standard"
    ini=(ROOT/"platformio.ini").read_text(encoding="utf-8")
    for fragment in ("symlink://../lib/vgmM5lib",
                     "VGMM5_CARDPUTER_VGMM5LIB=1",
                     "[env:m5stack-cardputer-classic]",
                     "-<fm_engine.c>","-<pcm_engine.c>"):
        if fragment not in ini:raise RuntimeError("R67 source config mismatch: "+fragment)
    source=(ROOT/"src/voice.cpp").read_text(encoding="utf-8")
    if '"vgmM5 R67"' not in source or "cardputer_draw_playback_view" not in source:
        raise RuntimeError("R67 display update missing")
    engine=(ROOT/"src/vgm_engine.cpp").read_text(encoding="utf-8")
    if "#include <vgmM5lib.h>" not in engine:
        raise RuntimeError("Shared vgmM5lib source missing")
    catalog=check_db(create=True)
    reports=ROOT/"reports"
    reports.mkdir(exist_ok=True)
    logfile=reports/("R67_"+name.upper()+"_BUILD.log")
    command=[find_pio(),"run","--project-dir",str(ROOT),"-e",env]
    print("R67:",name,"\n"," ".join(command),"\nNO FLASH",flush=True)
    with logfile.open("w",encoding="utf-8",errors="replace") as log:
        with subprocess.Popen(command,cwd=ROOT,stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT,text=True,
                              encoding="utf-8",errors="replace",bufsize=1) as proc:
            for line in proc.stdout:
                print(line,end="",flush=True)
                log.write(line)
            rc=proc.wait()
    fw=ROOT/".pio/build"/env/"firmware.bin"
    success=(rc==0 and fw.is_file() and fw.stat().st_size>100000)
    result={"board":"M5Stack Cardputer","edition":"R67 "+name,
            "native_build":"PASS" if success else "FAIL",
            "flash":"NOT PERFORMED","app_offset":"0x10000",
            "db_mode":catalog,"return_code":rc}
    if success:
        data=fw.read_bytes()
        result["firmware_sha256"]=hashlib.sha256(data).hexdigest()
        result["firmware_bytes"]=len(data)
    out=reports/("R67_"+name.upper()+"_RESULT.json")
    out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",
                   encoding="utf-8")
    dst=Path("F:/build/tmp") if os.name=="nt" and Path("F:/build/tmp").is_dir() else ROOT
    package=dst/("VGMM5_CARDPUTER_R67_"+name.upper()+"_BUILD.zip")
    with zipfile.ZipFile(package,"w",zipfile.ZIP_DEFLATED) as z:
        z.write(out,"RESULT.json")
        z.write(logfile,"native_build.log")
        for file in ("platformio.ini","partitions.csv"):
            z.write(ROOT/file,file)
        if success:
            z.write(fw,"firmware.bin")
            elf=fw.with_suffix(".elf")
            if elf.is_file(): z.write(elf,"firmware.elf")
    print("RESULT:",result["native_build"],"NO FLASH")
    print("REPORT:",package)
    return 0 if success else 2

if __name__=="__main__":
    try:sys.exit(build())
    except Exception as error:
        print("R67 BUILD ERROR:",str(error),flush=True)
        sys.exit(2)

#!/usr/bin/env python3
"""vgmM5 Tab5 R61 + YM2151 Cardputer R5 algo0 candidate: guarded BUILD ONLY."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
FW = ROOT / 'firmware'
ENV = 'tab5-r61-speaker-revert'
LOCK = ROOT / 'R5_CANDIDATE_PATCH_LOCK.json'
REPORT_NAME = 'VGMM5_TAB5_R5_YM2151_BUILD_SEND_THIS.zip'

def blob_sha(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode('ascii') + b'\0' + data).hexdigest()

def check_sources():
    original = json.loads((ROOT / 'PUBLIC_SOURCE_SHA256.json').read_text(encoding='utf8'))
    patch = json.loads(LOCK.read_text(encoding='utf8'))
    expected = {
        'format':'vgmM5-tab5-r61-r5-candidate-v1',
        'modified_path':'firmware/src/fm_engine.c',
        'original_blob_sha1':'e61968a51584096193bd12856b9f655ef07e07d0',
        'patched_blob_sha1':'dcad3f690246de08f166ef4dbae682587781de80',
        'condition':'VGMM5_TAB5_YM2151_R5_ALGO0_PREVIEW',
    }
    for k, v in expected.items():
        if patch.get(k) != v: raise RuntimeError('R5 patch lock unexpected: '+k)
    if len(original) < 70: raise RuntimeError('Original R61 pin set incomplete')
    for rel, expected_sha in original.items():
        data = (ROOT / rel).read_bytes()
        if rel == patch['modified_path']:
            if blob_sha(data) != patch['patched_blob_sha1']:
                raise RuntimeError('Unexpected R5 FM patch hash')
        elif hashlib.sha256(data).hexdigest() != expected_sha:
            raise RuntimeError('SAFE STOP: R61 source hash mismatch: '+rel)
    src = (ROOT / 'firmware/src/fm_engine.c').read_text(encoding='utf8')
    if src.count('VGMM5_TAB5_YM2151_R5_ALGO0_PREVIEW') < 2:
        raise RuntimeError('R5 preview guard absent')
    print('PASS: R61 protected source manifest; one separately pinned YM2151 FM source')
    return len(original)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--check-only', action='store_true', help='Validate source lock without build')
    args = p.parse_args()
    count = check_sources()
    if args.check_only:
        print('CHECK_ONLY PASS, NO FLASH')
        return
    if os.name != 'nt':
        raise RuntimeError('Windows PlatformIO required for this build runner; no device access')
    import build_tab5
    build_tab5.check_patch()
    pio = shutil.which('pio')
    if not pio:
        fallback = Path.home()/'.platformio'/'penv'/'Scripts'/'pio.exe'
        if fallback.is_file(): pio=str(fallback)
    if not pio: raise RuntimeError('PlatformIO is not installed')
    def run(command):
        print('RUN:', ' '.join(map(str,command)), flush=True)
        subprocess.run([str(x) for x in command],cwd=FW,check=True)
    run([pio,'pkg','install','-e',ENV])
    run([sys.executable,str(ROOT/'patch_m5gfx_r60.py')])
    run([pio,'run','-e',ENV])
    check_sources()
    binpath = FW/'.pio'/'build'/ENV/'firmware.bin'
    if not binpath.is_file() or not 100 < binpath.stat().st_size < 0x800000 or binpath.read_bytes()[:1]!=b'\xe9':
        raise RuntimeError('Invalid firmware BIN; NO FLASH')
    result = {'status':'PASS','flash':False,'r61_pinned_files':count,
              'firmware_bytes':binpath.stat().st_size,
              'firmware_sha256':hashlib.sha256(binpath.read_bytes()).hexdigest()}
    report_dir = Path('F:/build/tmp')
    if not report_dir.is_dir(): report_dir=ROOT
    report = report_dir/REPORT_NAME
    with zipfile.ZipFile(report,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('RESULT.json',json.dumps(result,indent=2)+'\n')
        z.write(LOCK,LOCK.name)
        z.write(binpath,'firmware.bin')
    print('PASS native Tab5 candidate BUILD ONLY',report)

if __name__=='__main__':
    try: main()
    except Exception as exc:
        print('SAFE STOP (NO FLASH):',exc,flush=True)
        sys.exit(2)

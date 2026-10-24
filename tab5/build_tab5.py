#!/usr/bin/env python3
"""vgmM5 Tab5 R61 FIX1: source-pinned, **BUILD ONLY**. No device access."""
import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
FW = ROOT / 'firmware'
ENV = 'tab5-r61-speaker-revert'
MANIFEST = ROOT / 'PUBLIC_SOURCE_SHA256.json'

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def check_sources():
    pins = json.loads(MANIFEST.read_text(encoding='utf-8'))
    if len(pins) < 70:
        raise RuntimeError('Incomplete source manifest')
    for rel, expected in pins.items():
        path = ROOT / rel
        if not path.is_file() or sha256(path) != expected:
            raise RuntimeError(f'SAFE STOP: source SHA mismatch: {rel}')
    return len(pins)

def check_patch():
    tree = ast.parse((ROOT / 'patch_m5gfx_r60.py').read_text(encoding='utf-8'))
    found = []
    for obj in tree.body:
        if isinstance(obj, ast.Assign) and len(obj.targets) == 1 and isinstance(obj.targets[0], ast.Name) and obj.targets[0].id == 'ENV':
            found.append(ast.literal_eval(obj.value))
    if found != [ENV]:
        raise RuntimeError('SAFE STOP: overlay environment mismatch')

def run(argv):
    print('RUN:', ' '.join(map(str, argv)), flush=True)
    result = subprocess.run(list(map(str, argv)), cwd=FW, check=False)
    if result.returncode:
        raise RuntimeError('SAFE STOP: command failed: ' + ' '.join(map(str,argv)))

def main():
    if os.name != 'nt':
        raise RuntimeError('Windows native PlatformIO required; NO FLASH')
    check_patch()
    print('Verified release files:', check_sources(), flush=True)
    pio = shutil.which('pio')
    if not pio:
        fallback = Path.home() / '.platformio' / 'penv' / 'Scripts' / 'pio.exe'
        if fallback.is_file():
            pio = str(fallback)
    if not pio:
        raise RuntimeError('PlatformIO not found')
    run([pio,'pkg','install','-e',ENV])
    run([sys.executable,str(ROOT/'patch_m5gfx_r60.py')])
    run([pio,'run','-e',ENV])
    check_sources()
    run([sys.executable,str(ROOT/'patch_m5gfx_r60.py')])
    image=FW/'.pio'/'build'/ENV/'firmware.bin'
    if not image.is_file() or not 100 < image.stat().st_size <= 0x800000 or image.read_bytes()[0] != 0xE9:
        raise RuntimeError('SAFE STOP: invalid app image')
    print('BUILD PASS NO FLASH: firmware.bin',image, 'SHA-256',sha256(image),flush=True)

if __name__ == '__main__':
    try:main()
    except Exception as exc:
        print('SAFE STOP / NO FLASH:',exc,flush=True)
        sys.exit(2)

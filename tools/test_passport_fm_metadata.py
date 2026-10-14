#!/usr/bin/env python3
"""Compile and exercise bounded production FM metadata parsing without networking."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', type=Path)
    ap.add_argument('--cjson-dir', type=Path)
    args = ap.parse_args()
    cc = shutil.which(os.environ.get('CC', 'gcc'))
    if not cc:
        raise SystemExit('gcc unavailable; set CC')
    candidates = [args.cjson_dir, Path(os.environ.get('IDF_PATH', '')) / 'components/json/cJSON', ROOT.parent / '.tools/esp-idf-v5.5.3/components/json/cJSON']
    cj = next((p for p in candidates if p and (p / 'cJSON.c').is_file()), None)
    if cj is None:
        raise SystemExit('cJSON unavailable; set IDF_PATH or --cjson-dir')
    build = ROOT / 'build/host'
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fm-metadata-', dir=build) as work:
        exe = Path(work) / ('metadata.exe' if os.name == 'nt' else 'metadata')
        subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-I', str(ROOT / 'main'), '-I', str(cj), str(ROOT / 'main/passport_fm_metadata.c'), str(ROOT / 'tests/test_passport_fm_metadata.c'), str(cj / 'cJSON.c'), '-lm', '-o', str(exe)], check=True)
        subprocess.run([str(exe), *([str(args.fixture.resolve())] if args.fixture else [])], check=True)
if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Compile bounded FM parsers; optionally exercise private real TS evidence."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ts", type=Path, action="append", default=[])
    parser.add_argument("--playlist", type=Path, action="append", default=[])
    args = parser.parse_args()
    cc = shutil.which(os.environ.get("CC", "gcc"))
    if not cc:
        raise SystemExit("gcc not available; set CC to a C11 host compiler")
    build = ROOT / "build/host"
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fm-stream-tests-", dir=build) as work:
        exe = Path(work) / ("test_fm.exe" if os.name == "nt" else "test_fm")
        subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                        "-I", str(ROOT / "main"), str(ROOT / "main/passport_fm_stream.c"),
                        str(ROOT / "tests/test_passport_fm_stream.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe), *(str(p.resolve()) for p in args.ts + args.playlist)], check=True)

if __name__ == "__main__":
    main()

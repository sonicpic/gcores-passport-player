#!/usr/bin/env python3
"""Run the production HTTP response validator without ESP-IDF/network I/O."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    if not compiler:
        raise SystemExit("gcc not found; set CC to a C11 host compiler")
    build = ROOT / "build/host"
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="audio-http-", dir=build) as work:
        binary = Path(work) / ("http.exe" if os.name == "nt" else "http")
        env = os.environ.copy()
        env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                   "-I" + str(ROOT / "main"), str(ROOT / "tests/test_passport_audio_http.c"),
                   str(ROOT / "main/passport_audio_http.c"), "-o", str(binary)]
        subprocess.run(command, cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)


if __name__ == "__main__":
    main()

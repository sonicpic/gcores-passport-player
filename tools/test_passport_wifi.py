#!/usr/bin/env python3
"""Build the actual Passport Wi-Fi implementation against a fault-injection host."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
def main():
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    if not compiler:
        raise SystemExit("gcc not found; set CC to a host C11 compiler")
    build = ROOT / "build" / "host"
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="passport-wifi-", dir=build) as work:
        binary = Path(work) / ("wifi.exe" if os.name == "nt" else "wifi")
        env = os.environ.copy()
        env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
                   "-I" + str(ROOT / "tests/wifi_port_stubs"), "-I" + str(ROOT / "main"),
                   str(ROOT / "tests/test_passport_wifi.c"), "-o", str(binary)]
        subprocess.run(command, cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compile the actual main.c handle() against recorded command dependencies."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extract_function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    # The selected function has no braces in strings/comments. Keep source text
    # verbatim so tests execute production dispatch, not a duplicated model.
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]


def main():
    compiler = shutil.which(os.environ.get("CC", "gcc"))
    if not compiler:
        raise SystemExit("gcc not found; set CC to a host C11 compiler")
    build = ROOT / "build" / "host"
    build.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "main/main.c").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="passport-main-", dir=build) as work:
        generated = Path(work) / "passport_main_handle.h"
        signatures = ["static bool active(", "static void enter_page(", "static bool have_current(",
                      "static bool can_return_player(",
                      "static void catalog_refresh_succeeded(",
                      "static void screen_set_dark(",
                      "static void seek_feedback_observe(", "static esp_err_t seek_with_feedback(", "static void seek_feedback_view(",
                      "static esp_err_t save_playback(",
                      "static void worker(", "static bool begin(", "static void auto_refresh_catalog(",
                      "static void render(", "static void play(", "static void handle(", "static bool finish_catalog_result("]
        generated.write_text("\n\n".join(extract_function(source, signature) for signature in signatures), encoding="utf-8")
        binary = Path(work) / ("main.exe" if os.name == "nt" else "main")
        env = os.environ.copy()
        env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
                   "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
                   "-I" + work, "-I" + str(ROOT / "tests/wifi_port_stubs"),
                   "-I" + str(ROOT / "main"), "-I" + str(ROOT / "components/bsp/include"),
                   str(ROOT / "tests/test_passport_main.c"), str(ROOT / "main/passport_logic.c"),
                   "-o", str(binary)]
        subprocess.run(command, cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)


if __name__ == "__main__":
    main()

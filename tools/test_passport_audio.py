#!/usr/bin/env python3
"""Exercise the real audio worker's command dispatch with host I/O doubles."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def worker_commands(source):
    text = source.read_text(encoding="utf-8")
    constants_begin = text.index("#define URL_LIMIT")
    constants_end = text.index("#define NET_IDLE", constants_begin)
    grow_begin = text.index("static esp_err_t pcm_grow(")
    grow_end = text.index("\nstatic uint32_t generation_get", grow_begin)
    types_begin = text.index("typedef enum { CMD_PLAY")
    types_end = text.index("typedef struct {\n    char mime", types_begin)
    worker = text.index("static void playback_worker(")
    begin = text.index("        if (xQueueReceive(s_commands", worker)
    guard = "        if (!running || paused) continue;"
    end = text.index(guard, begin) + len(guard)
    pcm_begin = text.index("        have_output = true;", worker)
    pcm_end = text.index("        net_status_t net = network_status();", pcm_begin)
    retry_begin = text.index("        if (!raw.consumed && !out.decoded_size && r ==", worker)
    retry_end = text.index("        if (raw.consumed > raw.len", retry_begin)
    cleanup_begin = text.index("failed:\n", worker) + len("failed:\n")
    cleanup_end = text.index("\n    }\n}\n\nstatic esp_err_t enqueue", cleanup_begin)
    network_begin = text.index("static esp_err_t network_start_at(")
    network_end = text.index("\nstatic void playback_worker", network_begin)
    network = text[network_begin:network_end].replace("network_start_at(", "actual_network_start_at(").replace("network_start(","actual_network_start(")
    return (text[types_begin:types_end], text[begin:end], text[pcm_begin:pcm_end],
            text[constants_begin:constants_end], text[grow_begin:grow_end],
            text[retry_begin:retry_end], text[cleanup_begin:cleanup_end], network)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=ROOT / "main/passport_audio.c")
    args = parser.parse_args()
    cc = shutil.which(os.environ.get("CC", "gcc"))
    if not cc:
        raise SystemExit("gcc not available; set CC to a C11 host compiler")
    types, commands, pcm, constants, grow, retry, cleanup, network = worker_commands(args.source)
    build = ROOT / "build/host"
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="audio-command-tests-", dir=build) as work:
        work = Path(work)
        (work / "audio_command_types.h").write_text(types, encoding="utf-8")
        (work / "audio_worker_commands.h").write_text(commands, encoding="utf-8")
        (work / "audio_pcm_output.h").write_text(pcm, encoding="utf-8")
        (work / "audio_buffer_constants.h").write_text(constants, encoding="utf-8")
        (work / "audio_pcm_grow.h").write_text(grow, encoding="utf-8")
        (work / "audio_pcm_retry.h").write_text(retry, encoding="utf-8")
        (work / "audio_worker_cleanup.h").write_text(cleanup, encoding="utf-8")
        (work / "audio_network_start.h").write_text(network, encoding="utf-8")
        source_text=args.source.read_text(encoding="utf-8")
        (work / "audio_publish.h").write_text(source_text[source_text.index("static void publish("):source_text.index("static void quiescent_set(")],encoding="utf-8")
        (work / "audio_enqueue.h").write_text(source_text[source_text.index("static esp_err_t enqueue("):source_text.index("esp_err_t passport_audio_play(")],encoding="utf-8")
        output = work / ("audio_commands.exe" if os.name == "nt" else "audio_commands")
        env = os.environ.copy()
        env["PATH"] = str(Path(cc).parent) + os.pathsep + env.get("PATH", "")
        command = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                   "-I" + str(ROOT / "tests/audio_stubs"), "-I" + str(ROOT / "main"),
                   "-I" + str(work), str(ROOT / "tests/test_passport_audio_commands.c"),
                   str(ROOT / "main/passport_mp3_seek.c"),
                   "-o", str(output)]
        subprocess.run(command, cwd=ROOT, env=env, check=True)
        subprocess.run([str(output)], cwd=ROOT, env=env, check=True)
        seek_output=work / ("mp3_seek.exe" if os.name=="nt" else "mp3_seek")
        subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-UNDEBUG",
                        "-I"+str(ROOT / "main"),str(ROOT / "tests/test_passport_mp3_seek.c"),
                        str(ROOT / "main/passport_mp3_seek.c"),"-o",str(seek_output)],env=env,check=True)
        subprocess.run([str(seek_output)],cwd=ROOT,env=env,check=True)


if __name__ == "__main__":
    main()

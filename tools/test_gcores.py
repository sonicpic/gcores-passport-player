#!/usr/bin/env python3
"""Compile/run the upstream regression tests and Passport memory tests on Windows or POSIX."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "main" / "gcores"
SOURCE = [CPP / p for p in (
    "rss/RssParser.cpp", "rss/TextUtils.cpp", "timeline/TimelineParser.cpp",
    "network/HttpRange.cpp", "storage/CacheCodec.cpp")]

def catalog_function(signature):
    """Compile actual catalog functions isolated from ESP-IDF I/O headers."""
    source = (ROOT / "main/passport_catalog.cpp").read_text(encoding="utf-8")
    begin = source.index(signature)
    body = source.index("{", begin)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]

def main():
    cxx = shutil.which(os.environ.get("CXX", "g++"))
    if not cxx:
        raise SystemExit("g++ not available; set CXX to a C++17 host compiler")
    build = ROOT / "build" / "host"
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gcores-tests-", dir=build) as work:
        metadata_header = Path(work) / "catalog_metadata_under_test.h"
        metadata_header.write_text(
            'namespace catalog_test {\n'
            'static const gpp::PodcastChannel kChannels[] = {{"games", "", "", "", ""}};\n'
            + catalog_function("void clear_extra_metadata(") + '\n}\n', encoding="utf-8")
        api_header = Path(work) / "catalog_api_under_test.h"
        api_header.write_text(
            '#include <array>\n#include <cstring>\n#include "passport_catalog.h"\n'
            'namespace catalog_api_test {\n'
            'static std::array<std::vector<gpp::PodcastEpisode>, PASSPORT_CHANNEL_COUNT> episodes;\n'
            'static unsigned lock_count=0;\nstruct Lock { Lock() { ++lock_count; } };\n'
            + catalog_function("bool valid_channel(") + '\n'
            + catalog_function("bool valid_episode(") + '\n'
            + catalog_function('extern "C" int passport_catalog_find_episode_by_identity(').replace('extern "C" ', '', 1) + '\n'
            + catalog_function('extern "C" bool passport_catalog_current_chapter(').replace('extern "C" ', '', 1)
            + '\n}\n', encoding="utf-8")
        env = os.environ.copy()
        env["PATH"] = str(Path(cxx).parent) + os.pathsep + env.get("PATH", "")
        common = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DGPP_NATIVE_TEST=1",
                  "-finput-charset=UTF-8", "-fexec-charset=UTF-8", "-I" + str(CPP), "-I" + work,
                  "-I" + str(ROOT / "main"), "-I" + str(ROOT / "tests/wifi_port_stubs")]
        for name in ("test_core", "test_port"):
            output = Path(work) / (name + (".exe" if os.name == "nt" else ""))
            command = common + [str(ROOT / "tests/gcores" / (name + ".cpp"))] + [str(s) for s in SOURCE] + ["-o", str(output)]
            subprocess.run(command, cwd=ROOT, env=env, check=True)
            subprocess.run([str(output)], cwd=ROOT, env=env, check=True)
    print("Gcores host tests: PASS (upstream regressions, 6-KiB packed timeline and zero-allocation actual catalog queries)")

if __name__ == "__main__":
    main()

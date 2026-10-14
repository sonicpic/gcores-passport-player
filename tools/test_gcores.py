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
    "network/HttpRange.cpp", "storage/CacheCodec.cpp", "storage/FlashCatalog.cpp")]

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
        api_functions = (
            "bool valid_channel(", "void from_record(", "bool valid_snapshot(",
            "bool same_identity(", "int find_locked(", "size_t visible_count(", "bool valid_episode(",
            "bool get_episode_locked(", "bool timeline_matches(", "size_t packed_count(",
            "bool packed_chapter(", 'extern "C" int passport_catalog_episode_count(',
            'extern "C" bool passport_catalog_get_episode(',
            'extern "C" int passport_catalog_find_episode_by_identity(',
            'extern "C" esp_err_t passport_catalog_set_playback(',
            'extern "C" size_t passport_catalog_chapter_count_by_identity(',
            'extern "C" bool passport_catalog_get_chapter_by_identity(',
            'extern "C" bool passport_catalog_current_chapter_by_identity(',
        )
        api_header.write_text(
            '#include <array>\n#include <cstring>\n#include "passport_catalog.h"\n'
            '#ifndef ESP_ERR_INVALID_ARG\n#define ESP_ERR_INVALID_ARG 0x102\n#endif\n'
            '#include "flash_fixture.h"\n'
            'namespace catalog_api_test {\n'
            'static gpp::FlashCatalog catalog;\n'
            'static gpp::FlashCatalogRecord io_record;\n'
            'static std::array<passport_episode_t,PASSPORT_EPISODE_PAGE_SIZE> page;\n'
            'static int page_channel=-1,page_start=0,page_count=0;\n'
            'static passport_episode_t playback{};\nstatic char playback_chapters[1025]{};\n'
            'static int playback_channel=-1,playback_index=-1;\n'
            'static std::string timeline_packed;\nstatic char timeline_guid[257]{},timeline_url[1025]{};\n'
            'static int timeline_channel=-1;\n'
            'static unsigned lock_count=0;\nstruct Lock { Lock() { ++lock_count; } };\n'
            + '\n'.join(catalog_function(sig).replace('extern "C" ', '', 1) for sig in api_functions)
            + '\n}\n', encoding="utf-8")
        env = os.environ.copy()
        env["PATH"] = str(Path(cxx).parent) + os.pathsep + env.get("PATH", "")
        common = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DGPP_NATIVE_TEST=1",
                  "-finput-charset=UTF-8", "-fexec-charset=UTF-8", "-I" + str(CPP), "-I" + work,
                  "-I" + str(ROOT / "main"), "-I" + str(ROOT / "tests/wifi_port_stubs"),
                  "-I" + str(ROOT / "tests/gcores")]
        for name in ("test_core", "test_port", "test_flash_catalog", "test_catalog_runtime"):
            output = Path(work) / (name + (".exe" if os.name == "nt" else ""))
            flags = (["-I" + str(ROOT / "tests/catalog_stubs")] if name == "test_catalog_runtime" else [])
            command = [cxx] + flags + common[1:] + [str(ROOT / "tests/gcores" / (name + ".cpp"))] + [str(s) for s in SOURCE] + ["-o", str(output)]
            subprocess.run(command, cwd=ROOT, env=env, check=True)
            subprocess.run([str(output)], cwd=ROOT, env=env, check=True)
            if name == "test_catalog_runtime":
                subprocess.run([str(output), "missing-partition"], cwd=ROOT, env=env, check=True)
    print("Gcores host tests: PASS (RSS, transactional 5x100 Flash pages, stable playback identity and zero-allocation catalog queries)")

if __name__ == "__main__":
    main()

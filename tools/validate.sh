#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python_bin="${PYTHON:-python3}"
cc_bin="${CC:-cc}"
if ! command -v "${python_bin}" >/dev/null 2>&1; then
    python_bin="python"
fi
if ! command -v "${cc_bin}" >/dev/null 2>&1; then
    cc_bin="gcc"
fi

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    "${python_bin}" tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    mkdir -p build/host
    test_dir="$(mktemp -d "${repo_root}/build/host/ai-passport-host-tests.XXXXXX")"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_passport_logic.c main/passport_logic.c \
        -o "${test_dir}/test_passport_logic"
    "${test_dir}/test_passport_logic"
    "${python_bin}" tools/test_gcores.py
    "${python_bin}" tools/test_passport_wifi.py
    "${python_bin}" tools/test_passport_fm.py
    "${python_bin}" tools/test_passport_fm_client.py
    "${python_bin}" tools/test_passport_fm_metadata.py
    "${python_bin}" tools/test_passport_fm_names.py
    "${python_bin}" tools/test_passport_audio.py
    "${python_bin}" tools/test_passport_audio_http.py
    "${python_bin}" tools/test_passport_main.py
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Itests/history_stubs -Imain \
        tests/test_passport_history.c main/passport_history.c \
        -o "${test_dir}/test_passport_history"
    "${test_dir}/test_passport_history"
    "${python_bin}" assets/fonts/verify-font.py
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${cc_bin}" -std=c11 -Wall -Wextra -Werror \
            -O2 -flto -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" -Wl,--gc-sections \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 "${python_bin}" tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 "${python_bin}" tests/test_key_wake_contract.py
    PYTHONDONTWRITEBYTECODE=1 "${python_bin}" tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 "${python_bin}" tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 "${python_bin}" tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 "${python_bin}" tests/test_install_passport_skills.py
    case "${test_dir}" in
        "${repo_root}/build/host/ai-passport-host-tests."*) rm -rf -- "${test_dir}" ;;
        *) echo "Unexpected host-test cleanup path: ${test_dir}" >&2; return 1 ;;
    esac
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*)
            pwsh -NoProfile -File tools/validate-windows.ps1 -Mode firmware
            return
            ;;
    esac

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    "${python_bin}" tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 "${python_bin}" tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac

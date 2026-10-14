#!/usr/bin/env python3
"""Static contracts for the deep-sleep key-wake guard wiring in main.c."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError(f"function is unterminated: {name}")


class KeyWakeGuardWiringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.main = read("main/main.c")

    def test_callback_consumes_the_guard_before_input_dispatch(self) -> None:
        # A release can arrive after the post-init voltage sample but before input
        # dispatch is ready. on_key must run the guard first so that release still
        # disarms it, instead of being discarded by the readiness early-return.
        if re.search(r"\binput_callback\s*\(", self.main):
            body = function_body(self.main, "input_callback")
            self.assertIn("key_guard_consume(", body)
            self.assertIn("event==BSP_BTN_RELEASE", body)
            self.assertLess(body.index("key_guard_consume("), body.index("xQueueSend("))
            self.assertRegex(body, r"if\s*\(key_guard_consume\([^;]+\)\)\s*return;")
        else:
            body = function_body(self.main, "on_key")
            self.assertIn("key_guard_consume(", body)
            self.assertLess(body.index("key_guard_consume("), body.index("!s_input_ready"))

    def test_startup_samples_release_after_button_driver_initialization(self) -> None:
        # A release before the driver starts produces no delayed CLICK, so the
        # physical sample must immediately disarm both held and tail guards.
        body = function_body(self.main, "app_main")
        self.assertIn("key_guard_arm(", body)
        self.assertIn("key_guard_sync_released(", body)
        self.assertLess(body.index("key_guard_arm("), body.index("bsp_button_init("))
        self.assertLess(body.index("bsp_button_init("), body.index("key_guard_sync_released("))
        self.assertRegex(body, r"key_guard_sync_released\([^;]*bsp_button_read_mv\(\)\s*>=\s*BSP_BTN_MV_RELEASED_MIN")


if __name__ == "__main__":
    unittest.main()

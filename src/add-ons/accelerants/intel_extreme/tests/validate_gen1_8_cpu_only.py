#!/usr/bin/env python3
"""Static policy checks for Intel Extreme Gen1–Gen8 CPU-only 2D.

This validates source invariants only. It does not compile Haiku, launch app_server,
or prove software rendering works on physical hardware.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[5]
ACCEL = ROOT / "src/add-ons/accelerants/intel_extreme"
DRIVER = ROOT / "src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp"


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def function_body(source, signature):
    start = source.find(signature)
    require(start >= 0, f"missing function: {signature}")
    opening = source.find("{", start)
    require(opening >= 0, f"missing body: {signature}")
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:i]
    raise AssertionError(f"unterminated function: {signature}")


def case_body(source, feature):
    start = source.find(f"case {feature}:")
    require(start >= 0, f"missing hook case {feature}")
    next_case = source.find("\n\t\tcase ", start + 1)
    return source[start:next_case if next_case >= 0 else len(source)]


def main():
    hooks = (ACCEL / "hooks.cpp").read_text(encoding="utf-8")
    accelerant = (ACCEL / "accelerant.cpp").read_text(encoding="utf-8")
    mode = (ACCEL / "mode.cpp").read_text(encoding="utf-8")
    dpms = (ACCEL / "dpms.cpp").read_text(encoding="utf-8")
    cursor = (ACCEL / "cursor.cpp").read_text(encoding="utf-8")
    driver = DRIVER.read_text(encoding="utf-8")

    drawing_hooks = {
        "B_SCREEN_TO_SCREEN_BLIT": "intel_screen_to_screen_blit",
        "B_FILL_RECTANGLE": "intel_fill_rectangle",
        "B_INVERT_RECTANGLE": "intel_invert_rectangle",
        "B_FILL_SPAN": "intel_fill_span",
    }
    for feature, implementation in drawing_hooks.items():
        body = case_body(hooks, feature)
        require("return NULL;" in body, f"{feature} must fall back to CPU drawing")
        require(f"(void*){implementation}" not in body,
                f"{feature} must not expose {implementation}")
    require("Gen1 through Gen12" in hooks,
            "CPU-only drawing policy must explicitly cover all generations")
    print("PASS: all four hardware 2D drawing hooks are withheld")

    # The general display pipeline remains exposed while drawing falls back to
    # app_server's software rasterizer.
    required_hooks = {
        "B_SET_DISPLAY_MODE": "intel_set_display_mode",
        "B_GET_DISPLAY_MODE": "intel_get_display_mode",
        "B_GET_FRAME_BUFFER_CONFIG": "intel_get_frame_buffer_config",
        "B_DPMS_CAPABILITIES": "intel_dpms_capabilities",
        "B_DPMS_MODE": "intel_dpms_mode",
        "B_SET_DPMS_MODE": "intel_set_dpms_mode",
        "B_SET_CURSOR_SHAPE": "intel_set_cursor_shape",
        "B_MOVE_CURSOR": "intel_move_cursor",
        "B_SHOW_CURSOR": "intel_show_cursor",
    }
    for feature, implementation in required_hooks.items():
        body = case_body(hooks, feature)
        require(f"(void*){implementation}" in body,
                f"{feature} must retain display/cursor capability")
    require("set_frame_buffer_registers(" in mode
            and "set_frame_buffer_base();" in mode,
            "mode path must program framebuffer scanout")
    require("B_DPMS_ON | B_DPMS_SUSPEND | B_DPMS_STAND_BY | B_DPMS_OFF" in dpms,
            "DPMS must declare all four supported monitor power states")
    require("width > 64 || height > 64" in cursor,
            "hardware cursor must reject unsupported dimensions safely")
    print("PASS: modeset, framebuffer, DPMS, and cursor hooks remain exposed")

    # Device-table evidence: Gen1/i740 is absent; listed target families start
    # with i830/i845 (Gen2) and extend through Broadwell (Gen8).
    require("i830GM" in driver and "i845G" in driver,
            "expected Gen2 device families are missing from the driver table")
    require("Broadwell" in driver,
            "expected Gen8 Broadwell devices are missing from the driver table")
    require(not re.search(r'\{\s*0x(?:08\w\w|09\w\w)\s*,[^\n]*i740', driver, re.I),
            "unexpected i740/Gen1 device-table entry; reassess the scope document")
    require("i740" not in driver.lower(),
            "first-generation i740 is not expected to be supported by this driver")
    print("PASS: device-table scope is Gen2+; Gen1 is not claimed as supported")

    # CPU-only drawing must not be mistaken for software scanout: framebuffer
    # memory is still allocated and programmed by the display driver.
    setter = function_body(mode, "intel_set_display_mode(")
    require("intel_allocate_memory(" in setter and "set_frame_buffer_base();" in setter,
            "mode-setting must allocate a framebuffer and program scanout")
    init = function_body(accelerant, "intel_init_accelerant(")
    require("probe_ports();" in init and "create_mode_list();" in init,
            "accelerant initialization must still probe outputs and build modes")
    print("PASS: display scanout remains hardware-backed while 2D drawing is CPU-only")
    print("NOTE: static checks only; no Haiku build or physical hardware test performed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)

#!/usr/bin/env python3
"""Static checks for Intel Extreme CPU-only 2D and Gen10–12 fail-closed policy.

These checks inspect source-level guards only. They are not a build, GPU test,
or proof that the hardware reset protocol is correct.
"""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[5]
ENGINE_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/engine.cpp"
KERNEL_PATH = ROOT / "src/add-ons/kernel/drivers/graphics/intel_extreme/intel_extreme.cpp"
DRIVER_PATH = ROOT / "src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp"
BRINGUP_PATH = ROOT / "src/add-ons/kernel/drivers/graphics/intel_extreme/GEN10_12_BCS_BRINGUP.md"
OVERLAY_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/overlay.cpp"
PORTS_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/Ports.cpp"
PLL_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/TigerLakePLL.cpp"
HOOKS_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/hooks.cpp"
MODE_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/mode.cpp"
CURSOR_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/cursor.cpp"
MEMORY_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/memory.cpp"
ACCELERANT_HEADER_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/accelerant.h"
DPMS_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/dpms.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function/signature: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing function body: {signature}")

    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function body: {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    engine = ENGINE_PATH.read_text(encoding="utf-8")
    kernel = KERNEL_PATH.read_text(encoding="utf-8")
    driver = DRIVER_PATH.read_text(encoding="utf-8")
    bringup = BRINGUP_PATH.read_text(encoding="utf-8")
    overlay = OVERLAY_PATH.read_text(encoding="utf-8")
    ports = PORTS_PATH.read_text(encoding="utf-8")
    pll = PLL_PATH.read_text(encoding="utf-8")
    hooks = HOOKS_PATH.read_text(encoding="utf-8")
    mode = MODE_PATH.read_text(encoding="utf-8")
    cursor = CURSOR_PATH.read_text(encoding="utf-8")
    memory = MEMORY_PATH.read_text(encoding="utf-8")
    accelerant_header = ACCELERANT_HEADER_PATH.read_text(encoding="utf-8")
    dpms = DPMS_PATH.read_text(encoding="utf-8")

    # DPMS must expose all four implemented states and reject invalid input
    # before changing shared state or programming display power registers.
    dpms_setter = function_body(dpms, "intel_set_dpms_mode(")
    require(
        "return B_DPMS_ON | B_DPMS_SUSPEND | B_DPMS_STAND_BY | B_DPMS_OFF;" in dpms,
        "DPMS capabilities must include On, Standby, Suspend, and Off",
    )
    invalid_mode_guard = dpms_setter.find("if (mode != B_DPMS_ON")
    shared_state_update = dpms_setter.find("gInfo->shared_info->dpms_mode = mode;")
    power_transition = dpms_setter.find("set_display_power_mode(mode);")
    require(
        invalid_mode_guard >= 0
        and "return B_BAD_VALUE;" in dpms_setter[invalid_mode_guard:shared_state_update]
        and power_transition > invalid_mode_guard
        and shared_state_update > power_transition,
        "invalid DPMS modes must be rejected before hardware or shared-state changes",
    )

    # Legacy LVDS status polling must be bounded and DPMS failures propagated.
    lvds_panel = function_body(dpms, "enable_lvds_panel(")
    require(
        "elapsed <= 1000" in lvds_panel
        and "return B_TIMED_OUT;" in lvds_panel,
        "LVDS DPMS polling must time out rather than hang indefinitely",
    )
    power_signature = dpms.find("set_display_power_mode(uint32 mode)")
    power_end = dpms.find("uint32\nintel_dpms_capabilities", power_signature)
    power_function = dpms[power_signature:power_end]
    require(
        power_signature >= 0
        and "status_t" in dpms[power_signature - 20:power_signature]
        and "return status;" in power_function
        and "return B_OK;" in power_function,
        "DPMS power transitions must return panel transition failures",
    )
    require(
        "extern status_t set_display_power_mode(uint32 mode);" in accelerant_header,
        "DPMS power transition declaration must return status",
    )

    # Cursor coordinates must subtract each axis's own hotspot.
    cursor_move = function_body(cursor, "move_cursor(int32 x, int32 y)")
    cursor_shape = function_body(cursor, "intel_set_cursor_shape(")
    cursor_show = function_body(cursor, "intel_show_cursor(")
    cursor_offsets = function_body(cursor, "get_cursor_pipe_offsets(")
    require(
        "x -= gInfo->shared_info->cursor_hot_x;" in cursor_move
        and "y -= gInfo->shared_info->cursor_hot_y;" in cursor_move,
        "hardware cursor position must apply the correct X and Y hotspot",
    )
    require(
        "move_cursor(x, y);" in cursor_shape,
        "hotspot changes must preserve signed off-screen cursor coordinates",
    )

    # Gen12 must not advertise the legacy cursor register path before the
    # per-pipe cursor plane and register mapping have been audited.
    cursor_hook_start = hooks.find("/* cursor management */")
    cursor_hook_end = hooks.find("/* engine/synchronization */", cursor_hook_start)
    require(
        cursor_hook_start >= 0 and cursor_hook_end > cursor_hook_start
        and hooks[cursor_hook_start:cursor_hook_end].count("Generation() < 12") >= 3,
        "legacy cursor hooks must remain disabled on Gen12+",
    )
    require(
        "width == 0" in cursor_shape and "height == 0" in cursor_shape
        and "andMask == NULL" in cursor_shape and "xorMask == NULL" in cursor_shape,
        "cursor shape must reject empty dimensions and null masks",
    )

    require(
        "hardwareCursor && info.device_type.Generation() < 12" in kernel
        and "Gen12+ hardware cursor remains disabled" in kernel,
        "kernel must not allocate the legacy cursor buffer for unvalidated Gen12+",
    )
    require(
        "info.shared_info->cursor_buffer_offset = (uint32)offset;" in kernel
        and "offset > info.shared_info->graphics_memory_size" in kernel
        and "B_PAGE_SIZE > info.shared_info->graphics_memory_size - offset" in kernel
        and "info.shared_info->cursor_buffer_offset = 0;" in kernel,
        "legacy cursor aperture offset must be validated and cleared on allocation failure",
    )

    require(
        all(token in cursor_offsets for token in (
            "case INTEL_PIPE_A:", "case INTEL_PIPE_B:", "case INTEL_PIPE_C:",
            "case INTEL_PIPE_D:", "offsets[count++] = offset;",
        ))
        and "INTEL_CURSOR_POSITION + pipeOffsets[0]" in cursor_shape,
        "legacy cursor register access must follow assigned display pipes",
    )
    require(
        "memset(data, 0, 64 * 16);" in cursor_shape
        and "memset(data + 16 * y, 0xff, 8);" in cursor_shape
        and "hotX >= width || hotY >= height" in cursor_shape
        and "gInfo->shared_info->cursor_visible ? CURSOR_ENABLED : 0" in cursor_shape
        and "uint32 encodedX = (uint32)x;" in cursor_move
        and "uint32 encodedY = (uint32)y;" in cursor_move
        and "x = CURSOR_POSITION_MASK;" in cursor_move
        and "y = CURSOR_POSITION_MASK;" in cursor_move
        and "if (!isVisible)" in cursor_show
        and "write32(INTEL_CURSOR_CONTROL + pipeOffsets[i], 0);" in cursor_show,
        "cursor shape updates must clear stale mask data and preserve hidden state",
    )

    # DPLL transitions must time out rather than hang, and the DDI modeset
    # caller must stop when PLL programming fails.
    require(
        "while ((read32(DPLL_ENABLE)" not in pll
        and "B_TIMED_OUT" in pll
        and "wait_for_register(DPLL_ENABLE, TGL_DPLL_LOCK, false, 100000)" in pll
        and "wait_for_register(DPLL_ENABLE, TGL_DPLL_POWER_STATE, true, 100000)" in pll
        and "wait_for_register(DPLL_ENABLE, TGL_DPLL_LOCK, true, 100000)" in pll
        and "Undo the power request if hardware did not acknowledge it" in pll
        and "Do not leave an unlocked PLL enabled" in pll,
        "Gen12 PLL transitions must be bounded and failed enable attempts rolled back",
    )
    require(
        "status_t pllStatus = ProgramPLL(" in ports and "return pllStatus;" in ports,
        "DDI mode-setting must propagate PLL programming failures",
    )

    require(
        "Pdiv != 2 && Pdiv != 3 && Pdiv != 5 && Pdiv != 7" in pll
        and "ref_khz != 19200 && ref_khz != 24000" in pll
        and "if (dco_frac >= (1 << 15))" in pll
        and "dco_int++;" in pll and "dco_frac = 0;" in pll
        and "!(dco >= 7998.0f && dco <= 10000.0f)" in pll
        and "float afeclk = (5.0f * freq) / 1000.0f;" in pll
        and "TGL_DPLL_DCO_INTEGER_SHIFT" not in pll,
        "PLL inputs and fractional carry must be validated before MMIO",
    )

    ddi_mode = function_body(ports, "DigitalDisplayInterface::SetDisplayMode(")
    require(
        "PortIndex() != INTEL_PORT_A" in ddi_mode
        and "PortIndex() != INTEL_PORT_B" in ddi_mode
        and "fPipe->Index() != INTEL_PIPE_A" in ddi_mode
        and "fPipe->Index() != INTEL_PIPE_B" in ddi_mode
        and "return B_NOT_SUPPORTED;" in ddi_mode
        and "PortIndex() == INTEL_PORT_B ? 1 : 0" in ddi_mode,
        "Gen12 must route DDI A/B to matching combo DPLLs and reject unimplemented PHY ports",
    )
    require(
        "status_t linkStatus = _SetPortLinkGen8(" in ddi_mode
        and "return linkStatus;" in ddi_mode,
        "DDI link M/N errors must propagate to the modeset caller",
    )

    require(
        "validate_gen12_dp_bandwidth(fPipe, hardwareTarget)" in ddi_mode
        and "mode != PIPE_DDI_MODE_DP_SST && mode != PIPE_DDI_MODE_DP_MST" in ports,
        "Gen12 DP bandwidth and reserved mode encodings must be checked before modeset writes",
    )
    require(
        "case PIPE_DDI_DP_WIDTH_1:" in ports
        and "case PIPE_DDI_DP_WIDTH_2:" in ports
        and "case PIPE_DDI_DP_WIDTH_4:" in ports
        and "Mode exceeds fixed Gen12 DP link capacity" in ports,
        "DDI lane encodings and fixed-rate Gen12 DP bandwidth must be validated",
    )

    require(
        "config &= ~TGL_DPCLKA_DDIA_CLOCK_SELECT;" in ports
        and "config &= ~TGL_DPCLKA_DDIB_CLOCK_SELECT;" in ports
        and "config &= TGL_DPCLKA_DDIA_CLOCK_SELECT;" not in ports
        and "config &= TGL_DPCLKA_DDIB_CLOCK_SELECT;" not in ports,
        "DDI clock routing must preserve unrelated CFGCR0 fields",
    )

    # EDID reads must attempt the alternate transport after either bus
    # setup or the EDID transaction fails; DDI/AUX selection is VBT-sensitive.
    edid_reader = function_body(ports, "Port::GetEDID(")
    require(
        "status_t status = SetupI2c(&bus);" in edid_reader
        and "ddc2_read_edid1(&bus, &fEDIDInfo, NULL, NULL)" in edid_reader
        and "if (status != B_OK)" in edid_reader
        and "status_t fallbackStatus = SetupI2cFallback(&bus);" in edid_reader
        and "if (fallbackStatus == B_OK)" in edid_reader,
        "EDID reads must attempt fallback transport after primary setup/read failure",
    )
    dp_setup = function_body(ports, "DisplayPort::SetupI2c(")
    dp_fallback = function_body(ports, "DisplayPort::SetupI2cFallback(")
    ddi_fallback = function_body(ports, "DigitalDisplayInterface::SetupI2cFallback(")
    require(
        "return _SetupDpAuxI2c(bus);" in dp_setup
        and "return _SetupDpAuxI2c(bus);" in dp_fallback
        and "_IsDisplayPortInVBT() && _IsHdmiInVBT()" in ddi_fallback
        and "return Port::SetupI2c(bus);" in ddi_fallback,
        "DP AUX and VBT-selected DDI GMBUS fallback paths must remain explicit",
    )

    # EDID detection gets at most one automatic retry after a failed
    # read; repeated connection polling must not generate unbounded DDC/AUX IO.
    has_edid = function_body(ports, "Port::HasEDID(")
    get_edid = function_body(ports, "Port::GetEDID(")
    require(
        "fEDIDReadAttempts < 2" in has_edid
        and "GetEDID(NULL, fEDIDReadAttempts != 0)" in has_edid,
        "HasEDID must perform at most one bounded retry after an initial failure",
    )
    require(
        "if (fEDIDReadAttempts < 2)" in get_edid
        and "fEDIDReadAttempts++;" in get_edid,
        "EDID reads must account for bounded automatic retries",
    )

    # A physical unplug must reset the bounded retry budget so a later replug
    # can recover EDID; this must not turn ordinary polling into infinite I/O.
    dp_start = ports.find("bool\nDisplayPort::IsConnected()")
    hdmi_start = ports.find("bool\nHDMIPort::IsConnected()")
    require(dp_start >= 0 and hdmi_start >= 0,
        "could not locate digital connector detection functions")
    dp_connected = ports[dp_start:dp_start + 5000]
    hdmi_connected = ports[hdmi_start:hdmi_start + 3000]
    for name, body in (("DisplayPort", dp_connected), ("HDMI", hdmi_connected)):
        disconnected = body.find("DISPLAY_MONITOR_PORT_ENABLED) == 0")
        require(
            disconnected >= 0
            and "fEDIDState = B_NO_INIT;" in body[disconnected:disconnected + 500]
            and "fEDIDReadAttempts = 0;" in body[disconnected:disconnected + 500],
            f"{name} disconnect must reset EDID state and retry budget for replug",
        )

    # DisplayPort I2C chunks must not advance after exhausting all bounded
    # I2C-DEFER retries. Both write and read paths require an explicit ACK.
    aux_i2c = function_body(ports, "Port::_DpAuxSendReceive(")
    require(
        aux_i2c.count("bool acknowledged = false;") == 2
        and aux_i2c.count("if (!acknowledged)") == 2
        and aux_i2c.count("return B_IO_ERROR;") >= 4,
        "AUX I2C read/write chunks must fail if seven DEFER retries receive no ACK",
    )
    require(
        aux_i2c.count("case DP_AUX_I2C_REPLY_ACK:") == 2
        and aux_i2c.count("continue;") == 2
        and aux_i2c.count("attempt < 8") == 2,
        "AUX I2C retries must allow seven retries after the initial DEFER",
    )
    aux_native = function_body(ports, "Port::_DpAuxTransfer(dp_aux_msg*")
    require(
        "retry < 8" in aux_native,
        "native AUX transactions must allow seven retries after the initial DEFER",
    )
    require(
        "else if (result == 0)" in aux_native
        and "if (result != (ssize_t)message->size)" in aux_native,
        "AUX replies must contain a response and reads must reject short payloads",
    )

    # CPU-only 2D policy is generation-independent: every Intel 2D drawing
    # hook must be absent so app_server selects its software rasterizer.
    drawing_hooks = (
        ("B_SCREEN_TO_SCREEN_BLIT", "intel_screen_to_screen_blit"),
        ("B_FILL_RECTANGLE", "intel_fill_rectangle"),
        ("B_INVERT_RECTANGLE", "intel_invert_rectangle"),
        ("B_FILL_SPAN", "intel_fill_span"),
    )
    for feature, implementation in drawing_hooks:
        start = hooks.find(f"case {feature}:")
        require(start >= 0, f"missing drawing hook case: {feature}")
        next_case = hooks.find("\n\t\tcase ", start + 1)
        body = hooks[start:next_case if next_case >= 0 else len(hooks)]
        require(
            "return NULL;" in body and f"(void*){implementation}" not in body,
            f"{feature} must remain unavailable for CPU-only 2D rendering",
        )
    require(
        "CPU-only 2D policy" in hooks and "Gen2 through Gen12" in hooks,
        "hooks must document the all-generation software-rendering policy",
    )

    # If a mode change cannot allocate its new framebuffer, only resume
    # scanout after the fallback framebuffer is allocated and programmed.
    mode_start = mode.find("intel_set_display_mode(")
    alloc_start = mode.find(
        "if (intel_allocate_memory(bytesPerRow * target.virtual_height", mode_start
    )
    alloc_end = mode.find("// clear frame buffer before using it", alloc_start)
    require(
        mode_start >= 0 and alloc_start > mode_start and alloc_end > alloc_start,
        "could not locate framebuffer allocation failure path",
    )
    allocation_failure = mode[alloc_start:alloc_end]
    restored = allocation_failure.find("set_frame_buffer_base();")
    restored_power = allocation_failure.find("set_display_power_mode(sharedInfo.dpms_mode);")
    failed_restore = allocation_failure.find("sharedInfo.frame_buffer = 0;")
    require(
        restored >= 0 and restored_power > restored,
        "fallback scanout must resume only after the framebuffer base is restored",
    )
    mode_setter = function_body(mode, "intel_set_display_mode(")
    power_off_status = mode_setter.find(
        "status_t powerStatus = set_display_power_mode(B_DPMS_OFF);"
    )
    free_status = mode_setter.find("status_t freeStatus = B_OK;")
    free_failure = mode_setter.find("if (freeStatus != B_OK)", free_status)
    free_restore_dpms = mode_setter.find(
        "set_display_power_mode(sharedInfo.dpms_mode);", free_failure
    )
    new_framebuffer_alloc = mode_setter.find(
        "if (intel_allocate_memory(bytesPerRow * target.virtual_height", free_status
    )
    require(
        power_off_status >= 0 and free_status > power_off_status
        and "return powerStatus;" in mode_setter[power_off_status:free_status]
        and free_status >= 0 and free_failure > free_status
        and "if (sharedInfo.frame_buffer != 0)" in mode_setter[free_status:free_failure]
        and "freeStatus = intel_free_memory(sharedInfo.frame_buffer);" in mode_setter[free_status:free_failure]
        and free_restore_dpms > free_failure
        and new_framebuffer_alloc > free_restore_dpms,
        "mode switch must preserve framebuffer ownership and skip freeing a null buffer",
    )

    # After a port modeset failure, keep the display off. On success, publish
    # the new mode/base before DPMS re-enables any scanout plane, then re-apply
    # the base after programming pitch while the pipe is on.
    port_loop = mode_setter.find("status_t portStatus = B_OK;")
    failed_modeset = mode_setter.find("if (portStatus != B_OK)", port_loop)
    failed_return = mode_setter.find("return portStatus;", failed_modeset)
    failed_block = mode_setter[failed_modeset:failed_return]
    require(
        port_loop >= 0 and failed_modeset > port_loop
        and failed_return > failed_modeset
        and "set_display_power_mode(sharedInfo.dpms_mode);" not in failed_block
        and "releaseFailedBuffer" in failed_block
        and "fallbackStatus = fallbackSize != 0" in failed_block
        and "((size_t)-1) / sharedInfo.current_mode.virtual_height" in failed_block
        and "sharedInfo.frame_buffer = 0;" in failed_block,
        "failed modesets must keep DPMS off and recover framebuffer ownership",
    )
    # The success path occurs after the early failure return, so locate its
    # first base update after the explicit success log.
    success_log = mode_setter.find("Port configuration completed successfully!")
    pre_restore_base = mode_setter.find("set_frame_buffer_base();", success_log)
    restore_new_dpms = mode_setter.find(
        "powerStatus = set_display_power_mode(sharedInfo.dpms_mode);", pre_restore_base
    )
    row_pitch = mode_setter.find("write32(INTEL_DISPLAY_A_BYTES_PER_ROW", restore_new_dpms)
    post_pitch_base = mode_setter.find("set_frame_buffer_base();", row_pitch)
    require(
        success_log >= 0 and pre_restore_base > success_log
        and restore_new_dpms > pre_restore_base
        and row_pitch > restore_new_dpms and post_pitch_base > row_pitch,
        "successful modeset must program the new base before DPMS restore and re-latch after pitch",
    )
    require(
        failed_restore >= 0 and "sharedInfo.frame_buffer_offset = 0;" in allocation_failure,
        "failed fallback allocation must clear stale CPU framebuffer pointers",
    )

    require(
        "base) != B_OK)" in allocation_failure,
        "framebuffer allocation must treat every nonzero status as failure",
    )
    overlay_allocator = function_body(overlay, "intel_allocate_overlay_buffer(")
    require(
        overlay_allocator.count("if (status != B_OK)") == 2
        and "if (status < B_OK)" not in overlay_allocator,
        "overlay allocations must handle positive errno values as failures",
    )
    require(
        "if (width == 0 || height == 0)" in overlay_allocator
        and "((size_t)-1) / height" in overlay_allocator
        and "size_t bufferSize = (size_t)buffer->bytes_per_row * height;" in overlay_allocator,
        "overlay allocation must reject empty dimensions and size overflow",
    )

    free_memory = function_body(memory, "intel_free_memory(")
    require(
        "status_t" in memory[memory.find("status_t\nintel_free_memory"):memory.find("status_t\nintel_allocate_memory")]
        and "return errno;" in free_memory
        and "return B_OK;" in free_memory
        and "extern status_t intel_free_memory(addr_t base);" in accelerant_header,
        "graphics memory free must report ioctl failures to overlay lifetime management",
    )

    # Legacy overlay commands depend on the primary ring, which is not
    # initialized on Gen6+. Keep all overlay hooks unavailable there.
    overlay_gate = hooks.find("if (gInfo->shared_info->device_type.Generation() >= 6)")
    overlay_cases = (
        "B_OVERLAY_COUNT",
        "B_OVERLAY_SUPPORTED_SPACES",
        "B_OVERLAY_SUPPORTED_FEATURES",
        "B_ALLOCATE_OVERLAY_BUFFER",
        "B_RELEASE_OVERLAY_BUFFER",
        "B_GET_OVERLAY_CONSTRAINTS",
        "B_ALLOCATE_OVERLAY",
        "B_RELEASE_OVERLAY",
        "B_CONFIGURE_OVERLAY",
    )
    require(overlay_gate >= 0, "overlay hooks must be gated on Gen6+")
    overlay_gate_block = hooks[overlay_gate:hooks.find("\n\tswitch (feature) {", overlay_gate + 1)]
    require(
        all(case in overlay_gate_block for case in overlay_cases)
        and "return NULL;" in overlay_gate_block,
        "all legacy overlay hooks must fail closed on Gen6+",
    )

    # With every hardware drawing hook disabled, do not expose an engine
    # interface whose idle-wait/token semantics are incomplete or unused.
    engine_start = hooks.find("/* engine/synchronization */")
    drawing_start = hooks.find("/* 2D acceleration */", engine_start)
    require(
        engine_start >= 0 and drawing_start > engine_start,
        "could not locate engine/synchronization hook block",
    )
    engine_block = hooks[engine_start:drawing_start]
    for feature in (
        "B_ACCELERANT_ENGINE_COUNT",
        "B_ACQUIRE_ENGINE",
        "B_RELEASE_ENGINE",
        "B_WAIT_ENGINE_IDLE",
        "B_GET_SYNC_TOKEN",
        "B_SYNC_TO_TOKEN",
    ):
        require(
            f"case {feature}:" in engine_block,
            f"missing engine/synchronization hook case: {feature}",
        )
    require(
        "return NULL;" in engine_block
        and all(
            f"(void*){implementation}" not in engine_block
            for implementation in (
                "intel_accelerant_engine_count",
                "intel_acquire_engine",
                "intel_release_engine",
                "intel_wait_engine_idle",
                "intel_get_sync_token",
                "intel_sync_to_token",
            )
        ),
        "hardware engine/synchronization hooks must stay disabled in CPU-only mode",
    )

    engine_token = function_body(engine, "intel_acquire_engine(")
    require(
        "= {1, 0 /*B_2D_ACCELERATION*/, NULL}" in engine
        and "capabilities & ~sEngineToken.capabilities" in engine_token
        and "return B_NOT_SUPPORTED;" in engine_token,
        "accelerant must not advertise unsupported 2D engine capabilities",
    )

    selector = function_body(engine, "get_blitter_ring_buffer()")
    require(
        "Generation() >= 6" in selector and "return NULL;" in selector,
        "Gen6+ ring selector must fail closed",
    )

    setup = function_body(engine, "setup_ring_buffer(ring_buffer &ringBuffer")
    require(
        "Generation() >= 6" in setup
        and "return B_NOT_SUPPORTED;" in setup,
        "ring setup must reject Gen6+ before programming the ring",
    )

    # The Gen6+ rejection must precede lock setup and every MMIO access.
    # This keeps an unsupported generation from touching a possibly live ring.
    gen6_gate = setup.find("if (gInfo->shared_info->device_type.Generation() >= 6)")
    require(gen6_gate >= 0, "ring setup is missing the Gen6+ gate")
    for operation in ("init_lock(", "read32(", "write32("):
        operation_at = setup.find(operation)
        require(
            operation_at < 0 or gen6_gate < operation_at,
            f"Gen6+ setup gate must precede {operation}",
        )

    idle = function_body(engine, "wait_engine_idle_internal()")
    require(
        "Generation() >= 6" in idle and "return B_NOT_SUPPORTED;" in idle,
        "Gen6+ idle wait must not report unsupported completion as success",
    )

    sync = function_body(engine, "intel_get_sync_token(")
    require(
        "Generation() >= 6" in sync and "return B_NOT_SUPPORTED;" in sync,
        "Gen6+ sync-token creation must not advertise a fake completion fence",
    )

    for signature in (
        "intel_screen_to_screen_blit(",
        "intel_fill_rectangle(",
        "intel_invert_rectangle(",
        "intel_fill_span(",
    ):
        body = function_body(engine, signature)
        require(
            "get_blitter_ring_buffer()" in body and "if (ring == NULL)" in body,
            f"{signature} must not submit without a validated ring",
        )

    allocation_start = kernel.find(
        "// Gen6+ BCS submission is fail-closed until a generation-correct"
    )
    allocation_end = kernel.find("// Enable clock gating", allocation_start)
    require(
        allocation_start >= 0 and allocation_end > allocation_start,
        "could not locate the kernel BCS initialization gate",
    )
    bcs_init = kernel[allocation_start:allocation_end]
    require(
        "blitter.base = NULL;" in bcs_init
        and "blitter.register_base = 0;" in bcs_init
        and "intel_allocate_memory" not in bcs_init,
        "kernel initialization must leave Gen6+ BCS unallocated and unavailable",
    )

    # A quarantined ring must also prevent unmap_aperture(): the GART
    # manager destroys every tracked allocation when an aperture is unmapped.
    # Preserve the aperture if the legacy ring is enabled or freeing its ring
    # allocation fails, and make the unmap conditional on that decision.
    teardown = function_body(kernel, "intel_extreme_uninit(")
    quarantine_gate = teardown.find("if (quarantineAperture)")
    unmap_at = teardown.find("gGART->unmap_aperture(info.aperture);")
    else_at = teardown.find("} else {", quarantine_gate)
    mmio_guard = teardown.find("info.registers != NULL")
    first_read = teardown.find("read32(")
    require(
        "bool quarantineAperture = false;" in teardown
        and "INTEL_RING_BUFFER_ENABLED" in teardown
        and "quarantineAperture = true;" in teardown
        and mmio_guard >= 0
        and first_read > mmio_guard
        and quarantine_gate >= 0
        and else_at > quarantine_gate
        and unmap_at > else_at,
        "GART unmap must be conditional, and ring inspection must require mapped MMIO",
    )

    # Overlay GART allocations must not be reported as released if a free
    # fails. Retain the descriptor so ownership of the remaining allocation
    # is not lost and a later release can retry.
    release_overlay_buffer = function_body(overlay, "intel_release_overlay_buffer(")
    require(
        "status_t status = intel_free_memory(overlay->state_base);" in release_overlay_buffer
        and "status_t status = intel_free_memory(overlay->buffer_base);" in release_overlay_buffer
        and "if (status != B_OK)" in release_overlay_buffer
        and release_overlay_buffer.find("free(overlay);")
            > release_overlay_buffer.find("intel_free_memory(overlay->buffer_base);")
        and "return status;" in release_overlay_buffer,
        "overlay release must preserve ownership metadata when a GART free fails",
    )

    # Overlay shutdown currently submits asynchronous GPU commands but has
    # no host-observed completion contract before the backing allocation can
    # be freed. Keep this lifecycle blocker explicit until a real completion
    # and serialization implementation is reviewed.
    hide_overlay = function_body(overlay, "hide_overlay(void)")
    queue_destructor = function_body(engine, "QueueCommands::~QueueCommands()")
    require(
        "queue.PutWaitFor(COMMAND_WAIT_FOR_OVERLAY_FLIP);" in hide_overlay
        and "gInfo->current_overlay = NULL;" in hide_overlay
        and "write32(fRingBuffer.register_base + RING_BUFFER_TAIL," in queue_destructor
        and "QueueCommands" in bringup
        and "submission, not CPU-observed completion" in bringup
        and "The i965 path is distinct" in bringup
        and "intel_uninit_accelerant()" in bringup,
        "overlay shutdown completion and teardown limitations must remain documented",
    )

    # The PCI table is an explicit allowlist and must stop at i830/Gen2.
    # Reject regressions that re-enable pre-i830 i810/i815-era adapters.
    table_start = driver.find("kSupportedDevices[] = {")
    table_end = driver.find("};", table_start)
    require(
        table_start >= 0 and table_end > table_start,
        "could not locate the supported PCI device allowlist",
    )
    device_table = driver[table_start:table_end]
    require(
        '{0x3577, INTEL_GROUP_83x, "i830GM"}' in device_table
        and '{0x2562, INTEL_GROUP_83x, "i845G"}' in device_table,
        "minimum supported devices must begin with i830/i845 (Gen2)",
    )
    require(
        "i810" not in device_table.lower()
        and "i815" not in device_table.lower()
        and "0x7121" not in device_table.lower()
        and "0x1132" not in device_table.lower(),
        "pre-i830 Intel GPU support must remain absent from the PCI allowlist",
    )

    # Ensure representative Gen9–Gen12 families remain identified while
    # all drawing hooks stay CPU-only and Gen6+ engine submission fails closed.
    for pci_id, family in (
        ("0x1912", "Skylake Gen9"),
        ("0x5a52", "Cannon Lake Gen10"),
        ("0x8a51", "Ice Lake Gen11"),
        ("0x4c8a", "Rocket Lake Gen12"),
        ("0x46a0", "Alder Lake Gen12"),
    ):
        require(pci_id in device_table, f"missing {family} device ID {pci_id}")
    print("PASS: Gen9–Gen12 device families are listed for CPU-only 2D testing")
    print("PASS: pre-i830 GPUs remain excluded from the explicit PCI allowlist")

    # Keep the selected Gen10 review candidate explicit and disabled. A PCI
    # allowlist entry is identification only; it must not bypass the BCS gate.
    require(
        '{0x5a52, INTEL_MODEL_CNLM, "CannonLake GT2"}' in driver,
        "Cannon Lake Gen10 review candidate 0x5a52 is missing or changed",
    )
    require(
        "candidate for source review only" in bringup
        and "no Gen10 BCS implementation or reset routine is added" in bringup
        and "command submission remains disabled" in bringup,
        "Gen10 candidate must remain explicitly disabled pending PRM and hardware evidence",
    )

    # The same Generation() >= 6 guard applies to these numeric generations.
    for generation in (10, 11, 12):
        print(f"PASS: Gen{generation} remains behind the Gen6+ fail-closed gate")

    print("PASS: CPU-only 2D drawing and engine hooks remain disabled for Gen1–12")
    print("PASS: no Gen6+ command path or completion token is enabled by these guards")
    print("NOTE: static source checks only; no build or hardware recovery test performed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)

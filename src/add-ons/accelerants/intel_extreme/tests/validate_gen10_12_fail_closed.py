#!/usr/bin/env python3
"""Static regression checks for the Intel Extreme Gen10–12 fail-closed gate.

This checks source-level guards only. It is not a build, GPU test, or proof
that the hardware reset protocol is correct.
"""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[5]
ENGINE_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/engine.cpp"
KERNEL_PATH = ROOT / "src/add-ons/kernel/drivers/graphics/intel_extreme/intel_extreme.cpp"
DRIVER_PATH = ROOT / "src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp"
BRINGUP_PATH = ROOT / "src/add-ons/kernel/drivers/graphics/intel_extreme/GEN10_12_BCS_BRINGUP.md"
OVERLAY_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/overlay.cpp"
HOOKS_PATH = ROOT / "src/add-ons/accelerants/intel_extreme/hooks.cpp"


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
    hooks = HOOKS_PATH.read_text(encoding="utf-8")

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
        "CPU-only 2D policy" in hooks and "Gen1 through Gen12" in hooks,
        "hooks must document the all-generation software-rendering policy",
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

    print("PASS: no Gen6+ command path or completion token is enabled by these guards")
    print("NOTE: static source checks only; no build or hardware recovery test performed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)

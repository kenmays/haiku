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

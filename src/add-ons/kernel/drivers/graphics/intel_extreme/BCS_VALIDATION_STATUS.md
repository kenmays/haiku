# Intel Extreme BCS and GPU completion validation status

Branch: `work/intel-extreme-hd530-edid-2d`

This checklist records the safety gates that must be satisfied before enabling direct-ring 2D acceleration. It is not a claim that hardware execution has been validated.

## Confirmed implementation direction

- Keep CPU virtual pointers, physical backing addresses, and GPU GGTT offsets as distinct address domains.
- Validate ring and status-page offsets for alignment, aperture bounds, and hardware register width before programming registers.
- Publish ring-tail updates only after command writes are visible to the device; the current x86 path includes an `SFENCE` before MMIO tail publication.
- Fail closed when generation-specific ring setup or synchronization prerequisites are not met.
- Do not treat a CPU-side wait or a submitted command as proof of GPU completion.

## Latest code gate (2026-10-20)

- Gen6+ BCS allocation is now explicitly omitted during kernel initialization; shared ring fields are initialized as unavailable.
- Legacy Gen1–5 primary-ring allocation teardown now checks the hardware enable/start state before freeing; an allocation still named by an enabled ring is quarantined rather than freed.
- The accelerant refuses to select a Gen6+ BCS ring even if stale shared state contains a ring description.
- Ring setup rejects Gen6+ before touching the BCS registers. This covers Gen6/7 as well as Gen8–12 and avoids submitting commands without a completion fence and bounded recovery.
- These changes are a fail-closed safety measure, not implementation of BCS acceleration. Gen1–5 still needs a separate audit of completion-token semantics, ring shutdown, and allocation lifetime.
- No successful Haiku build, runtime test, or HD 530 hardware test has been established for this branch.

## Gen10–12 command and recovery validation

This is a **static fail-closed validation**, not hardware validation. The current implementation intentionally does not enable command submission or attempt recovery for these generations.

| Generation | Source-level result | Command path | Recovery result |
|---|---|---|---|
| Gen10 | Pass: covered by the `Generation() >= 6` guard | BCS ring selection returns unavailable; ring setup returns `B_NOT_SUPPORTED`; 2D hooks have no ring to submit to | No Gen10 reset/recovery sequence is implemented or invoked by this accelerant |
| Gen11 | Pass: covered by the same guard | Same fail-closed behavior; no BCS batch is published | No Gen11-specific reset sequence is implemented or invoked by this accelerant |
| Gen12 | Pass: covered by the same guard | Same fail-closed behavior; no BCS batch is published | No Gen12-specific reset sequence is implemented or invoked by this accelerant |

Static checks also confirm that kernel initialization leaves the Gen6+ BCS ring fields unavailable and does not allocate a BCS ring, and that engine-idle and sync-token APIs return `B_NOT_SUPPORTED` rather than claiming completion. The four command-producing hooks checked are screen-to-screen blit, fill rectangle, invert rectangle, and fill span.

A repeatable source-level regression check is provided at:
`src/add-ons/accelerants/intel_extreme/tests/validate_gen10_12_fail_closed.py`

Run it from any Haiku checkout with:
`python3 src/add-ons/accelerants/intel_extreme/tests/validate_gen10_12_fail_closed.py`

The source guards were checked against the current branch contents and all checks passed. The script itself has not been executed inside a Haiku checkout in this session; no Haiku build, emulator test, or Gen10/11/12 hardware test has been performed.

### Why a shared Gen9 recovery sequence is not acceptable

The Linux i915 reference driver selects a Gen8-family engine reset path for Gen8–10 and uses a distinct Gen11 reset implementation for Gen11 and later. This is evidence that reset behavior must be generation-aware, not proof that the same sequences can be ported directly into Haiku. References for further implementation review:

- Linux i915 reset implementation: https://linux.googlesource.com/linux/kernel/git/torvalds/linux/%2B/1f57f68c4dd101e5e8ffc9ffa6428f45bcdd776a/drivers/gpu/drm/i915/gt/intel_reset.c
- Intel Graphics Programmer's Reference Manuals: https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/rocket-lake.html
- Linux i915 engine and command-submission overview: https://docs.kernel.org/gpu/i915.html

Until per-generation command semantics, status-page writes, reset register protocol, reset completion polling, and post-reset resource quiescence are independently reviewed and tested, Gen10–12 must remain disabled for hardware 2D acceleration.

## Required before Gen9 BCS can be enabled

1. **Allocation ownership and teardown**
   - Trace every GART allocation from successful allocation to the one matching free.
   - Preserve the exact allocation base returned by GART; do not reconstruct it from a physical address or a narrowed GGTT offset.
   - Prove that the engine is quiescent before freeing ring, status-page, or command-referenced storage.
   - Make partial initialization unwind each successfully acquired resource exactly once.

2. **Ring initialization and publication**
   - Confirm generation-specific ring control, head/tail, ring size, and enable ordering against the applicable hardware documentation.
   - Validate all ring bounds and wraparound arithmetic before writing command bytes.
   - Ensure command data is visible before the tail register is written; use the platform-appropriate memory barrier and MMIO accessors.
   - On any initialization failure, leave the engine disabled and the acceleration path unavailable.

3. **Completion fence**
   - Program the correct hardware status-page/GGTT address for the target generation; never substitute the CPU pointer or physical backing address.
   - Use a supported completion sequence and monotonic breadcrumb value.
   - Verify the required flush and post-sync semantics for the exact engine and generation; do not assume a command valid on one generation is valid on another.
   - Poll with a bounded deadline and handle wraparound without mistaking an old breadcrumb for completion.
   - Treat timeout as failure, not completion.

4. **Bounded recovery**
   - Stop new submissions after timeout and serialize recovery against submission and teardown.
   - Use only a generation-documented engine reset sequence with bounded status polling.
   - If reset cannot be confirmed, quarantine resources that may still be referenced by the GPU; do not free or reuse them.
   - Reinitialize the ring and fence state only after reset/quiescence has been established.
   - Return an error to callers and keep acceleration disabled after unrecoverable failure.

5. **Validation evidence**
   - Build the affected driver for the supported Haiku target configurations.
   - Run static checks and review every error path, especially partial initialization and teardown.
   - Test first on the intended Gen9 HD 530 system, including repeated submissions, ring wraparound, forced timeout, reset failure, and device teardown.
   - Record actual build and hardware results separately; source inspection alone does not establish successful GPU execution.

## Current release gate

**Gen9 BCS remains fail-closed until the completion-fence, quiescence, and recovery conditions above are implemented and verified.** Gen10–12 must not inherit Gen9 command or reset assumptions without separate generation-specific validation.

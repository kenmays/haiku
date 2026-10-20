# Intel Extreme BCS and GPU completion validation status

Branch: `work/intel-extreme-hd530-edid-2d`

This checklist records the safety gates that must be satisfied before enabling direct-ring 2D acceleration. It is not a claim that hardware execution has been validated.

## Confirmed implementation direction

- Keep CPU virtual pointers, physical backing addresses, and GPU GGTT offsets as distinct address domains.
- Validate ring and status-page offsets for alignment, aperture bounds, and hardware register width before programming registers.
- Publish ring-tail updates only after command writes are visible to the device; the current x86 path includes an `SFENCE` before MMIO tail publication.
- Fail closed when generation-specific ring setup or synchronization prerequisites are not met.
- Do not treat a CPU-side wait or a submitted command as proof of GPU completion.

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

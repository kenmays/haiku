# Intel Extreme Gen10–12 BCS bring-up and recovery gate

Branch: `work/intel-extreme-hd530-edid-2d`

Status: **design/review checklist only; command submission remains disabled.**

This document separates what the current source proves from what must be established before enabling the Blitter Command Streamer (BCS) on Gen10, Gen11, or Gen12. A generation number alone is not enough to select a command encoding or reset recipe; exact platform/device identification and the applicable Intel PRM must be recorded for every supported device.

## Current fail-closed contract

- The accelerant does not advertise `B_2D_ACCELERATION` in its engine token.
- Gen6+ ring selection returns unavailable, and ring setup returns `B_NOT_SUPPORTED` before lock initialization or MMIO access.
- Engine-idle and sync-token operations do not report unsupported completion as success.
- The kernel does not allocate or publish a Gen6+ BCS ring.
- Drawing hooks cannot submit a command batch without a validated ring.

These are safety gates, not evidence of command execution, GPU completion, or successful recovery. Keep them in place until every gate below is satisfied.

## Generation matrix

| Graphics generation | Command-submission evidence required | Recovery evidence required | Current decision |
|---|---|---|---|
| Gen10 | Exact platform/device ID; BCS register map; supported XY blit packet encoding and length; address width/tiling/pitch constraints; valid status-page mapping and post-sync completion sequence; ordering/flush requirements from the matching PRM | Correct reset domain and register protocol for the exact platform; engine stop/prepare behavior; bounded request/ack polling; post-reset ring and breadcrumb reinitialization; proof of GPU quiescence before resource reuse | Disabled |
| Gen11 | All Gen10 evidence, independently checked against the Gen11 PRM and device ID; do not infer packet/fence equivalence from a Gen9 path | Gen11-specific reset-domain selection and any required shared-engine/media-resource coordination; bounded polling and cleanup for every partial-failure path | Disabled |
| Gen12 | Exact platform/sub-generation and engine instance; verify command support, address/format constraints, engine topology, and completion semantics from the matching documentation | Reset domains and any platform workarounds verified for the exact device; do not assume all Gen12 sub-generations share one reset recipe | Disabled |

“Gen10”, “Gen11”, and “Gen12” here refer to graphics-generation buckets, not a promise that all devices in a bucket use identical BCS command or reset behavior. The device allowlist must be narrower than a generation comparison when the hardware evidence requires it.

## First implementation target: Gen10 Cannon Lake

The repository's kernel PCI allowlist in `src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp` already names Cannon Lake Gen10 IDs, including `0x5a52` (`CannonLake GT2`). Use `0x5a52` as a **candidate for source review only**, not as an enabled or hardware-validated target. The allowlist entry proves only that the kernel recognizes the PCI ID; it does not prove the BCS register layout, command encoding, reset protocol, or device availability for testing.

Before writing submission/reset code for this candidate, record and review all of the following:

- The exact Cannon Lake PRM revision covering the BCS engine, MI command structures, status page, engine reset, and applicable workarounds. Do not substitute Ice Lake (Gen11) or a generic Gen9 document.
- The candidate's BCS MMIO base/instance and supported ring configuration, with register definitions traced to the matching PRM.
- The exact completion operation and status-page address semantics, including whether the intended post-sync operation is legal for this engine and address space.
- The reset domain and all prepare/request/acknowledgement steps, plus the condition that proves the engine can no longer access ring/status-page allocations.
- Availability of a real Cannon Lake test machine and the build/test path. If no such hardware is available, leave the code disabled and treat any emulator or source tests as non-hardware evidence.

**Current blocker:** the repository inspection establishes that `0x5a52` is an allowlisted Cannon Lake candidate, but does not establish the matching PRM evidence, test-hardware availability, or a verified completion/reset sequence. Therefore no Gen10 BCS implementation or reset routine is added in this step.

For comparison—not as a substitute for the Gen10 manual—Intel publishes an Ice Lake (Gen11) PRM index with a dedicated Volume 10, Copy Engine: https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/ice-lake.html. The existence of separate platform manuals reinforces that command and reset details must be tied to the exact platform.

## Source audit findings for the Gen10 candidate

Inspection of the current branch confirms these implementation constraints:

- `driver.cpp` identifies `0x5a52` as `INTEL_MODEL_CNLM` / Cannon Lake GT2. The allowlist does not establish a validated BCS register map.
- `intel_extreme.cpp` allocates a generic hardware status page and validates its aperture-relative offset, but it deliberately leaves the Gen6+ blitter ring unallocated. The existing status-page allocation is not itself a completion fence: no Gen10 BCS breadcrumb is submitted or waited on.
- `intel_allocate_memory()` and `intel_free_memory()` delegate to the GART allocator. Any future ring/fence work must establish the exact allocator contract, including GPU-visible address space, allocation lifetime, and behavior when freeing fails or a device cannot be proven quiescent.
- `engine.cpp` rejects Gen6+ ring setup before lock initialization and MMIO access, does not advertise 2D acceleration, and rejects Gen6+ sync-token/idle claims. Do not relax these guards as part of a register-definition-only change.
- A targeted public-documentation search in this pass did not establish an authoritative Cannon Lake-specific Copy Engine PRM or a complete Gen10 reset recipe. Intel's official [Graphics for Linux PRM index](https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/) is the authoritative starting point for currently published manuals; if the exact Cannon Lake material is unavailable, this is an evidence blocker, not permission to borrow Gen11/Gen12 sequences.

These findings intentionally do not claim that the existing generic status page is suitable for a Gen10 BCS completion write, or that the current GART API alone can guarantee safe teardown after a hang.

## Teardown/GART lifetime audit

A follow-up audit found that ring-level quarantine is insufficient unless aperture teardown respects it:

- `intel_free_memory()` delegates to `gGART->free_memory()`. A successful explicit free removes that allocation; a failed free leaves ownership unresolved.
- `intel_extreme_uninit()` eventually calls `gGART->unmap_aperture()`. In the matching GART manager, `unmap_aperture()` removes and deletes the aperture object, and `Aperture::~Aperture()` calls `DeleteMemory()` for every allocation still tracked by that aperture. Therefore, skipping an explicit free while the ring is active does not preserve the allocation if the aperture is subsequently unmapped.
- The teardown path now checks the legacy primary ring's enable state even when its allocation was unavailable. If the ring remains enabled, or freeing its allocation fails, it leaves the entire aperture mapped rather than invoking the GART destructor and potentially releasing GPU-visible memory. This intentionally leaks/quarantines the aperture; it does not prove engine recovery or establish that the device can be reopened cleanly.
- This conservative path is limited to the legacy primary ring and allocation lifetime. It is not a general proof that all display, overlay, cursor, or future BCS resources are quiescent. Before enabling BCS, teardown must also define how all engine and display references are stopped or quarantined and how failed initialization is unwound.

Source reviewed on this branch:
- [Intel Extreme teardown and allocation wrappers](https://github.com/kenmays/haiku/blob/work/intel-extreme-hd530-edid-2d/src/add-ons/kernel/drivers/graphics/intel_extreme/intel_extreme.cpp)
- [GART aperture unmap and destructor](https://github.com/kenmays/haiku/blob/work/intel-extreme-hd530-edid-2d/src/add-ons/kernel/bus_managers/agp_gart/agp_gart.cpp)

A separate accelerant-side lifetime issue was also found in `intel_release_overlay_buffer()`: it previously ignored the return values from GART frees and unconditionally freed the overlay descriptor. It now frees the optional i965 state allocation first, checks each status, clears a field only after a successful free, and retains the descriptor if either free fails so ownership is not silently discarded. The regression checker now guards this failure path as well. This does not by itself prove overlay shutdown is complete or that the overlay engine is quiescent; that remains part of the full device-lifecycle audit. The overlay release routine still has an explicit `TODO: locking!`, and `hide_overlay()` does not return a completion/error status to its caller. Those are unresolved concurrency and shutdown-verification issues; do not interpret successful GART frees alone as proof that every display-side reference has been retired.

The source-level regression checker now asserts that the aperture unmap is conditional on the quarantine decision. This assertion has not yet been run from a local checkout; it is not a build or hardware test.

## Required implementation sequence

1. **Identify the device.** Record PCI ID, platform codename, graphics IP version, BCS instance/register base, aperture/GGTT properties, and the exact PRM revision used. Reject unknown IDs.
2. **Prove the memory model.** Document allocation owner and lifetime for ring, status page, and any batch storage; validate GPU-visible addresses and register widths; do not conflate CPU virtual, physical, and GGTT addresses.
3. **Implement submission separately per validated platform family.** Check every command dword, packet length, surface pitch/format, bounds, alignment, and wraparound. Publish command writes only after the appropriate memory-ordering barrier and then update the hardware tail through the supported MMIO accessor.
4. **Implement a real completion fence.** Use a PRM-supported status-page/breadcrumb sequence for that engine and generation. Use a monotonic sequence value, correct GPU-visible address, required cache/flush semantics, and a bounded wait. Ring head/tail equality alone is not completion.
5. **Implement bounded recovery.** Stop new submissions; serialize against submission and teardown; follow the documented reset preparation/request/ack/cancel sequence; bound every poll; verify reset completion; reinitialize state only after quiescence is proven. If reset times out or quiescence is ambiguous, quarantine potentially referenced allocations and permanently disable acceleration for that device instance.
6. **Validate failures, not just success.** Exercise repeated commands, wraparound, stale/corrupt head and tail, command reservation failure, fence timeout, reset-prepare timeout, reset-ack timeout, teardown during outstanding work, and partial initialization unwind. Confirm all waits are bounded and no GPU-referenced memory is freed or reused.
7. **Enable narrowly.** Add one device/platform allowlist entry only after source review, successful target build, repeatable software/emulator tests where meaningful, and physical hardware results are recorded. Keep all unvalidated devices fail-closed.

## Upstream reference interpretation

The Linux i915 reset implementation is useful for identifying review questions, not as a register-sequence recipe to copy. In the cited implementation, Gen8-family reset preparation has a bounded ready-for-reset wait, reset requests are cancelled on exit, and the selected reset body uses a Gen11-specific implementation for graphics version 11 and later rather than routing every Gen8+ device through one identical body. Linux also contains platform workarounds and engine coordination not represented by this Haiku accelerant.

Reference source:
- Linux i915 reset implementation: https://linux.googlesource.com/linux/kernel/git/torvalds/linux/%2B/1f57f68c4dd101e5e8ffc9ffa6428f45bcdd776a/drivers/gpu/drm/i915/gt/intel_reset.c
- Linux i915 engine and command-submission overview: https://docs.kernel.org/gpu/i915.html
- Intel Graphics for Linux developer-reference index / platform PRMs: https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/rocket-lake.html

Do not infer that the Linux reset implementation alone documents Haiku's required command submission, fence, power-management, or allocation-lifetime behavior. Check the exact Intel PRM and existing Haiku driver abstractions before implementation.

## Release gate

No Gen10–12 BCS submission or reset/recovery should be enabled until all applicable steps above are evidenced for the exact supported device. A source-level regression test proves only that the fail-closed gates remain in place; it does not prove hardware functionality.

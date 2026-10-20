# Intel HD Graphics 530 / Skylake Gen9 driver audit

This is an implementation audit for Haiku's `intel_extreme` driver, using Intel's Skylake-specific programmer documentation as the normative source. It is not a claim of complete hardware support. No Haiku build, emulator validation, or physical Skylake GPU test has been run as part of this audit.

## Target and scope

Intel identifies the desktop Core i5-6500's integrated GPU as Intel HD Graphics 530, device ID `0x1912`, with eDP/DP/HDMI/DVI output capabilities and support for up to three displays. These are platform-level capabilities, not a promise that every motherboard exposes every connector or mode:
- [Intel Core i5-6500 specifications](https://www.intel.com/content/www/us/en/products/sku/88184/intel-core-i56500-processor-6m-cache-up-to-3-60-ghz/specifications.html)
- [Intel Skylake S platform documentation](https://www.intel.com/content/www/us/en/products/platforms/details/skylake-s/docs.html)

The driver must distinguish the GPU's capabilities from board routing, PCH generation, VBT firmware data, PHY wiring, connector type, and monitor capabilities. Mini DisplayPort is a connector form factor for DisplayPort signaling; it is not a separate GPU display protocol.

## Primary references to use

1. **Skylake Gen9 PRM (normative for GPU command/register behavior):** [Intel's official Skylake PRM index](https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/skylake.html). Review all relevant volumes, especially:
   - Vol. 2a/2b/2c/2d: command opcodes, enumerations, registers, command structures.
   - Vol. 3/4: GPU overview and supported configurations.
   - Vol. 5/6: memory views and command-stream programming.
   - Vol. 11: blitter engine.
   - Vol. 12: display engine.
   - Vol. 13: MMIO.
   - Vol. 16: workarounds.
2. **Intel graphics document index:** [Intel Processor Graphics developer documentation](https://www.intel.com/content/www/us/en/developer/articles/guide/intel-graphics-developers-guides.html). Use the exact Skylake generation, not a later generation's register descriptions.
3. **Processor datasheet and platform EDS/addenda:** use the documents linked from the exact SKU/platform page above. These establish platform routing, power, PCI/device and interface constraints; they do not replace the graphics PRM.
4. **Specification updates / errata:** use the specification update that matches the actual processor family, package/platform, and stepping. The [desktop 6th-gen Core processor specification update PDF](https://www.intel.com/content/dam/www/public/us/en/documents/specification-updates/desktop-6th-gen-core-family-spec-update.pdf) is the relevant starting point for desktop SKUs; confirm its revision and affected stepping table against the installed processor. Intel's [6th-generation processor EDS addendum](https://www.intel.com/content/www/us/en/content-details/334408/6th-generation-intel-core-processor-intel-pentium-processor-intel-celeron-processor-and-intel-xeon-processor-e3-v5-family-formerly-known-as-skylake-desktop-workstation-and-mobile-platforms-external-design-specification-addendum.html) is a useful platform reference, but it is not a substitute for the correct processor specification update or PRM Vol. 16. The public [Skylake PRM Vol. 16 Workarounds PDF mirror](https://www.x.org/docs/intel/SKL/rev01/intel-gfx-prm-osrc-skl-vol16-workarounds.pdf) is useful for cross-checking the official Intel volume index; prefer Intel-hosted documents when available.
5. **Cross-checks only:** Linux's i915 source and Intel's Gen8/Gen9 PRMs can help identify intended sequencing and known workarounds. They are comparison material, not permission to copy a later-generation register sequence into Skylake.

## Findings from the current Haiku source

### 1. Ring-buffer timeout was not fail-safe — hardened in this branch

In `src/add-ons/accelerants/intel_extreme/engine.cpp`, `QueueCommands::MakeSpace()` previously logged a one-second stall and then continued as though the request had enough room. That could allow writes to overwrite unconsumed ring-buffer commands after a timeout.

The current branch changes `MakeSpace()` to return success/failure and makes `Put()`, `PutFlush()`, `PutWaitFor()`, and `PutOverlayFlip()` stop when space cannot be acquired. This is a defensive integrity fix, not a GPU recovery/reset implementation. A timed-out command can still be dropped, so callers and synchronization semantics need further work.

### 2. Software rendering is the default on Skylake-family devices

In `hooks.cpp`, the 2D blit/fill/invert/span hooks now return `NULL` for `INTEL_FAMILY_LAKE` and `INTEL_FAMILY_SOC0`. This intentionally leaves rendering to Haiku's CPU/software path while the affected GPU command stream and engine-idle behavior remain unvalidated. The engine token's capability mask remains zero; do not advertise `B_2D_ACCELERATION` until the Gen9 blitter path passes hardware testing.

The mode list and proposed/fallback modes now advertise `B_PARALLEL_ACCESS`. This is a **display-mode flag** from Haiku's `Accelerant.h`, not an engine capability: it tells the app_server that CPU access to the mapped framebuffer can proceed in parallel with display scanout. It does not itself implement software rendering, enable GPU blits, or prove that CPU writes are cache-coherent on every platform; the framebuffer mapping and cache behavior still require testing.

### 3. 2D acceleration is not yet proven usable on Skylake

- `sEngineToken` is initialized with capabilities `0 /*B_2D_ACCELERATION*/`. This means the driver is not advertising the 2D acceleration capability, despite individual hook implementations existing. Do not simply flip this bit until command submission, sync, and recovery are validated.
- The internal idle wait now returns `B_NOT_SUPPORTED` for all Gen6+ devices rather than claiming idle; the public `void` hook logs the failure. Gen6+ needs a verified BCS completion fence and generation-correct flush sequence before the guard can be removed. Skylake also needs register/command-level root-cause investigation against PRM Vols. 6, 11, 13 and 16.
- The existing rectangle and screen-to-screen operations use legacy XY blit command structures. Confirm command encodings, supported pixel formats, pitch/address units, clipping, alignment, fence/cache requirements, and valid framebuffer placement against the Skylake blitter PRM before enabling them.
- The `B_FILL_SPAN` hook and its one-row color-blit implementation are present, but must not be treated as proof that hardware acceleration is operational.

### 4. Display detection and EDID/DDC exist, but require targeted verification

`Ports.cpp` already contains EDID/DDC handling, DP AUX/I2C support, VBT-dependent I2C selection/fallback, and DDI port handling. The right next step is to audit sequencing and error reporting against Skylake display-engine documentation rather than replacing these mechanisms wholesale.

Priority checks:
- Verify AUX transactions, I2C-over-AUX, native AUX, retries, timeout cleanup, and link-training state transitions against PRM Vol. 12 and Vol. 16.
- Verify HDMI/DVI DDI mode selection, port/pipe/transcoder routing, DPLL selection, clock programming, lane/PHY setup, and supported color depth. HDMI audio and HDMI 2.0/HDCP features are separate capability paths and must not be inferred from basic video output.
- Verify miniDP as DP signaling on the board's actual DDI routing. A passive adapter does not create support for an otherwise unavailable signal/protocol.
- Preserve the distinction between “EDID read failed” and “monitor disconnected.” VBT and internal-panel fallback behavior should be tested on laptop and desktop configurations separately.
- `DigitalDisplayInterface::IsConnected()` previously checked the first pipe's enable bit in the EDID fallback and could therefore report a port connected based on an unrelated pipe. The branch now removes that fallback inference: after the port-specific scan fails to find an enabled pipe routed to this port, EDID detection is logged but the port is not reported connected. This logic change still needs hardware tests for connected-but-disabled outputs, hotplug, and monitors that fail EDID.

### 5. Display modes and multiple monitors

The accelerant creates three pipes for generations 7–11, but pipe allocation alone does not guarantee correct multi-monitor support. Validate pipe/transcoder/port constraints, shared DPLL allocation, bandwidth, link training, and rollback on failure. Test clone/extended configurations, hotplug, suspend/resume, and failed modesets.

### 6. Required test gates before advertising acceleration

- Build the accelerant and kernel driver with Haiku's supported build system and treat warnings/errors as blockers.
- Static checks: command size/alignment, ring bounds, framebuffer pitch and base units, integer overflow, MMIO readback, and every timeout/error path.
- Graphics-memory ioctl failure injection: force the allocation-result copy to user space to fail and verify the kernel frees the newly allocated aperture base; also verify normal allocate/free and invalid-base handling.
- Instrumented hardware tests: register dumps before/after initialization, ring head/tail progress, engine idle behavior, and failure recovery.
- 2D correctness: fills, spans, inversion, overlapping/non-overlapping blits, all supported pixel formats, clipping/edge coordinates, and comparison against software rendering.
- Display matrix: EDID base/extensions/checksums, DDC over I2C and AUX, HDMI, DP/mDP, internal eDP, multiple monitors, hotplug, mode changes, DPMS, and resume.
- Only enable `B_2D_ACCELERATION` after the command stream is shown to complete reliably and software fallback is available for unsupported or failed operations.

## Current status

This branch includes the fill-span hook work, a ring-buffer fail-closed fix, software-rendering fallback for Skylake-family 2D operations, and the `B_PARALLEL_ACCESS` display-mode flag. It does **not** yet provide full HD Graphics 530 feature parity, complete errata closure, or verified HDMI/DP/mDP/EDID support. Those require the exact applicable Intel documentation revision plus builds and physical-hardware tests.

### 7. Ring publication, initialization, and synchronization review — additional hardening in this branch

The follow-up review found several issues beyond the original timeout handling:

- **Ring geometry must match the wrap algorithm.** `Write()` wraps with `size - 1`, which is valid only for a non-zero power-of-two size. Setup now rejects a null CPU mapping, missing register base, sub-page size, non-power-of-two size, non-page-aligned size/offset, or a size that cannot be represented by the hardware control mask before programming the ring.
- **Keep an empty slot.** A ring must not let the producer consume all bytes, because equal head/tail values are interpreted as empty. The software accounting now reserves an 8-byte slot in the initial free-space estimate and in the head-to-tail calculation.
- **Never pad without reserving space.** Queue destruction previously emitted a NOOP for 8-byte alignment without checking that a slot was available. The destructor now reserves space before padding. If padding cannot be reserved, it rolls the unpublished batch back rather than publishing a partial/unaligned batch.
- **Lock acquisition failure must fail closed.** `QueueCommands` now records whether `acquire_lock()` succeeded and avoids touching the ring or releasing a lock it did not acquire. This guards against a failed ring-lock initialization; it does not substitute for complete setup/teardown ordering.
- **Initialization now unwinds explicit failures, but still needs a complete hardware audit.** Ring setup returns a status, rejects invalid geometry, and primary accelerant initialization releases locks and ring state when setup or mode-list creation fails. There is no demonstrated Gen9-specific reset/idle handshake in this code path. Verify the ring is disabled and the engine is quiescent before changing its base/control, that the programmed graphics address is valid for the platform's address width, and that MMIO writes have the required posting/readback and ordering.
- **Timeout is not recovery.** `MakeSpace()` fails rather than overwriting pending commands, but its callers return `void`; a batch can therefore be dropped without propagating an error. The code still has no verified engine reset/reinitialization sequence or bounded recovery state machine.
- **Synchronization was falsely reporting completion; status propagation is now improved.** `intel_get_sync_token()` fills `engine_id` and a zero counter; `intel_sync_to_token()` validates its inputs and returns the internal wait status. The internal wait now returns `B_NOT_SUPPORTED` on all Gen6+ devices because the BCS completion fence and generation-correct flush sequence are not yet implemented; it does not pretend that ring head/tail equality alone proves completion. The counter is not a true submission sequence, and there is no verified reset/recovery path, so this is not yet a complete synchronization implementation. Keep acceleration disabled until submission sequencing and recovery are validated.
- **Blit coordinate arithmetic is partially guarded.** Screen-to-screen and rectangle commands now reject null/empty batches and reject several 16-bit endpoint overflows or reversed rectangles before converting inclusive dimensions/edges to exclusive coordinates. This is not full clipping: add framebuffer-dimension bounds and verify width/height semantics against Haiku's hook contract. Validate source/destination overlap direction, pitch units, framebuffer base units, raster operations, and command lengths against the Skylake blitter PRM before using these command encodings on Gen9.

#### Recovery design required before enabling hardware 2D

A future reset path should be a documented, bounded state machine rather than a blind register write: stop accepting new batches; acquire the shared engine lock; capture ring head/tail, relevant engine status/error registers, and the last submitted sequence; request the documented Gen9 engine reset procedure; poll with a strict deadline; reinitialize ring control/base/head/tail and any required context/fence/cache state; submit a harmless validation command; wait for confirmed completion; and only then resume submissions. If any step fails, leave the engine marked unavailable and continue with software rendering. The exact reset bits and ordering must be sourced from the matching Skylake PRM and errata, not inferred from another generation.

These additional source changes are static hardening only. They have not been compiled or exercised on a Skylake machine.

### 8. Blitter-engine routing and ring initialization findings

Intel Skylake Gen9 Vol. 11 describes the Blitter (BLT) engine as a separate command streamer with its own register block beginning at `0x20000`. The driver's original `INTEL_PRIMARY_RING_BUFFER` is `0x02030` (render ring tail register), yet the XY source/color blit commands were being submitted through `primary_ring_buffer`. That is a concrete engine-selection mismatch and a strong candidate for the historical stall; it should not be treated as a proven sole cause until hardware traces confirm it. Intel PRM Vol. 11 and the Linux i915 ring implementation provide the reference basis.

Changes in this branch:
- Added `INTEL_BLITTER_RING_BUFFER` at `0x22030` and a separate `blitter_ring_buffer` in shared state.
- The kernel driver now allocates a dedicated ring for Gen6+ devices when allocation succeeds.
- The accelerant initializes the BCS ring independently; failure to allocate/setup the BCS ring is logged and leaves blit hooks unavailable instead of preventing display initialization.
- Gen6+ XY blit hooks select the BCS ring; pre-Gen6 devices continue to use the original primary ring. Gen6+ idle waits deliberately return `B_NOT_SUPPORTED` until BCS completion synchronization is implemented.
- All blit hooks are withheld if the required ring is missing. Skylake/SOC software-rendering fallback remains in force.
- Ring setup refuses to reinitialize an already-enabled ring because head==tail does not prove the engine pipeline has completed. For a disabled ring, it reads back control, clears head/tail, programs the start address, publishes an empty tail, enables the ring, and verifies control/head/tail/start readback.
- Teardown serializes with ring producers, polls head/tail briefly, logs a timeout, then disables the ring and destroys the lock. Head==tail is only a coarse ring-drain signal, not a proven engine completion fence; the Gen6+ completion path remains disabled.

Still unresolved:
- The BCS ring's presence and MMIO offset are source-level matches to the Skylake register model, but they have not been tested on hardware.
- The exact `MI_FLUSH`/`MI_FLUSH_DW` sequence and the current `COMMAND_FLUSH` encoding must be verified for the BCS engine and Gen9. A flush command accepted by the render engine is not automatically valid for BCS. Until a valid status-page breadcrumb/fence is implemented, Gen6+ idle waits return `B_NOT_SUPPORTED`.
- Proper completion tokens still need a monotonic sequence/breadcrumb mechanism; head==tail is only a coarse idle test.
- Reset/recovery still needs a Gen9-specific state machine and error/status capture, and hardware acceleration must remain disabled on Skylake until this path is tested.
- The shared-state layout change requires rebuilding the kernel driver and accelerant together.

Reference: [Intel Skylake Gen9 PRM Vol. 11 — Blitter](https://www.x.org/docs/intel/SKL/rev01/intel-gfx-prm-osrc-skl-vol11-blitter.pdf); [Linux i915 ring initialization](https://code.googlesource.com/linux/torvalds/linux/+/11e3b725cfc282efe9d4a354153e99d86a16af08/drivers/gpu/drm/i915/intel_ringbuffer.c).

### 9. Hardware status-page allocation bug

The kernel driver's status-page allocation passed `(addr_t*)info.shared_info->status_page` instead of the address of the pointer field. If `status_page` was initially null, the allocator received a null/invalid output pointer rather than `&status_page`. The branch now passes `(addr_t*)&info.shared_info->status_page`, initializes the pointer/physical address explicitly, and logs allocation failure.

This fixes the output-pointer bug only. The code still has a `TODO`-equivalent gap in that it does not program the per-engine HWS_PGA register or establish a Gen6+ breadcrumb protocol. Do not treat status-page allocation as proof that BCS completion/synchronization works.

### 10. Ring-stall diagnostics and fail-closed checks

The ring-space wait now checks that the ring remains enabled and that the hardware tail still matches the batch's last published tail before using the head pointer to calculate free space. It fails closed on disabled-ring state, invalid head geometry, or a tail mismatch. The one-second timeout log now includes the ring control and start registers as well as the hardware head/tail, published tail, software tail, ring size, remaining space, and requested bytes. These values are intended to distinguish a stopped/disabled ring, a bad ring base/control setup, and a command streamer that stopped consuming an otherwise valid ring.

This is diagnostic hardening, not a recovery implementation. The ring head/tail relationship cannot establish that all prior GPU work has completed, and the new log does not identify the precise command that stalled. Capture these values alongside a full MMIO dump and the matching Gen9 engine status/error registers when reproducing the issue on physical HD Graphics 530 hardware.

The status-page/breadcrumb implementation remains deliberately blocked until the exact Skylake Gen9 BCS MI_FLUSH_DW encoding, HWS address-space requirements, and mapping lifetime are verified. Linux i915 Gen8+ code is a useful cross-check for the command shape, but is not sufficient by itself to prove a proposed sequence is valid for Haiku's current GGTT/status-page setup or the BCS engine on this device. Do not remove the Gen6+ B_NOT_SUPPORTED idle-wait guard or enable B_2D_ACCELERATION based on this change.

### 11. Ring publication ordering remains unverified

The ineffective local-stack `atomic_add()` previously placed immediately before the MMIO tail update has been removed: an atomic operation on an unrelated local variable does not flush the ring or establish the GPU-visible ordering required here. The current code still writes the new tail without a verified CPU/write-combined-memory-to-MMIO publication primitive. Do **not** treat command submission as ordering-safe until the CPU mapping/cache type, architecture-specific ordering requirements, and Haiku-supported barrier or MMIO publication primitive are verified for this platform. This issue is separate from a GPU completion fence and does not establish that BCS commands have executed.


### 12. Failed command reservations now discard the entire unpublished batch

`QueueCommands` now records reservation failures from `Put()`, `PutFlush()`, `PutWaitFor()`, and `PutOverlayFlip()`. Its destructor restores the software position and free-space accounting to the batch's initial state and does not publish the tail if any reservation failed. This prevents a previously written prefix from being published after a later command failed to reserve ring space. The command words already written beyond the published tail are intentionally left unreachable and will be overwritten by later submissions.

The local-stack `atomic_add()` previously used before the tail write was also removed because it did not operate on ring memory and did not establish the required GPU-visible publication ordering. The tail write still lacks a verified architecture/cache-aware ordering primitive; command submission therefore remains unvalidated and hardware acceleration must stay disabled. These changes are static hardening only and have not been compiled or tested on hardware.


### 13. Gen10–Gen12 support boundary and shared-path audit (follow-up)

The PCI table contains entries for Cannon Lake (Gen10), Ice Lake / Elkhart Lake / Tiger Lake (Gen11), and Rocket Lake / Alder Lake / Raptor Lake (Gen12). A matching PCI ID proves only that the driver can recognize a device; it does **not** establish correct display initialization, connector routing, 2D command legality, power management, or acceleration support.

Current safe support statement for these generations:

- **Device discovery:** PCI IDs are present for a subset of these product families. This is not a complete or authoritative enumeration of every SKU, stepping, or subsystem variant. Continue cross-checking IDs against current upstream PCI tables and Intel platform documentation before adding entries.
- **Display modes and pipes:** the accelerant allocates four pipe objects for Gen12 and three for Gen7–Gen11. Object allocation is not proof that the physical generation supports those pipes, that each transcoder/port is routed correctly, or that clock/bandwidth limits are respected. Gen12 pipe D and multi-monitor modes must remain unclaimed until platform-specific constraints and failure rollback are verified.
- **2D engine:** a dedicated BCS ring is selected for Gen6+; this is only ring plumbing, not generation-correct command submission. The current shared ring setup, legacy XY blit command structures, flush opcode, status-page assumptions, and MMIO ordering have not been independently validated for Gen10, Gen11, or Gen12. Do not enable hardware 2D or set `B_2D_ACCELERATION` for these generations.
- **Synchronization/recovery:** Gen6+ idle/sync deliberately fails closed with `B_NOT_SUPPORTED` rather than treating head/tail equality as proof of completion. Do not remove that guard for Gen10–Gen12. Implement and validate a per-generation completion/breadcrumb scheme and bounded engine recovery only after the correct PRM register/command definitions, address-space constraints, and errata are established.
- **EDID/DDC/AUX:** shared code paths are present, but the current fallback logic does not prove AUX power sequencing, I2C-over-AUX transaction semantics, native AUX retry behavior, link training, Type-C/TC routing, PHY power, or hotplug behavior on Gen10–Gen12. These require generation-specific display-engine documentation and tests; Gen9 behavior must not be assumed equivalent.
- **`B_PARALLEL_ACCESS`:** retaining this display-mode flag is independent of GPU blit acceleration. It does not prove framebuffer mapping/cache coherency on every platform, and it must not be used as evidence that BCS is operational.
- **Power management:** DMC/firmware loading, power wells, PHY/port power, runtime power transitions, suspend/resume, and reset sequencing need generation-specific review. Avoid writing guessed register values or applying Skylake workarounds to newer generations without matching documentation.

#### Required next implementation gates for Gen10–Gen12

1. Create a per-generation register/command capability table sourced from the matching Intel PRMs, EDS/platform docs, and specification updates/errata; do not infer support from `Generation() >= N` alone where behavior differs.
2. Audit ring allocation and GPU virtual/graphics address width, ring control masks, enable/disable ordering, memory/cache attributes, and the supported CPU-to-MMIO publication barrier for each generation.
3. Implement a verified BCS completion breadcrumb with monotonically increasing submission sequence numbers, timeout propagation, and an engine-unavailable state after failed recovery. The status-page allocation fix alone is insufficient; HWS_PGA programming and address-space/mapping lifetime must be proven.
4. Add generation-specific bounded reset/reinitialization only after documenting exact status/error registers, reset bits, polling deadlines, and required reinitialization steps. A failed reset must preserve software rendering and must not claim completion.
5. Validate rectangle/fill commands against each generation's BCS PRM, including opcode/length, pitch and address units, supported tiling/format, clipping, overlap direction, flush requirements, and completion semantics.
6. Test display separately from 2D: port detection, EDID base/extensions/checksums, DDC/AUX, DP/eDP/HDMI routing, link training, hotplug, modeset rollback, multiple displays, DPMS, and suspend/resume.
7. Build kernel driver and accelerant together and run static checks plus real hardware tests on at least one representative platform per supported generation. Keep the acceleration capability disabled until each tested generation passes.

**Status:** Gen10–Gen12 remain recognized-device / software-rendering targets, not fully supported or hardware-accelerated targets. No build, CI test, emulator test, or physical Gen10–Gen12 validation has been performed in this branch. This section documents the remaining gates; it does not claim implementation or validation that has not occurred.

### 14. Validate ring state before every command reservation

Follow-up source review found that the previous checks in `QueueCommands::MakeSpace()` were only reached when the cached `space_left` value was smaller than the request. A stale cached value could therefore bypass the enabled-bit, head geometry, and published-tail checks and write commands to a disabled or externally changed ring.

The current branch now reads and validates the ring control, start address, head, and hardware tail **for every reservation**, before decrementing cached space or writing command words. It rejects a reservation if the ring is disabled, the programmed size differs from the software ring geometry, the start address differs from the configured graphics offset, the head is out of range/misaligned, or the hardware tail does not equal the batch's initially published tail. This closes the cached-space bypass in the producer path and complements the existing rechecks while waiting for space.

This is a fail-closed consistency check, not a synchronization fence: it does not prove prior GPU commands have completed, resolve CPU/WC-memory-to-MMIO ordering, provide a completion breadcrumb, or recover a stalled engine. The driver still must be built and tested with a real device, and Gen6+ hardware acceleration must remain disabled pending the documented BCS completion and recovery work.

### 15. Bound cached free space using the live ring head

The reservation path now also derives an upper bound on available bytes from the current hardware head and the software tail (which may be ahead of the last published tail while a batch is being assembled). It preserves the mandatory 8-byte empty slot and clamps `space_left` downward when the cached value exceeds that bound. This keeps cached accounting from being treated as authoritative when reserving command words.

This calculation is a conservative producer-side bound, not a substitute for the hardware consistency checks, a completion breadcrumb, or a reset protocol. Head/tail values can still be misleading after an engine reset or other hardware-state corruption; no automatic recovery is attempted. Gen6+ BCS synchronization and acceleration remain disabled until the generation-specific status-page and completion protocol is implemented and validated.

### 16. Reject ring allocations outside the graphics aperture

`setup_ring_buffer()` now validates the programmed GGTT offset before writing `RING_BUFFER_START`: the offset must fit the register's 32-bit field, and the complete ring allocation must fit within `graphics_memory_size`. This prevents address truncation and avoids enabling a ring whose allocation extends beyond the graphics aperture. The check is in addition to page alignment, power-of-two sizing, and hardware size-field validation.

This validates the driver's allocation geometry only; it does not establish correct cache attributes, publication ordering, status-page setup, GPU completion, or engine recovery. Build validation remains outstanding.

### 17. Status-page mapping and ownership trace (follow-up)

The allocation path has now been traced from the kernel driver's GART aperture through the shared driver state into the accelerant:

- `intel_extreme_init()` maps the GART aperture and stores its CPU base and size in shared state. Ring allocations are made through `gGART->allocate_memory()`; their CPU-visible `base` is converted to a GGTT-relative `offset` by subtracting `aperture_base`.
- The hardware status page is separately allocated with `B_APERTURE_NEED_PHYSICAL`. The shared structure stores both its CPU-visible pointer (`status_page`) and allocator-returned physical address (`physical_status_page`). The pointer-output bug is fixed, and allocation failure is logged.
- The accelerant clones the shared-info area and accesses the status-page pointer through shared state. This is a shared CPU mapping, not evidence that the GPU can address that mapping using the same address value.
- The current code does not establish a documented per-generation HWS_PGA programming value/address space, initialize a completion sequence, or prove that CPU reads observe GPU writes with the required cache/coherency rules. Nor does it prove the status-page allocation remains valid for every engine and lifecycle transition.
- The newer `status_page_offset` field is validated metadata only: the current branch has no consumer that programs it into an HWS register or submits a completion breadcrumb. Its presence must not be interpreted as an enabled or GPU-validated status-page path.
- Ring command storage is allocated separately from the status page. The existing ring aperture checks validate ring geometry but do not validate the status page's GPU-visible address or its lifetime relative to engine enable/disable and teardown.

**Implementation decision:** do not wire the status page into Gen6+ BCS completion yet. Choosing between physical and GGTT addresses, programming a guessed HWS_PGA register, or assuming the status page is coherent would risk hangs or false completion. The next code change must be based on the exact device-generation PRM and the GART allocator's documented mapping semantics; it must then establish page alignment/address-width checks, explicit ownership/lifetime, a monotonic breadcrumb protocol, and CPU-visible ordering before the fail-closed idle wait can be removed.

This is a source audit only. It makes no claim of a working hardware status page or successful build/test.

### 18. Kernel-side graphics allocation lifetime audit

Tracing `intel_extreme_init()` against `intel_extreme_uninit()` found a lifecycle gap relevant to any future status-page completion implementation:

- Initialization allocates the primary ring and (for Gen6+) a separate blitter ring through `intel_allocate_memory()`; it also allocates overlay-register memory, the hardware status page, and optionally cursor memory.
- The uninitialization path removes interrupt handling, unmaps the GART aperture, and deletes the register/shared areas, but the inspected function does not call `intel_free_memory()` for those allocations before unmapping the aperture.
- The status page is not currently used for a verified breadcrumb protocol, so this does not establish a live synchronization failure by itself. It does mean that the lifetime and cleanup of the future GPU-visible status page cannot be considered solved.
- A cleanup patch must account for the actual allocation base expected by `gGART->free_memory()`, ensure engines/rings are stopped before their storage is freed, handle partial initialization and allocation failures, avoid double-freeing shared allocations, and free allocations before unmapping their aperture. The cursor and overlay allocations have the same ownership question.

**Decision:** record this as a follow-up rather than add unconditional frees without auditing every open/close/error path. Freeing a ring while a command streamer can still fetch from it would be worse than leaking it; freeing shared state twice would be a separate correctness bug. Before changing lifecycle code, trace all callers of `intel_extreme_uninit()`, the device close path, and the accelerant's ring teardown ordering, then implement one idempotent cleanup path that handles partial initialization.

This remains a source inspection finding; no runtime leak test or hardware teardown test has been performed.


### 19. Kernel device lifetime and graphics-allocation teardown trace

This section records the call-path trace for the potential allocation leak identified above. It is a source audit, not proof of runtime behavior under every driver unload/error scenario.

#### Device open/close ownership

In `src/add-ons/kernel/drivers/graphics/intel_extreme/device.cpp`:

- `device_open()` serializes initialization with `gLock`. When `open_count == 0`, it calls `intel_extreme_init()`; on success it increments `open_count` and returns the shared `intel_info` as the cookie.
- `device_close()` is a no-op and does not decrement the count.
- `device_free()` decrements `open_count`; when the prior count was one, it marks initialization unavailable and calls `intel_extreme_uninit()`.
- The private graphics-memory ioctl allocates/frees through the mapped GART aperture, so these operations depend on the device remaining initialized while callers use the file descriptor.

Thus the final file-cookie release, rather than `device_close()`, is the visible teardown boundary. The count is managed under `gLock`. This does not establish that every accelerant has finished using shared rings before final release; that depends on the userspace accelerant lifecycle and file-descriptor ownership.

#### Accelerant ring teardown

In `src/add-ons/accelerants/intel_extreme/accelerant.cpp`, primary accelerant initialization sets up the primary ring and, on Gen6+, attempts a separate BCS ring. If mode-list creation fails, it tears down initialized ring locks before `uninit_common()`. `intel_uninit_accelerant()` similarly tears down the BCS ring (when its size indicates setup), then the primary ring, and destroys engine/accelerant locks for the primary instance. A clone does not tear down these shared rings.

In `engine.cpp`, `uninit_ring_buffer()` serializes against producers with the ring lock, waits briefly for head/tail equality, logs on timeout, disables the ring and destroys its lock. Head/tail equality is not a verified engine-completion fence; the timeout path disables the ring anyway. The allocation backing each ring is not freed there: the kernel driver's GART allocator owns it for the lifetime of the device initialization.

#### Allocations visible in kernel initialization

The initialization path allocates or attempts to allocate:
- the primary render ring;
- a separate Gen6+ BCS ring;
- one page of overlay-register memory;
- one page of hardware status-page memory;
- optional one-page cursor memory.

The shared structure retains CPU pointers and/or physical addresses for these resources. The primary and BCS ring bases are translated to aperture-relative offsets. The inspected `intel_extreme_uninit()` disables/removes interrupt handling and MSI where applicable, then unmaps the aperture and deletes MMIO/shared areas, but does not visibly free these allocations through `intel_free_memory()` first.

#### Safe conclusion and next implementation gate

The source confirms a likely graphics-allocation lifetime leak on normal teardown, but does not yet justify an unconditional cleanup patch. A safe fix needs a single cleanup routine that tracks successful allocations explicitly, handles every partial-init return path, disables/stops every initialized command streamer before freeing ring storage, and frees each allocation exactly once while the aperture is still mapped. It must also account for accelerant teardown having already disabled the rings, initialization failures before shared state is fully populated, and allocation APIs whose output parameters may be unset on failure.

Before implementing that routine, audit **all** early returns in `intel_extreme_init()` and the GART allocator's exact base argument contract; initialize allocation tracking fields before the first possible failure; verify whether any display/overlay/cursor hardware can still reference its backing memory; and confirm whether the device manager guarantees final `device_free()` only after accelerant clones close. Do not free using an aperture-relative offset if `free_memory()` expects the original CPU allocation base. Do not free the status page or overlay/cursor storage until all consumers are disabled. If engine quiescence cannot be confirmed, fail safely rather than reclaiming memory that hardware may still access.

No teardown code was changed in this audit-only commit because the present source trace does not prove a complete, safe release sequence for all partial initialization and hardware-use cases.


### 20. Initialization failure boundaries and free-address contract

A second pass over the full `intel_extreme_init()` body found only three explicit error returns: GART aperture mapping failure, shared-area creation failure, and MMIO mapping failure. All three occur before the later overlay, status-page, cursor, primary-ring, and BCS-ring allocations. The shared-area creator and MMIO mapper use `AreaKeeper`; the visible failure paths explicitly unmap the GART aperture. Therefore, the allocation-leak concern from Section 19 is concentrated on teardown after successful initialization, not on a visible explicit return after one of those graphics allocations has succeeded. This does not rule out internal failures or allocator-side partial-allocation behavior.

The allocator wrapper's `intel_allocate_memory()` returns the allocation base through its `_base` output, while the ring setup separately derives an aperture-relative hardware offset by subtracting `info.aperture_base`. The public free ioctl passes the caller-provided `buffer_base` directly to `intel_free_memory()`, which forwards it to `gGART->free_memory()`. That establishes the API-level distinction: the free argument is the allocator's original base, not the ring's GGTT/aperture-relative `offset`. Cleanup must preserve the original returned base for every allocation and must never free the derived hardware offset.

The shared structure currently records original CPU-visible pointers for overlay/status/cursor and ring backing memory, but it does not provide explicit per-allocation success flags for all resources. A follow-up implementation should use a clearly documented sentinel convention or kernel-private ownership record rather than infer allocation success solely from physical address zero or a shared pointer that may be stale. It must preserve the allocator's exact expected base type and handle failed optional allocations without attempting to free uninitialized values.


### 21. Gen6+ sync-token behavior: fail closed instead of publishing a fake fence

The current accelerant's `intel_get_sync_token()` previously returned success with a constant counter of zero for every engine generation. A constant counter cannot identify which submitted commands a token represents and must not be treated as proof of completion.

The implementation now returns `B_NOT_SUPPORTED` for Gen6+ until a generation-correct breadcrumb/fence path is implemented. The legacy path retains its existing behavior. This is deliberately conservative: it prevents callers from receiving a nominally successful Gen6+ token that has no real completion semantics. It does not implement a fence and may cause synchronization callers to receive an unsupported status until the fence work is completed.

### 22. Completion-fence implementation gates

A real Gen6+ fence must not be based on ring head/tail equality alone. Before enabling it, the implementation must establish and test all of the following for each supported engine/generation:

1. The exact documented command encoding and placement for the completion write (including required flush/invalidate operations and any command restrictions for BCS versus render).
2. The status-page address form required by the engine's HWS register: physical address versus GGTT address, alignment, address-width restrictions, and any generation-specific programming sequence.
3. That the address programmed into hardware refers to the allocation's GPU-visible address, not merely the CPU pointer or an assumed physical address.
4. Required CPU/GPU cache-coherency and publication ordering: write command payload, apply the platform-supported memory barrier/cache operation, then publish the ring tail; after observing a breadcrumb, apply any required read barrier before consuming completion data.
5. A monotonically advancing sequence counter with wrap-safe comparison, and a defined policy for reset/reinitialization so stale pre-reset values cannot satisfy new tokens.
6. A bounded wait with a deadline and diagnostics that capture engine identity, head, tail, ring control, last submitted sequence, last completed sequence, and relevant documented error status.

Until these are verified against the exact platform programming reference and reviewed in code, the driver continues to reject Gen6+ sync-token creation and engine-idle claims.

### 23. Bounded reset/recovery: safety requirements and current limit

No generic reset write has been added. Reset controls, engine reset scope, forcewake requirements, polling semantics, and required post-reset initialization differ by platform and engine. A guessed reset register or a display-engine reset applied to a command streamer could hang the GPU or disrupt display output.

A recovery implementation must be serialized against all submissions and follow a state machine similar to:

- **RUNNING → QUIESCING:** reject new work, acquire the engine submission lock, record a diagnostic snapshot and last sequence.
- **QUIESCING → RESETTING:** use the documented per-generation/per-engine reset mechanism and required forcewake/power-domain access; poll only until a fixed deadline.
- **RESETTING → REINITIALIZING:** restore ring start/size/head/tail/control, completion state, cache/context state and any required engine-specific registers using documented ordering.
- **REINITIALIZING → VALIDATING:** submit a harmless, valid engine-specific command and wait for its actual completion breadcrumb before marking the engine usable.
- **Any failed deadline or validation → UNAVAILABLE:** leave hardware acceleration disabled for that engine, return an error to waiters, and retain software rendering as the fallback. Do not free its ring/status-page memory unless hardware access has been safely stopped or device reset/removal semantics prove it can no longer DMA.

Recovery must not use head==tail as its sole proof of idle, must not reclaim ring memory after an unconfirmed reset, and must not silently resume submissions after a failed validation.

### 24. Graphics-allocation cleanup: ownership requirements before implementation

The kernel driver currently keeps allocator bases in the primary/BCS ring `base` fields, `overlay_registers`, `shared_info->status_page`, and `shared_info->cursor_memory`. The overlay, status-page and cursor physical-address fields are not suitable substitutes for allocator bases. The BCS and primary rings are shared with the accelerant, whose teardown attempts to disable them; however, a bounded ring-drain timeout is not a completion guarantee, and final kernel teardown must not assume that an accelerant's timeout means DMA has stopped.

A cleanup change should therefore:
- Track allocation ownership/success explicitly in kernel-private state, rather than relying on an uninitialized optional allocation output or a physical address sentinel.
- Stop new work and disable interrupt delivery before attempting engine shutdown; use only verified generation-specific shutdown/reset paths.
- Disable overlay/cursor consumers with documented hardware sequencing before freeing their backing memory.
- Free only the exact allocator bases returned by `intel_allocate_memory()`, once each, while the aperture mapping remains valid.
- Continue cleanup after an individual free failure while recording it; never double-free shared ring storage from both accelerant and kernel.
- Unmap the aperture and delete areas only after all resources that depend on them are safely released.
- If hardware quiescence cannot be established, fail closed and keep acceleration unavailable rather than releasing potentially active DMA memory.

This is intentionally a requirements record, not a claim that recovery or full allocation cleanup is complete. The next safe implementation depends on verifying the applicable engine reset/HWS procedures and confirming GART allocator behavior, then testing partial-init, normal teardown, stalled engine, and reset-failure paths.

### 25. Allocation-failure state made explicit in kernel initialization

The primary and Gen6+ blitter ring descriptors are now initialized to an unavailable state before allocation. On allocation failure, their base pointers remain null, register bases and sizes remain zero, and a diagnostic is emitted. Overlay, status-page, and cursor output fields are also initialized before allocation; cursor allocation failure is logged and both cursor address forms are reset to invalid values.

This hardens partial initialization and prevents a failed optional allocation from leaving a stale or partially returned address in shared state. It does **not** free successful allocations, establish GPU quiescence, or solve teardown ownership. A failed primary ring allocation must continue to leave acceleration unavailable; it must not be treated as a usable ring.

### 26. Avoid initializing the unused legacy render ring on Gen6+

The accelerant's 2D command path selects the BCS ring for Generation 6 and later; the primary ring is selected only for earlier generations. Primary-ring setup therefore no longer gates accelerant initialization on Gen6+. Its teardown is likewise skipped there because this accelerant instance never initialized that ring. BCS setup remains optional for display initialization and its failure leaves hardware blits unavailable.

This removes an unnecessary failure dependency and avoids touching an unused render engine during Gen6+ accelerant startup/shutdown. It does not enable Gen6+ blits: the completion-fence and publication-ordering prerequisites remain unresolved. This change should be validated on legacy pre-Gen6 devices and Gen6+ devices, including BCS allocation/setup failure.

### 8. Follow-up review: status-page allocation is not a completion fence

The kernel-side initialization in `src/add-ons/kernel/drivers/graphics/intel_extreme/intel_extreme.cpp` allocates a page for `status_page` with `B_APERTURE_NEED_PHYSICAL` and stores `physical_status_page`. That allocation alone does **not** establish that the GPU can write a breadcrumb there:

- The inspected initialization path does not establish a verified Gen6+ BCS HWS_PGA programming sequence, a GPU-visible address-domain guarantee, or a defined breadcrumb slot/format.
- The accelerant currently has no monotonic BCS sequence number and no completion polling tied to a submitted batch. Head/tail equality is only ring consumption state; it is not a sufficient completion proof for all engine/pipeline work.
- The status page must not be assumed coherent merely because it has a CPU pointer and physical address. Confirm the GART mapping, GPU address interpretation, required alignment, cache behavior, and required posting/readback sequence for the exact generation.
- A fence implementation must reserve space for the generation-correct flush and breadcrumb commands as one atomic batch. If reservation, command emission, or tail publication fails, the fence counter must not advance and the operation must not report success.
- Timeout handling must return a real failure and leave acceleration disabled. A bounded reset is not safe until the correct per-engine reset sequence, reset polling, error-state capture, and post-reset ring reinitialization are implemented and checked against the applicable PRM/workarounds.

**Safe next implementation gate:** first document and verify the exact Gen9 BCS HWS_PGA register and address semantics against the Skylake PRM and the current Haiku GART implementation; then implement the Gen9 fence behind an explicitly disabled-by-default path with unit/static checks. Only after that sequence is verified should the implementation be generalized to Gen6–12, with per-generation command and register tables rather than treating Gen6+ as one uniform hardware target.

### 9. Allocation and teardown constraints

- The primary ring allocation is intentionally skipped for Gen6+ because the active 2D path is BCS; do not reintroduce an unused render-ring allocation as a workaround.
- The BCS ring allocation is retained, but allocation success is not equivalent to safe engine ownership. Do not reclaim its aperture allocation while the engine may still fetch commands. The current teardown stops the ring after a bounded drain wait, but this alone does not prove GPU quiescence or GART allocation lifetime safety.
- Any future cleanup path must track allocation ownership explicitly, stop and read back the engine control register, validate the stop/reset state, and only then release the corresponding GART allocation. On uncertain state, prefer a bounded leak over freeing memory that the GPU may still access.
- Status-page and cursor allocation failures should leave both pointer and address fields invalid. Optional cursor failure may fall back to software; status-page failure must block any future fence-enabled path rather than silently substituting head/tail polling.

These are review findings and implementation gates, not claims that the missing fence or recovery path has been completed.

### 10. Engine capability negotiation hardening

Commit `d699a6e` updates `intel_acquire_engine()` to reject any requested capability not advertised by `sEngineToken.capabilities`. The token currently advertises zero hardware capabilities, so requests for `B_2D_ACCELERATION` return `B_NOT_SUPPORTED` rather than receiving a token that cannot satisfy the request. This closes a gap where acquisition ignored the caller's capability mask despite acceleration being intentionally disabled.

This change is defensive API behavior, not a substitute for testing the app_server/accelerant fallback path. Confirm that the software renderer is selected correctly when the acquire hook returns `B_NOT_SUPPORTED`; only advertise `B_2D_ACCELERATION` after the BCS fence, publication ordering, and recovery path are verified end-to-end.


### 11. Regression gates for ring submission and engine API

The following cases should be covered by a Haiku build/test run before this branch is considered ready. They are test requirements; they have not been executed in this environment.

**Engine acquisition**
- Requesting `B_2D_ACCELERATION` must return `B_NOT_SUPPORTED` while the token advertises no capabilities, and must leave the output token null.
- A zero-capability request may acquire the engine lock, but must not result in any command submission or imply that hardware 2D acceleration is available.
- If an input sync token cannot be satisfied, acquisition must release the engine lock and leave the output token null.
- Releasing a token with an invalid token pointer must return `B_BAD_VALUE` without releasing another caller's lock.

**Ring reservation and publication**
- Reject zero, odd-dword, non-dword-aligned, oversized, and malformed ring requests without changing the hardware tail.
- Reject disabled rings, incorrect ring-size control fields, mismatched START, out-of-range or unaligned HEAD, and an externally changed TAIL.
- Simulate a full ring and a non-advancing HEAD: reservation must time out within the documented bound, roll the software batch back, and never publish a partial command prefix.
- Force a failure after one or more commands have been written to the CPU mapping: the destructor must restore the original software position/space and leave the MMIO tail unchanged.
- Verify the required platform memory-ordering primitive before enabling any path that publishes commands from write-combined or otherwise weakly ordered memory. A CPU compiler barrier alone is not sufficient evidence.
- Confirm that the software-rendering path is selected when the ring is unavailable or the engine acquisition hook rejects acceleration.

**Teardown and failure injection**
- Exercise ring allocation failure, lock initialization failure, ring-enable readback mismatch, mode-list creation failure, and normal accelerant shutdown.
- On a non-draining ring, teardown must stop the engine and read back the control register; it must not free backing memory unless the kernel/GART ownership path separately establishes safe quiescence.
- Verify pre-Gen6 primary-ring teardown separately from Gen6+ BCS teardown, and confirm Gen6+ startup does not touch the unused primary ring.

**Acceptance criteria:** no successful hardware-acceleration capability advertisement until the above regressions pass and a generation-correct completion fence is implemented; no Gen6+ completion claim based solely on ring HEAD/TAIL equality; no GPU-visible allocation is reclaimed on an unverified stop/reset path.


### 12. Revalidate ring ownership immediately before tail publication

Commit `db62740` adds a final MMIO-state check in the `QueueCommands` destructor before publishing a non-empty batch. It verifies that the ring remains enabled, the configured size still matches, the hardware tail still equals the batch's original published tail, and START still points at the expected aperture offset. If any check fails, the batch is discarded by restoring the saved software position and space accounting; the code does not advance the hardware tail.

This narrows the window in which a ring disable/reprogram or unexpected tail change could receive a stale submission. It does not close races with a reset path that fails to use the same ring lock, does not establish CPU/WC-memory ordering, and does not prove GPU completion. Those remain explicit requirements; hardware acceleration stays disabled.


### 13. Cross-generation 2D hardware-acceleration gap analysis (Gen1–Gen12)

**Scope and status.** This is a source-level gap inventory, not a claim that every listed family is currently bound by this driver or that hardware was tested. Generation labels and product codenames do not always align one-to-one; support must be keyed to PCI ID, platform stepping, and documented engine capabilities. The current branch intentionally advertises **zero** hardware 2D capabilities. Its Gen6+ command-selection rule routes XY blits to BCS, but that routing rule is not proof that every Gen6–12 device is supported. The current kernel driver's PCI-ID table does contain entries for several later families (including Cannon Lake, Ice Lake, Tiger Lake, Rocket Lake, and Alder Lake); this establishes device enumeration coverage for those IDs, **not** validated 2D acceleration or complete display/engine support.

#### Cross-generation blockers that affect every family

1. **Capability gating and fallback:** Keep `B_2D_ACCELERATION` disabled until a generation-specific implementation passes build, regression, and hardware validation. Verify the app_server actually falls back to software when acquisition returns `B_NOT_SUPPORTED`.
2. **Command legality:** Validate every opcode, packet length, pitch/tiling interpretation, pixel format, coordinate bound, source/destination address form, and required flush for the selected engine and generation. A command legal on one ring/family must not be assumed legal on another.
3. **GPU address domain:** Establish how each allocation becomes GPU-visible (GGTT/aperture offset, physical address, or other platform-specific form). CPU virtual pointers, physical addresses, and aperture offsets are not interchangeable.
4. **Publication ordering:** Prove the correct Haiku primitive for writes to the ring mapping before MMIO tail publication, including WC/cache behavior and required MMIO readbacks. The current final ring-state check only validates ownership; it is not a memory barrier.
5. **Completion semantics:** Implement a real monotonically increasing breadcrumb/fence, generation-correct status-page programming, cache/read ordering, wrap-safe comparisons, and bounded waits. Ring HEAD/TAIL equality alone is not a completion fence.
6. **Reset and recovery:** Implement per-generation, per-engine quiesce/reset/reinitialize/validate state machines with bounded polling and error capture. If recovery cannot prove the engine is stopped, keep acceleration unavailable and do not free potentially DMA-visible allocations.
7. **Ownership and teardown:** Track original allocator bases and successful allocations explicitly. Serialize producers, reset, shutdown, and memory reclamation; handle failed initialization and stalled-engine paths without double-free or unsafe reclamation.
8. **Validation coverage:** Add tests for reservation rollback, tail publication, fence timeout, sequence wrap/reset, malformed commands, allocation failures, and software fallback. Run Haiku builds and test on actual hardware for each claimed platform class.

#### Generation matrix

| Generation / representative family | Current 2D assessment | Main gaps before hardware acceleration can be claimed |
|---|---|---|
| **Gen1 / earliest 8xx-class** | Unverified; do not assume supported. This branch provides no evidence of a complete tested Gen1 path. | Confirm PCI-ID binding, chipset-specific command streamer/ring model, legal BLT packets, address/fence registers, aperture and cache rules. If hardware lacks a usable compatible BLT path, keep software fallback. |
| **Gen2 / 8xx-class (e.g. 845/865-era)** | Legacy path only; not covered by Gen6+ BCS changes. | Prove legacy ring initialization, command encodings, alignment, tiling/fence handling, completion and reset semantics per chipset. Do not treat the existing legacy flush/HEAD-TAIL wait as universally sufficient without platform validation. |
| **Gen3 / 915–945-era** | Legacy path; no new cross-generation validation established. | Validate supported PCI IDs and per-family BLT formats, pitch/tiling rules, synchronization, address width, ring geometry and teardown. Add tests on actual 915/945-class devices. |
| **Gen4 / 965 and G4x** | Legacy path; not proven by the HD530 work. | Verify chipset-specific blit packets, fence/tiling behavior, cache flushes, address interpretation, ring startup and completion. Avoid inheriting assumptions from earlier 8xx/9xx families. |
| **Gen5 / Ironlake** | Legacy/pre-Gen6 path; no hardware proof in this branch. | Validate Ironlake ring and command behavior separately, including status/fence semantics and reset. Confirm generation classifier and supported-ID table agree. |
| **Gen6 / Sandy Bridge** | BCS routing exists in source, but hardware 2D is disabled. | Implement and verify BCS HWS/status-page setup, GPU-visible address, completion breadcrumb, ordering, timeout diagnostics, reset/recovery and BCS-specific command legality. |
| **Gen7 / Ivy Bridge and Haswell** | BCS routing may select the right engine, but no fence-enabled acceleration is claimed. | Validate per-platform command restrictions/workarounds, BCS register layout, HWS programming, flush/fence semantics, cache coherency and reset behavior; do not assume Ivy and Haswell are identical. |
| **Gen8 / Broadwell** | BCS route exists in the common Gen6+ branch; no completed end-to-end acceleration path. | Check generation-specific address width and command/address encoding, ring registers, status-page semantics, ordering, engine workarounds, and reset/recovery. |
| **Gen9 / Skylake (HD 530 target)** | **Primary development target; hardware 2D intentionally disabled.** Defensive ring validation and BCS routing exist, but no verified completion fence. | Highest priority: verify Skylake BCS HWS_PGA and address-domain semantics against the PRM and Haiku GART mapping; identify the correct memory-order primitive; implement atomic flush + breadcrumb submission, monotonic sync tokens, bounded completion waits, and safe reset/recovery. Then build and test on real HD 530 hardware. |
| **Gen10 / Cannon Lake-era** | No evidence from this work that a complete Gen10-specific path is validated. | Confirm exact PCI-ID support and generation classification first. Audit BCS availability, command packets, register bases, address width, HWS/fence rules, workarounds and reset sequence independently. |
| **Gen11 / Ice Lake** | Do not infer 2D support from display modesetting or device detection. | Confirm PCI IDs and BCS availability; verify generation-specific ring/command/HWS/address behavior, memory ordering, power/forcewake requirements and recovery. Add a separate hardware test target. |
| **Gen12 / Tiger Lake and later Gen12-class platforms** | Unverified for this branch. A generic `Generation() >= 6` test is too broad to establish compatibility. | Validate exact platform/device family and engine topology, register definitions, address forms, command restrictions, cache/fence rules, power management, reset scope and possible additional blitter instances. Do not enable a single-ring BCS implementation where topology or required setup differs. |

#### Recommended implementation order

**P0 — keep acceleration safely disabled (already in place):** zero capability advertisement; reject unsupported capability requests; preserve software fallback. Do not relax this gate based on static inspection alone.

**P1 — close the Gen9/HD530 proof gap:** document authoritative Skylake BCS command/HWS_PGA requirements and Haiku GART address semantics; locate and validate the Haiku-supported CPU/WC-to-MMIO ordering primitive; implement a fence as part of the same atomic submission batch; add wrap-safe sequence tracking and timeout diagnostics. The exact register/command sequence remains a blocker until independently verified against the platform reference.

**P2 — recovery and lifetime:** capture documented engine error state; implement bounded Gen9 BCS reset and post-reset validation; serialize reset with submissions; track allocation ownership; only reclaim ring/status-page memory after a proven stop/reset state.

**P3 — expand by family, not by a broad generation comparison:** add a platform capability table keyed by device family/stepping and engine availability; give each family its own command/fence/flush/reset operations where required. Start with the closest validated generations, and add Gen10, Gen11, Gen12 only after the same acceptance suite passes.

**P4 — legacy families:** audit Gen1–Gen5 separately. They must not inherit the Gen6+ BCS implementation, nor should a legacy HEAD/TAIL wait be upgraded to a “fence” without proving completion semantics. Decide per chipset whether a safe legacy acceleration path is supportable or software fallback is the correct outcome.

#### Acceptance criteria for each supported generation

A generation may be marked hardware-accelerated only after all of these are true: (a) exact PCI IDs and engine capabilities are enumerated; (b) valid 2D operations pass command-validation tests; (c) publication ordering is established for the actual ring-memory mapping; (d) a GPU-written completion fence is observed correctly under load and wrap/reset conditions; (e) timeout and engine-hang paths fail closed and recover or disable the engine safely; (f) allocation lifetime and teardown are tested; (g) software fallback works when acceleration is disabled or fails; and (h) the kernel driver and accelerant build together and pass hardware testing on that family. Until then, mark the family **unverified**, not supported.

#### Reference points for follow-up verification

- Linux i915 engine overview (RCS/BCS and command submission): https://www.kernel.org/doc/html/latest/gpu/i915.html
- Upstream i915 engine register definitions (cross-generation register differences): https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/gt/intel_engine_regs.h
- Upstream i915 register definitions: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/i915_reg.h

These are reference implementations and register indexes, not substitutes for the relevant Intel Programmer's Reference Manual or proof that Haiku's allocator and memory mappings have equivalent semantics.


### 14. Gen9 fence implementation gate: submission model and publication ordering

A follow-up source audit found a prerequisite that must be resolved before a Gen9 BCS breadcrumb can be called verified:

- The accelerant's `setup_ring_buffer()` directly programs the legacy ring START/HEAD/TAIL/CONTROL registers and writes the tail MMIO register to submit work.
- Gen8+ upstream i915/xe reference code uses logical-ring/execlist submission and programs per-engine context/status-page state. The upstream i915 legacy ring implementation explicitly limits its legacy ring default-vfunc path to pre-Gen8 hardware; this is a warning against assuming that a Gen9 BCS engine is operational merely because the legacy register block exists.
- A breadcrumb packet appended to an unverified submission model would not establish completion. Therefore, the code now fails closed for Gen8+ ring initialization instead of enabling this unverified direct-MMIO path. Hardware 2D remains disabled, and the Gen9 BCS completion fence is **not implemented yet**.

**Publication ordering improvement:** the ring tail-publication path now issues an x86 `SFENCE` after command memory writes and immediately before the MMIO tail doorbell. This is the appropriate architectural store-ordering primitive for draining prior write-combining stores before later stores; it closes the previously documented missing ordering step for the legacy ring path. It is not a completion fence, does not establish that the GPU-visible mapping is coherent, and does not make the Gen9 legacy submission model valid.

**Required next steps for a real Gen9 completion path:**

1. Implement or establish a supported Gen8+/Gen9 BCS submission model (including any required logical ring context/execlist setup), rather than layering a breadcrumb onto unverified direct ring submission.
2. Establish the exact status-page GGTT offset from Haiku's GART allocator and validate the status-page mapping/coherency contract. `B_APERTURE_NEED_PHYSICAL` alone does not prove the GPU address used by Gen8+ per-engine HWS_PGA.
3. Program the Gen9 BCS HWS_PGA register at the correct engine register base, with a verified GGTT address and posting readback, only as part of that supported engine initialization path.
4. Emit the Gen8+ BCS breadcrumb using the documented MI_FLUSH_DW GGTT store form and any applicable Skylake workarounds, as one atomic submission with the work it fences. Validate the address alignment constraints (including the documented address bit restriction) against the Skylake PRM.
5. Read the GPU-written sequence from the correct CPU mapping using the required read/cache ordering, compare wrap-safely, and bound the wait. On timeout, capture engine state and keep acceleration disabled unless a documented reset and post-reset validation succeed.
6. Build the kernel driver and accelerant together and test on real Skylake/HD 530 hardware. No build or physical-hardware result is claimed by this source change.

**References used for this gate (cross-checks, not a replacement for the Skylake PRM):**
- Upstream i915 engine register definitions: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/gt/intel_engine_regs.h (`RING_HWS_PGA(base)` is at engine base + `0x80`).
- Upstream i915 Gen8 breadcrumb/flush sequence: https://code.googlesource.com/linux/torvalds/linux/+/e387dc122fc7c70c2a5df2567f4e2d1114f5a5da/drivers/gpu/drm/i915/intel_lrc.c
- Intel Skylake PRM index and Blitter volume are linked in the pull request description.

This gate deliberately prioritizes correctness over exposing a partially working acceleration path. Do not remove the Gen8+ guard or advertise `B_2D_ACCELERATION` until the submission model, address mapping, fence, publication ordering, recovery, and hardware validation are complete.

### 15. GART allocation audit: status-page address domains and lifetime

A source trace of `intel_allocate_memory()` and the Haiku GART module establishes these address-domain facts:

- The allocator's `_base` output is the allocation's address in the mapped graphics aperture. Ring offsets are derived by subtracting `info.aperture_base`, and that relative offset is what the legacy `RING_BUFFER_START` register receives.
- When `B_APERTURE_NEED_PHYSICAL` is requested, `_physicalBase` is separately populated from the backing page's physical address. The kernel stores this in `physical_status_page`, while `status_page` is populated from the aperture-base output cast to a pointer.
- Therefore, `physical_status_page` is not a substitute for the GPU-visible GGTT offset required by Gen8+/Gen9 per-engine HWS programming. The driver now stores an explicit `status_page_offset` in shared info, computed as the status-page aperture address minus `info.aperture_base`, and validates page alignment, full-page aperture bounds, and 32-bit register fit before publishing it. If validation fails, it frees the allocation using the original aperture base and clears all status-page address fields. CPU pointer, physical backing address, and GGTT/aperture offset remain distinct. This candidate offset does not by itself establish correct Gen9 HWS_PGA programming or GPU/CPU polling coherency.
- The GART API exposes `free_memory(aperture, base)`; it requires the original allocation base, not a physical address or an offset. The current initialization code does not retain a separate explicit ownership ledger for each optional allocation. `intel_extreme_uninit()` unmaps the aperture, which owns the GART allocations, but does not independently prove the GPU has stopped accessing them before that unmap.

**Remaining implementation gate:** before adding Gen9 HWS_PGA or completion breadcrumbs, verify the applicable Skylake register/address requirements and status-page coherency semantics, and track each successful allocation's original base and ownership. The future teardown/recovery path must prove engine quiescence before releasing/unmapping any GPU-visible memory; a timeout or failed reset must not be treated as proof of quiescence. Do not pass `physical_status_page` to HWS_PGA by assumption, and do not add a fence that writes to an unverified address domain.

This is a source-level allocator audit only. No hardware fence, Gen9 submission path, build result, or HD 530 hardware test is claimed.

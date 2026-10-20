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
- The internal idle wait now returns an error for `INTEL_FAMILY_LAKE` and `INTEL_FAMILY_SOC0` rather than claiming idle; the public `void` hook logs the failure. This is a serious blocker. It needs a register/command-level root-cause investigation against Skylake PRM Vols. 6, 11, 13 and 16 before removing the guard.
- The existing rectangle and screen-to-screen operations use legacy XY blit command structures. Confirm command encodings, supported pixel formats, pitch/address units, clipping, alignment, fence/cache requirements, and valid framebuffer placement against the Skylake blitter PRM before enabling them.
- The `B_FILL_SPAN` hook and its one-row color-blit implementation are present, but must not be treated as proof that hardware acceleration is operational.

### 4. Display detection and EDID/DDC exist, but require targeted verification

`Ports.cpp` already contains EDID/DDC handling, DP AUX/I2C support, VBT-dependent I2C selection/fallback, and DDI port handling. The right next step is to audit sequencing and error reporting against Skylake display-engine documentation rather than replacing these mechanisms wholesale.

Priority checks:
- Verify AUX transactions, I2C-over-AUX, native AUX, retries, timeout cleanup, and link-training state transitions against PRM Vol. 12 and Vol. 16.
- Verify HDMI/DVI DDI mode selection, port/pipe/transcoder routing, DPLL selection, clock programming, lane/PHY setup, and supported color depth. HDMI audio and HDMI 2.0/HDCP features are separate capability paths and must not be inferred from basic video output.
- Verify miniDP as DP signaling on the board's actual DDI routing. A passive adapter does not create support for an otherwise unavailable signal/protocol.
- Preserve the distinction between “EDID read failed” and “monitor disconnected.” VBT and internal-panel fallback behavior should be tested on laptop and desktop configurations separately.
- In `DigitalDisplayInterface::IsConnected()`, review the fallback after EDID detection: its pipe scan appears to return based on the first pipe's enable state rather than establishing that an enabled pipe is routed to the current port. Confirm and correct this logic with a focused test before changing behavior.

### 5. Display modes and multiple monitors

The accelerant creates three pipes for generations 7–11, but pipe allocation alone does not guarantee correct multi-monitor support. Validate pipe/transcoder/port constraints, shared DPLL allocation, bandwidth, link training, and rollback on failure. Test clone/extended configurations, hotplug, suspend/resume, and failed modesets.

### 6. Required test gates before advertising acceleration

- Build the accelerant and kernel driver with Haiku's supported build system and treat warnings/errors as blockers.
- Static checks: command size/alignment, ring bounds, framebuffer pitch and base units, integer overflow, MMIO readback, and every timeout/error path.
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
- **Synchronization was falsely reporting completion; status propagation is now improved.** `intel_get_sync_token()` fills `engine_id` and a zero counter; `intel_sync_to_token()` validates its inputs and returns the internal wait status. The internal wait returns an error on the known-stalling Skylake/SOC families and propagates timeout failure on other families. The counter is not a true submission sequence, and there is no verified reset/recovery path, so this is not yet a complete synchronization implementation. Keep acceleration disabled until submission sequencing and recovery are validated.
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
- Gen6+ XY blit hooks and idle waiting select the BCS ring; pre-Gen6 devices continue to use the original primary ring.
- All blit hooks are withheld if the required ring is missing. Skylake/SOC software-rendering fallback remains in force.
- Ring setup now waits up to one second for an already-enabled ring to drain, disables it with a readback, clears head/tail, programs the start address, publishes an empty tail, enables the ring, and verifies control/head readback. It refuses to reset a ring whose live pointers are invalid or which does not drain.
- Teardown serializes with ring producers, waits briefly for pending commands, logs a timeout, disables the ring, and only then destroys the lock.

Still unresolved:
- The BCS ring's presence and MMIO offset are source-level matches to the Skylake register model, but they have not been tested on hardware.
- The exact `MI_FLUSH`/`MI_FLUSH_DW` sequence and the current `COMMAND_FLUSH` encoding must be verified for the BCS engine and Gen9. A flush command accepted by the render engine is not automatically valid for BCS.
- Proper completion tokens still need a monotonic sequence/breadcrumb mechanism; head==tail is only a coarse idle test.
- Reset/recovery still needs a Gen9-specific state machine and error/status capture, and hardware acceleration must remain disabled on Skylake until this path is tested.
- The shared-state layout change requires rebuilding the kernel driver and accelerant together.

Reference: [Intel Skylake Gen9 PRM Vol. 11 — Blitter](https://www.x.org/docs/intel/SKL/rev01/intel-gfx-prm-osrc-skl-vol11-blitter.pdf); [Linux i915 ring initialization](https://code.googlesource.com/linux/torvalds/linux/+/11e3b725cfc282efe9d4a354153e99d86a16af08/drivers/gpu/drm/i915/intel_ringbuffer.c).

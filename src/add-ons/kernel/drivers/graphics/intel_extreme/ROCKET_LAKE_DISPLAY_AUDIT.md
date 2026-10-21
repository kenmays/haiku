# Rocket Lake display-path source audit

**Audit date:** 2026-10-20  
**Project branch:** `work/intel-extreme-hd530-edid-2d`  
**Scope:** source-level review of the existing Gen12/Rocket Lake display path; not a build or hardware certification.

## Current code observations

- `driver.cpp` has explicit Rocket Lake entries for device IDs `0x4c8a`, `0x4c8b`, `0x4c90`, and `0x4c9a`, each mapped to `INTEL_MODEL_RKL`.
- `accelerant.cpp:init_common()` allocates four pipe objects when `Generation() >= 12`.
- `accelerant.cpp:probe_ports()` uses the DDI path for devices whose model reports `HasDDI()`; for Gen12 it probes ports through `INTEL_PORT_G`.
- `hooks.cpp` exposes the mode-setting, EDID, DPMS, and cursor entry points through the generic hook table. The source-level presence of these hooks does not prove that every register sequence or port type is correct on Rocket Lake.
- Hardware drawing hooks and engine/synchronization hooks remain unavailable. This is the required CPU-only 2D policy and must not be relaxed as part of display bring-up.
- The current static validator checked Rocket Lake IDs by searching the whole source text, which could pass if an ID appeared only in a comment or with the wrong model mapping. The validator is being tightened to require each ID in the active supported-device table and mapped to `INTEL_MODEL_RKL`.

## Follow-up fix from this audit

The mode-setting loop previously logged a per-port `SetDisplayMode()` failure but continued to return `B_OK`, potentially reporting a successful modeset when a connected output failed. The loop now preserves the first port error and returns it after updating shared framebuffer/mode state and restoring the requested power state. The static validator checks that this error propagation remains present. This is a correctness fix, not proof that the underlying Rocket Lake register programming or physical output works.

## Review risks and evidence still required

| Area | Source-level concern | Evidence required before claiming support |
|---|---|---|
| Device/model mapping | PCI ID matching alone does not prove the correct generation, PCH, or display model is selected. | Static validation of the active table; inspect the model and PCH detection paths. |
| Pipe/transcoder setup | A four-pipe allocation is only object allocation; it does not establish correct pipe/transcoder routing or limits. | Trace the full mode-set sequence against the relevant display-generation documentation and register definitions. |
| DDI and link training | Probing ports A-G is not proof of correct HDMI/DP/eDP identification, AUX transactions, lane configuration, or link training. | Per-port review and physical tests across supported connector and adapter types. |
| EDID/DDC/AUX | An EDID hook is advertised generically; probe success and correct EDID reads remain unverified here. | Monitor EDID read tests, including disconnect/reconnect and failure handling. |
| DPMS and hotplug | Hook exposure does not establish safe power transitions or reliable hotplug handling. | Repeated DPMS cycles and hotplug tests while checking for hangs, blank screens, and register errors. |
| Scanout memory and framebuffer | A mode-set API does not establish safe allocation, pitch/format handling, or scanout addresses for every mode. | Validate framebuffer layout, supported modes, memory bounds, and recovery from rejected modes. |
| Cursor | Cursor hooks may be exposed when cursor memory exists; memory presence alone does not prove correct cursor registers or format. | Cursor move/show/hide/shape tests, or keep cursor hooks disabled if unsupported. |

## Required validation stages

Keep the status of each stage separate:

1. **Source audit:** inspect the model-specific register paths and compare them with authoritative Intel/Linux display code.
2. **Static checks:** run `python3 src/add-ons/accelerants/intel_extreme/tests/validate_rkl_alchemist_battlemage.py` from the Haiku source root.
3. **Build:** build the affected driver and accelerant in a supported Haiku build environment.
4. **Boot/runtime:** verify driver loading, device enumeration, mode enumeration, and failure paths.
5. **Physical display validation:** test actual Rocket Lake hardware with EDID, mode changes, DPMS, hotplug, and scanout checks.

No build, boot, or physical hardware result is asserted by this document. Until those stages are completed, describe Rocket Lake as present in the source allowlist with source-level support under review, not as fully hardware-validated.

## Scope guardrails

- Keep CPU-only 2D rendering; do not enable blit/fill hooks, engine submission, or synchronization hooks.
- Do not infer Intel Extreme compatibility from Linux Xe PCI-ID coverage.
- Keep Alchemist/DG2 and Battlemage/BMG out of the active allowlist until their display path is separately implemented and reviewed.

## Follow-up audit: Gen12 DPLL and DDI mode-set blockers

A targeted review of `DigitalDisplayInterface::SetDisplayMode()` and `TigerLakePLL.cpp` found issues that should be treated as mode-set reliability blockers, not papered over by widening the PCI allowlist:

- **Fixed in the follow-up implementation checkpoint below:** the Gen12 DDI path now checks the returned status from `ProgramPLL(chosenPLL, p, q, k, dco)` and returns immediately on failure, before port-clock routing and timing programming.
- **Fixed in the follow-up implementation checkpoint below:** the unlock, power-state, and lock polls in `ProgramPLL()` now have 100 ms bounded waits and return `B_TIMED_OUT` on failure. Linux i915's corresponding DPLL code remains useful corroboration for bounded waits (see `intel_dpll_mgr.c`), but the Haiku change still needs a build and hardware test.
- **Fixed in the follow-up implementation checkpoint below:** the register header defines the DDIA and DDIB clock-select masks as fields at bits 1:0 and 3:2, respectively. The old `config &= TGL_DPCLKA_DDIA_CLOCK_SELECT` / `config &= TGL_DPCLKA_DDIB_CLOCK_SELECT` expressions discarded unrelated CFGCR0 fields, including other port selections and clock-off controls. They now clear only the selected port's field (`config &= ~...`) before setting its selection, preserving unrelated bits. This correction is source-reviewed against the local macro definitions; the full platform sequence still needs hardware validation.
- **The unsafe port-index heuristic has been removed.** The current Gen12 combo-PHY path explicitly maps DDI A to DPLL0 and DDI B to DPLL1, and rejects DDI C and later ports with `B_NOT_SUPPORTED` until their TC/MG PHY-specific PLL path is implemented. This is a conservative supported subset, not full multi-port support.
- **DP link-rate selection and retraining remain unimplemented.** The current Gen12 path uses the fixed 2.7 Gbit/s DP reference; it now validates 1/2/4-lane encodings and rejects modes whose 24/30/18/36-bpp payload exceeds the configured lane capacity with 5% headroom. The bandwidth check runs before modeset register writes. This avoids knowingly programming an over-capacity mode but does not implement higher DP link rates or link training.

### Safe next implementation step

Do not change PLL register sequencing based only on this source read. The first two safeguards are now implemented: `ProgramPLL()` uses bounded waits with explicit error returns, and `DigitalDisplayInterface::SetDisplayMode()` propagates its status. The DDI clock-select field masks have been corrected against the local register definitions; verify the full clock-routing sequence and concurrent-port behavior against the platform documentation and physical hardware before claiming support. Keep this separate from claims of physical support. Linux i915 provides useful corroboration for bounded PLL waits, but it is not a substitute for the platform-specific register sequence.

Evidence consulted:
- Intel Tiger Lake Display Engine, Volume 12, PLL and display clock programming: https://cdrdv2-public.intel.com/705833/intel-gfx-prm-osrc-tgl-vol-12-display-engine.pdf
- Linux i915 DPLL manager, which uses bounded register waits and reports timeout failures: https://gbmc.googlesource.com/linux/+/92a09c47464d040866cf2b4cd052bc60555185fb/drivers/gpu/drm/i915/display/intel_dpll_mgr.c

These are source-level findings only. No build, runtime, or physical display test is asserted.

## Hardware cursor review (Gen12 / Rocket Lake)

Source reviewed: `accelerants/intel_extreme/cursor.cpp`, `hooks.cpp`, `accelerant.cpp`, and the kernel's `intel_extreme.cpp`.

- The kernel allocates one page for cursor storage when `hardware_cursor` is enabled on generations below 12. Gen12+ cursor allocation is now skipped, and the accelerant hooks are gated off for Generation 12 and newer until the per-pipe cursor plane is implemented and tested.
- The cursor implementation writes a legacy-style cursor control, position, size, palette, and base register set via `INTEL_CURSOR_*` definitions. It copies AND/XOR masks into a 64-pixel-wide, two-plane layout and advertises the two-color cursor format.
- **Allocation is not proof of generation support.** The hook gate checks only for non-NULL cursor memory, not the device generation, active pipe, or whether the register definitions and cursor format match Rocket Lake's per-pipe display-plane model.
- Cursor register accesses are not visibly associated with the active pipe in `cursor.cpp`. The legacy path now rejects zero dimensions, dimensions above 64x64, null masks, and a missing allocation; it clears the fixed 64-row mask area to transparent before copying a new shape and preserves the previous visible/hidden state when updating shape registers. These are safety fixes for the legacy path, not proof of Gen12 cursor support.
- Cursor movement accepts unsigned 16-bit coordinates at the accelerant entry point. Negative/off-screen semantics should be verified against Haiku's accelerant contract and the hardware's signed position encoding before changing this code.
- **Recommendation:** keep the existing cursor path classified as legacy/unverified on Rocket Lake until `INTEL_CURSOR_*` definitions are traced for Gen12 and the driver selects/programs the correct cursor plane for the active pipe. Add tests for 64x64 and smaller shapes, hot spot changes, hide/show, all four screen edges, negative/off-screen coordinates, DPMS transitions, mode changes, and multi-display pipe routing. If Gen12 register/pipe support is not proven, gate cursor hooks by a known-good generation/model rather than treating allocated memory as capability.

## Hardware video overlay review (Haiku overlay accelerant API)

Source reviewed: `accelerants/intel_extreme/overlay.cpp`, `hooks.cpp`, `commands.h`, and the kernel's overlay allocation setup.

- The legacy overlay path implements one overlay channel, with `B_RGB15`, `B_RGB16`, `B_RGB32`, and `B_YCbCr422` source formats (with a narrower YCbCr422 list for the i965 group). It includes color-keying, scaling/filter coefficient generation, horizontal mirroring, clipping, and a buffer allocation path.
- Overlay register state is mapped only when the kernel sets a nonzero `overlay_offset`; initialization zeros a page-sized register image and seeds contrast/saturation fields. The legacy overlay update path submits `QueueCommands` operations to the primary ring and waits for overlay flip completion.
- **This overlay path is intentionally not exposed on Gen6 and newer.** `hooks.cpp` returns `NULL` for every overlay API hook from `B_OVERLAY_COUNT` through `B_CONFIGURE_OVERLAY` on Generation >= 6, because the legacy command path relies on a primary ring that is not initialized/validated for those generations. This is the right fail-closed policy for Rocket Lake; do not re-enable the legacy overlay hooks just because the display engine has plane/scaler hardware.
- The implementation has legacy-specific assumptions that require care even on older hardware: the configure path does not comprehensively validate buffer/view/window arguments before dereferencing or programming them; clipping comments acknowledge incomplete behavior; and it updates `select_pipe = 0` rather than selecting based on the actual active display pipe. This is not a safe template for a Gen12 overlay implementation.
- **Recommended Gen12 approach:** implement a modern display-plane path rather than resurrecting the legacy overlay engine. First inventory the Rocket Lake display planes/scalers and their per-pipe routing, then define a narrow supported-format set and validate stride, alignment, allocation size, source rectangle, destination rectangle, scaling range, clipping, color conversion, and buffer lifetime. Synchronize plane flips with vblank/commit completion and disable/release the plane cleanly on DPMS, mode changes, and client teardown. Keep the Haiku overlay API hooks unavailable until this is implemented and tested.

## Hardware-assist conclusion

Hardware cursor and hardware video overlay are separate from the general-purpose 2D engine. A display-plane implementation can potentially provide these assists while ordinary fills/blits remain CPU-rendered. The current source does not establish that Rocket Lake's cursor path is generation-correct, and the overlay API is deliberately disabled for Gen6+. No cursor or overlay behavior has been validated on physical Rocket Lake hardware. These findings are source-level only; no build, boot, or runtime test is asserted.


## Follow-up implementation checkpoint (2026-10-20)

The following source-level guardrails have now been implemented on this branch:

- `TigerLakePLL.cpp` replaces the three unbounded DPLL unlock, power-state, and lock polls with bounded waits (100 ms maximum per transition) and returns `B_TIMED_OUT` when a transition does not complete. Power-up and lock timeout paths attempt bounded rollback; failure to unlock is reported without an unbounded wait.
- PLL parameter inputs and reference frequency are validated before MMIO. DCO fractional rounding carries into the integer field instead of spilling a 16th fractional bit into a reserved register bit.
- `DigitalDisplayInterface::SetDisplayMode()` checks and propagates `ProgramPLL()` failure before continuing with DDI port-clock routing and timing programming. The outer per-port modeset path already preserves the first returned port error.
- The legacy cursor hooks are gated off for Generation 12 and newer because the current register set and pipe routing have not been demonstrated to be Gen12-correct.
- Legacy cursor shape inputs reject zero dimensions, dimensions above 64x64, null AND/XOR masks, and a missing cursor allocation.
- The static validator has been extended to guard these cursor restrictions, Gen6+ overlay-hook suppression, bounded PLL waits, and PLL error propagation.
- A dedicated plan now records the Gen12 cursor register audit, modern plane/scaler inventory, safety constraints, and test matrix: `accelerants/intel_extreme/tests/GEN12_CURSOR_OVERLAY_PLANE_PLAN.md`.

### Still open / not claimed fixed

- The `TGL_DPCLKA_CFGCR0` field-mask bug has been corrected against the definitions in `headers/private/graphics/intel_extreme/intel_extreme.h`; static validators now reject the old destructive masks. The complete clock-routing sequence still needs physical validation.
- The fixed-rate Gen12 DP path still does not train or retrain the link, select among higher DP rates, or verify a fresh link-training completion state. DDI C and Type-C/MG PHY ports remain explicitly unsupported in this path. The A/B PLL mapping is source-level only until tested on Rocket Lake hardware.
- The mode-set sequence still needs a full Rocket Lake audit of transcoder selection, DDI link training, data/link M/N programming, pipe enable/disable order, and recovery after an intermediate failure. The `_SetPortLinkGen8()` status is now propagated, and DP lane-width encodings are decoded explicitly as 1/2/4 rather than `encoded + 1`.
- No source validator, Haiku build, boot/runtime test, or physical Rocket Lake display test is claimed as passing by this checkpoint. Run the validator and supported build/test stages before treating these edits as verified.


## Follow-up audit: primary plane and scanout register mapping

A source review of `accelerants/intel_extreme/Pipes.cpp` found another Gen12 scanout item that must be resolved before describing Rocket Lake modesetting as validated:

- The generic `Pipe` constructor derives pipe offsets from fixed values (A=0, B=`0x1000`, C=`0x2000`, D=`0xf000`) and derives a primary-plane offset from `INTEL_PLANE_OFFSET` for B/C, while Pipe D has no corresponding plane offset in this code.
- `Pipe::Enable()` programs `INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset` and `INTEL_DISPLAY_A_CONTROL + fPlaneOffset`, using the legacy-named `DISPLAY_CONTROL_ENABLED` and tile-mode mask, and waits for vblank between enable/disable transitions.
- `ConfigureTimings()` also writes the generic pipe-size and primary-plane-size register names. The source contains generation-specific branches, but those branches alone do not demonstrate that every offset, field, latch rule, or plane address is correct for Rocket Lake.
- This is not enough evidence to conclude that scanout is wrong; it is a concrete register-mapping verification gap. Trace every register macro to its definition and documented Gen12 scope, then compare the exact order and per-pipe/per-plane addressing with Rocket Lake display-engine documentation and upstream Linux i915.
- Specifically verify primary-plane base/surface programming, stride/pixel format, tile/modifier assumptions, plane size versus pipe source size, required posting reads, vblank wait bounds, and the pipe/transcoder enable order. Confirm how Pipe D and any additional Gen12 planes are represented.
- Until this audit is complete, keep the active allowlist unchanged and avoid adding a new cursor/overlay implementation on top of assumptions inherited from older generations.

The Gen12 cursor/overlay plan in `accelerants/intel_extreme/tests/GEN12_CURSOR_OVERLAY_PLANE_PLAN.md` defines the plane inventory and physical test requirements. No register writes were changed in this follow-up because guessing at display-engine offsets or masks could make a working BIOS-configured display fail.


## Follow-up implementation checkpoint: link limits and scanout failure safety

Additional source changes after the earlier checkpoint:

- Gen12 DDI A/B now map explicitly to DPLL0/DPLL1. Gen12 DDI C and later ports return `B_NOT_SUPPORTED` before modeset register writes because their TC/MG PHY-specific PLL setup is not implemented. This intentionally narrows the claim instead of routing an arbitrary port through DDI A's clock field.
- DCO inputs are validated before MMIO: P/Q/K values, DCO range, and the reference clock must be supported. Fractional rounding carries into the integer field rather than spilling into a reserved fractional bit.
- The DDI link M/N helper now returns its error to the caller. DP lane-width encodings are decoded as explicit 1/2/4-lane values; reserved encodings are rejected.
- The Gen12 DP path uses a fixed 2.7 Gbit/s link reference. A payload-capacity check with 5% headroom runs before pipe/PLL/link writes. This does **not** replace DP link-rate selection or link training; modes exceeding the fixed link are rejected.
- If a port mode-set fails, the accelerant no longer restores DPMS over a partially configured pipe. It leaves the display off and attempts to replace the failed-mode framebuffer with a correctly sized buffer for the prior software mode. If that allocation cannot be restored, the stale framebuffer pointer is cleared so it cannot be dereferenced.
- On successful modesets, the new framebuffer surface is programmed before DPMS can re-enable scanout; pitch is then programmed with the pipe on and the base is re-applied to latch the final state. A null framebuffer pointer is no longer passed to the free ioctl on a later recovery attempt.
- Added a GitHub Actions source-guard workflow at `.github/workflows/intel-extreme-source-guards.yml`. It runs the Rocket Lake/PCI-ID, Gen1–8 CPU-only, and Gen10–12 fail-closed validators when matching source changes are pushed or proposed.

### Qualification status

These are source-level corrections and guardrails only. The three Python source validators passed in GitHub Actions at commit `1e87b3b08e769a4258cf11d9f4756579caa6ca10` ([run 39530427418](https://github.com/kenmays/haiku/actions/runs/39530427418)). This is static source validation, not a C++ build or runtime test. The Haiku source checkout could not be cloned into this execution environment because GitHub DNS resolution failed, so a supported Haiku build and physical Rocket Lake DP/HDMI testing are still required.

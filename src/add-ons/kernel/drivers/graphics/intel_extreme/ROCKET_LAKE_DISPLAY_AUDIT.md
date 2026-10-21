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
- **Port clock routing needs a register-mask audit.** The Gen12 path edits `TGL_DPCLKA_CFGCR0` using read/modify/write operations, including `config &= TGL_DPCLKA_DDIA_CLOCK_SELECT` / `config &= TGL_DPCLKA_DDIB_CLOCK_SELECT`. Before changing this sequence, verify the masks' exact bit definitions and the required preservation of unrelated port clock selections against the Rocket Lake/Tiger Lake display register documentation. Do not assume these expressions preserve unrelated fields.
- **DisplayPort DPLL choice is provisional.** The current code uses a fixed DisplayPort DPLL parameter set and a simplistic port-index choice (PLL 1 only for port index 7, otherwise PLL 0). The source itself notes the assignment is TODO. That is not sufficient evidence for concurrent outputs or all DDI ports.

### Safe next implementation step

Do not change PLL register sequencing based only on this source read. The first two safeguards are now implemented: `ProgramPLL()` uses bounded waits with explicit error returns, and `DigitalDisplayInterface::SetDisplayMode()` propagates its status. Audit the DPLL/port-clock masks against the matching register definitions before changing the clock-routing sequence. Keep this separate from claims of physical support. Linux i915 provides useful corroboration for bounded PLL waits, but it is not a substitute for the platform-specific register sequence.

Evidence consulted:
- Intel Tiger Lake Display Engine, Volume 12, PLL and display clock programming: https://cdrdv2-public.intel.com/705833/intel-gfx-prm-osrc-tgl-vol-12-display-engine.pdf
- Linux i915 DPLL manager, which uses bounded register waits and reports timeout failures: https://gbmc.googlesource.com/linux/+/92a09c47464d040866cf2b4cd052bc60555185fb/drivers/gpu/drm/i915/display/intel_dpll_mgr.c

These are source-level findings only. No build, runtime, or physical display test is asserted.

## Hardware cursor review (Gen12 / Rocket Lake)

Source reviewed: `accelerants/intel_extreme/cursor.cpp`, `hooks.cpp`, `accelerant.cpp`, and the kernel's `intel_extreme.cpp`.

- The kernel allocates one page for cursor storage when the `hardware_cursor` driver setting is enabled (default true). The original hook gate exposed cursor operations whenever that pointer was non-NULL; the follow-up implementation now additionally gates the legacy cursor hooks off for Generation 12 and newer.
- The cursor implementation writes a legacy-style cursor control, position, size, palette, and base register set via `INTEL_CURSOR_*` definitions. It copies AND/XOR masks into a 64-pixel-wide, two-plane layout and advertises the two-color cursor format.
- **Allocation is not proof of generation support.** The hook gate checks only for non-NULL cursor memory, not the device generation, active pipe, or whether the register definitions and cursor format match Rocket Lake's per-pipe display-plane model.
- Cursor register accesses are not visibly associated with the active pipe in `cursor.cpp`. The cursor shape path enables the cursor immediately, while visibility is tracked separately. There is no local bounds check for zero dimensions or a clear validation that all rows/bytes fit the one-page allocation, although the 64x64 limit makes the expected legacy image payload small.
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

- `TigerLakePLL.cpp` replaces the three unbounded DPLL unlock, power-state, and lock polls with bounded waits (100 ms maximum per transition) and returns `B_TIMED_OUT` when a transition does not complete.
- `DigitalDisplayInterface::SetDisplayMode()` checks and propagates `ProgramPLL()` failure before continuing with DDI port-clock routing and timing programming. The outer per-port modeset path already preserves the first returned port error.
- The legacy cursor hooks are gated off for Generation 12 and newer because the current register set and pipe routing have not been demonstrated to be Gen12-correct.
- Legacy cursor shape inputs reject zero dimensions, dimensions above 64x64, null AND/XOR masks, and a missing cursor allocation.
- The static validator has been extended to guard these cursor restrictions, Gen6+ overlay-hook suppression, bounded PLL waits, and PLL error propagation.
- A dedicated plan now records the Gen12 cursor register audit, modern plane/scaler inventory, safety constraints, and test matrix: `accelerants/intel_extreme/tests/GEN12_CURSOR_OVERLAY_PLANE_PLAN.md`.

### Still open / not claimed fixed

- The `TGL_DPCLKA_CFGCR0` read/modify/write masks and preservation of unrelated port clock selections remain unaudited against the exact register definition. Do not change them by guesswork.
- DisplayPort DPLL parameters and PLL-to-port assignment are still provisional. The current fixed DP DPLL and port-index heuristic are not evidence of reliable multi-output behavior.
- The mode-set sequence still needs a full Rocket Lake audit of transcoder selection, DDI link training, data/link M/N programming, pipe enable/disable order, and recovery after an intermediate failure.
- No source validator, Haiku build, boot/runtime test, or physical Rocket Lake display test is claimed as passing by this checkpoint. Run the validator and supported build/test stages before treating these edits as verified.

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

- **PLL status is not propagated.** The Gen12 DDI path calls `ProgramPLL(chosenPLL, p, q, k, dco)` but ignores its returned `status_t`. A bad PLL index or parameter rejection can therefore be followed by port clock routing and timing programming as though PLL setup succeeded.
- **PLL polling is unbounded.** `ProgramPLL()` contains `while` loops waiting for unlock, power-state assertion, and lock. If hardware does not reach the requested state, the caller can hang indefinitely rather than return a recoverable mode-set error. Linux i915's corresponding DPLL code uses bounded register waits and reports lock/power failures (see `intel_dpll_mgr.c`, for example the ICL PLL enable/power helpers).
- **Port clock routing needs a register-mask audit.** The Gen12 path edits `TGL_DPCLKA_CFGCR0` using read/modify/write operations, including `config &= TGL_DPCLKA_DDIA_CLOCK_SELECT` / `config &= TGL_DPCLKA_DDIB_CLOCK_SELECT`. Before changing this sequence, verify the masks' exact bit definitions and the required preservation of unrelated port clock selections against the Rocket Lake/Tiger Lake display register documentation. Do not assume these expressions preserve unrelated fields.
- **DisplayPort DPLL choice is provisional.** The current code uses a fixed DisplayPort DPLL parameter set and a simplistic port-index choice (PLL 1 only for port index 7, otherwise PLL 0). The source itself notes the assignment is TODO. That is not sufficient evidence for concurrent outputs or all DDI ports.

### Safe next implementation step

Do not change PLL register sequencing based only on this source read. First make `ProgramPLL()` use bounded waits with explicit error returns, propagate its status from `DigitalDisplayInterface::SetDisplayMode()`, and audit the DPLL/port-clock masks against the matching register definitions. Keep this separate from claims of physical support. Linux i915 provides useful corroboration for bounded PLL waits, but it is not a substitute for the platform-specific register sequence.

Evidence consulted:
- Intel Tiger Lake Display Engine, Volume 12, PLL and display clock programming: https://cdrdv2-public.intel.com/705833/intel-gfx-prm-osrc-tgl-vol-12-display-engine.pdf
- Linux i915 DPLL manager, which uses bounded register waits and reports timeout failures: https://gbmc.googlesource.com/linux/+/92a09c47464d040866cf2b4cd052bc60555185fb/drivers/gpu/drm/i915/display/intel_dpll_mgr.c

These are source-level findings only. No build, runtime, or physical display test is asserted.

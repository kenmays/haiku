# Intel Extreme Gen13–Gen16 CPU-only display bring-up

## Status and safety contract

This is a bring-up gate, not a claim that Gen13–Gen16 hardware is already supported.
The current Intel Extreme driver is a legacy display driver with generation-specific
register definitions and sequencing. A PCI ID must not be added to
`kSupportedDevices[]` merely because the GPU is Intel or its marketing name appears
to follow Gen12.

The target remains **CPU-only 2D rendering**:
- Keep `B_SCREEN_TO_SCREEN_BLIT`, `B_FILL_RECTANGLE`,
  `B_INVERT_RECTANGLE`, and `B_FILL_SPAN` unavailable to the Intel accelerant.
- Keep GPU command submission, blitter/BCS setup, and engine synchronization disabled
  unless separately implemented and validated.
- Hardware use is limited to a generation-correct display pipeline: framebuffer
  allocation/scanout, modesetting, connectors, EDID/DDC/AUX, DPMS, and cursor.
- Do not enable overlays on a new generation without separate format, scaling,
  allocation, and register validation.

A successful PCI match is not proof of modesetting support. Until the checks below
pass, unknown devices must remain unclaimed by this driver so the system can use its
fallback display path.

## Generation naming must be resolved by PCI ID

“Gen13–Gen16” is not sufficient as a hardware key. Intel CPU product generations,
Xe graphics architecture names, and display-engine generations do not map one-to-one.
For every proposed target, record:
- PCI vendor/device/revision ID from the actual machine;
- codename and graphics architecture;
- display-engine/display-version generation (not just CPU generation);
- whether the device is an integrated GPU or discrete GPU;
- SoC/PCH and display routing topology;
- the public PCI ID source and the source revision/date used.

Do not infer device IDs from neighboring products, and do not use broad PCI-ID masks
that could accidentally match unreviewed devices.

## Required implementation gates

### Gate 1 — device identity and fail-closed detection
- Add only exact, verified PCI IDs to `kSupportedDevices[]`.
- Map each device to a dedicated, correct `device_type`/generation descriptor.
  Do not label a newer display engine as Gen12 simply to pass existing branches.
- Verify `get_next_intel_extreme()` only publishes explicit allowlisted IDs.
- Ensure an unknown Gen13+ ID does not reach accelerant register writes.

### Gate 2 — display register model
Audit every register and helper reachable during initialization, modesetting, DPMS,
interrupt handling, cursor setup, port probing, and teardown. For each register,
record whether its offset, bit definitions, power well, posting-read requirement, and
write ordering are valid for the target display engine.

Do not reuse Gen12 offsets for a later engine based only on similar names. Add explicit
generation-specific helpers where register layout or sequencing differs. Validate
pipe/transcoder counts and legal routing rather than assuming the existing
`Generation() >= 12` four-pipe rule applies to every later architecture.

### Gate 3 — connector and power sequencing
- Identify eDP, DP, HDMI, USB-C/Type-C, and analog output capabilities from supported
  firmware/platform data; do not probe every possible port blindly.
- Validate DDI/Type-C routing, AUX channel mapping, HPD handling, GMBUS/DDC selection,
  EDID reads and retry reset on disconnect.
- Validate panel power sequencing and DPMS transitions against the target display engine.
- Keep waits bounded and propagate timeout/error status.
- Do not claim VGA support unless the board actually routes a DAC/adapter path supported
  by the engine.

### Gate 4 — framebuffer and CPU-only 2D
- Confirm the kernel's memory/aperture allocation and framebuffer mapping rules for
  the target architecture, including alignment, stride, size, and address width.
- Verify scanout format and mode validation without relying on GPU drawing.
- Keep all four 2D acceleration hooks returning `NULL`; test fills, inversion, screen
  copies, spans, text, clipping, overlap, resize, scrolling, and sustained redraw through
  the app_server CPU rasterizer.
- Check cursor memory format, dimensions, hotspot, signed off-screen coordinates, and
  whether the target engine supports the existing cursor register model. If uncertain,
  disable hardware cursor and use software cursor fallback.

### Gate 5 — automated checks, build, and hardware validation
- Add a source validator for each new family that checks allowlist coverage, CPU-only
  hooks, no BCS/ring submission, bounded waits, and generation-correct register paths.
- Run existing Gen1–8 and Gen9–12 validators, then the new validator.
- Build Haiku for the target architecture and inspect warnings/errors.
- Hardware-test cold boot, native/preferred mode, other advertised modes, color/stride
  patterns, cursor, DPMS, EDID, every physical connector, hotplug, suspend/resume, and
  30-minute redraw stability.
- Record exact PCI ID, firmware, board, monitor/adapter, build commit, logs, and each
  PASS/FAIL result. Test each materially different display engine separately.

## Initial test matrix template

| Target | PCI ID (exact) | Display-engine version | CPU-only drawing | Modeset/scanout | EDID/AUX/DDC | DPMS | Cursor | Hotplug | Status |
|---|---|---|---|---|---|---|---|---|---|
| Gen13 candidate | TBD | TBD | Not tested | Not tested | Not tested | Not tested | Not tested | Not tested | Not enabled |
| Gen14 candidate | TBD | TBD | Not tested | Not tested | Not tested | Not tested | Not tested | Not tested | Not enabled |
| Gen15 candidate | TBD | TBD | Not tested | Not tested | Not tested | Not tested | Not tested | Not tested | Not enabled |
| Gen16 candidate | TBD | TBD | Not tested | Not tested | Not tested | Not tested | Not tested | Not tested | Not enabled |

## Exit criteria

A target may be described as **source-enabled** only after exact IDs, the display-engine
mapping, register access paths, and fail-closed CPU-only policy are reviewed and its
source validator passes. It may be described as **build-tested** only after a clean
Haiku build. It may be described as **hardware-validated** only after the applicable
physical tests pass.

No Gen13–Gen16 PCI IDs are intentionally enabled by this document. This avoids
misidentifying a newer display engine and risking black screens or unsafe MMIO writes.
The next code change should be driven by exact target PCI IDs and a display-engine
register map, not by generation-number assumptions.

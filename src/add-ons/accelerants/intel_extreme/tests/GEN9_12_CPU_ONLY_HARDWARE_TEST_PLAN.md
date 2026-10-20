# Intel Extreme Gen9–Gen12 CPU-only 2D hardware validation

## Scope and status

This plan targets Intel Gen9, Gen10, Gen11, and Gen12 display devices listed in
`src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp`. It validates the
CPU-only 2D drawing policy together with the hardware display pipeline. It does not
validate 3D/OpenGL acceleration.

The accelerant must keep these hooks unavailable for every tested generation:
`B_SCREEN_TO_SCREEN_BLIT`, `B_FILL_RECTANGLE`, `B_INVERT_RECTANGLE`, and
`B_FILL_SPAN`. The app_server software rasterizer should implement those operations.
Modesetting, framebuffer allocation/scanout, connector detection, DPMS, and cursor
handling remain hardware-dependent and must be tested separately.

Gen6+ ring/BCS submission and engine synchronization remain fail-closed. Do not enable
hardware drawing or treat a successful boot as evidence that BCS works. A PCI allowlist
entry identifies a device only; it does not establish complete feature support.

## Representative test targets

| Internal generation target | Representative listed PCI ID | Family | Focus |
|---|---:|---|---|
| Gen9 | `8086:1912` | Skylake GT2 | Reference baseline; native panel/HDMI, CPU redraw |
| Gen9 | `8086:5912` | Kaby Lake GT2 | DDI/HDMI/DP, hotplug, cursor |
| Gen10 | `8086:5a52` | Cannon Lake GT2 | Candidate for source review and test; verify exact silicon and mode support |
| Gen11 | `8086:8a51` | Ice Lake GT2 | DP/eDP, AUX/DDC and native panel mode |
| Gen11 | `8086:4555` | Elkhart Lake | Low-power SoC display routing and power transitions |
| Gen12 | `8086:9a49` | Tiger Lake | DDI/DP/eDP, panel power and mode switching |
| Gen12 | `8086:4c8a` | Rocket Lake UHD 750 | Desktop HDMI/DP and multi-pipe routing |
| Gen12 | `8086:46a0` | Alder Lake-P | DDI/DP/eDP, mode stability and bounded EDID retry |
| Gen12 | `8086:a721` | Raptor Lake-P | Later Gen12 device-table coverage, only if the specific platform is supported |

These are representative entries, not a guarantee that every listed device is usable
with every board, firmware version, or connector topology. Verify the PCI ID from
`listdev` before testing. Mark a family as tested only after the exact device has
completed the exit criteria below.

## Pre-build checks

1. Start from the exact branch and record the commit SHA.
2. Run both source validators from the Haiku source root:

   ```sh
   python3 src/add-ons/accelerants/intel_extreme/tests/validate_gen10_12_fail_closed.py
   python3 src/add-ons/accelerants/intel_extreme/tests/validate_gen1_8_cpu_only.py
   ```

3. Build a fresh Haiku image with the intended architecture/toolchain. Record all
   compiler errors and warnings; do not treat static source checks as a build.
4. Boot with `intel_extreme` selected and save `listdev`, system log, PCI ID/revision,
   board/laptop model, firmware version, and connected monitor/cable details.
5. Keep a recovery path available before testing modes, DPMS, or hotplug.

## Test procedure for each physical target

### A. Verify CPU-only 2D dispatch

- Confirm all four drawing hooks return `NULL` in the built accelerant.
- Run a BView/app_server 2D test (not OpenGL): repeated fills, inverted rectangles,
  screen copies, spans, text, gradients, checkerboards, scrolling, window movement,
  resizing, overlap, minimize/restore, and full-screen redraws.
- Exercise rapid redraw while CPU and memory are under moderate load.
- Look for missing/stale pixels, wrong colors, stride corruption, cursor trails, hangs,
  or app_server/accelerant crashes.
- If runtime tracing is available, verify the drawing operations do not submit Intel
  blit commands. Do not enable BCS to “fix” software-rendering failures.
- Capture screenshots before and after stress, plus system logs.

### B. Framebuffer and modesetting

- Test the monitor's preferred/native mode and 800x600, 1024x768, 1280x720, and one
  additional supported mode; never force an unsupported pixel clock or panel timing.
- Test every advertised pixel format, at minimum RGB32 and RGB16 when exposed.
- Display color bars, gradients, checkerboards, and moving rectangles; inspect row
  stride, clipping, colors, stale scanlines, and mode-specific corruption.
- Perform at least 20 supported mode changes, including return to native mode.
- Trigger an invalid or rejected mode only through supported UI/API paths and confirm
  the previous framebuffer remains visible after failure.
- Test virtual dimensions/panning only if advertised.

### C. Cursor

- Move the cursor through all corners and edges, including negative/off-screen
  coordinates during window dragging.
- Test multiple cursor shapes, hotspot changes, visibility toggling, and rapid movement
  over changing backgrounds.
- Check cursor shape memory allocation, no trails, and correct cursor position after
  mode changes. Record cursor failures separately from software-drawing failures.

### D. DPMS, panel power, and recovery

- Test On -> Standby -> On, On -> Suspend -> On, and On -> Off -> On.
- Repeat each transition at least 10 times; repeat after a mode change and after a
  monitor disconnect/reconnect when supported.
- For eDP panels, verify panel power sequencing and native mode recovery after DPMS.
- A timeout or a display that stays black is a failure; record logs and do not hide it
  by forcing repeated power transitions.
- System suspend/resume is a separate test and should be reported separately from
  monitor DPMS.

### E. Connectors and EDID/DDC

- Test each connector actually present on the platform: HDMI, DisplayPort/mini-DP,
  eDP, DVI, VGA, or other listed output.
- Cold boot with a display attached; verify EDID read, preferred mode, and stable output.
- Unplug/replug and confirm bounded retries permit a later EDID read to recover.
- For DisplayPort, capture AUX errors, DEFER retries, empty replies, and short reads.
- For HDMI/DDC/GMBUS, capture bus setup/read failures and whether fallback succeeds.
- Test adapters separately from native connectors; label the exact adapter and topology.
- Multi-monitor operation is not a pass unless every tested connector/pipe arrangement
  is recorded.

### F. Stability

- Run at least 30 minutes of continuous 2D redraw/scroll workload.
- Perform at least 50 mode changes only if the platform remains stable and the modes are
  valid; stop if a black-screen or recovery failure occurs.
- Repeat three cold boots and, where practical, test a display reconnect on each boot.
- Watch for unbounded log growth, hangs, framebuffer corruption, and memory exhaustion.

## Result record

For every test machine, attach:

- Exact PCI vendor/device ID, revision, generation/family, and branch commit.
- Haiku build configuration and whether the build completed.
- Board/laptop and firmware versions.
- Monitor, connector, cable/adapter, EDID checksum, and preferred mode.
- Results for copy/fill/invert/span, framebuffer/modes, cursor, DPMS, EDID, hotplug,
  stability, and cold boot.
- PASS/FAIL/N/A for each item, severity, reproduction steps, screenshots, and logs.

## Exit criteria

A Gen9–Gen12 target is hardware-validated for CPU-only 2D only if the build succeeds,
all four 2D operations work while hardware drawing hooks remain unavailable, supported
modes/framebuffer operations are stable, and all applicable cursor, DPMS, connector,
EDID, hotplug, and stability tests pass.

A source validator passing is only a source-level result. No generation is considered
hardware-complete until its physical test record is complete. Gen10 and later must
remain on the fail-closed hardware-engine path until generation-correct submission,
completion fencing, recovery, and teardown are independently reviewed and tested.

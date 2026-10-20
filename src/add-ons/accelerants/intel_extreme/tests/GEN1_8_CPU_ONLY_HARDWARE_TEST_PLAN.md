# Intel Extreme Gen2–Gen8 CPU-only 2D hardware validation

## Scope and acceptance boundary

The accelerant's four 2D drawing hooks are intentionally unavailable: screen-to-screen
blit, fill rectangle, invert rectangle, and fill span return `NULL` from
`get_accelerant_hook()`. This allows app_server to use its CPU/software rasterizer.
Modesetting, framebuffer allocation/scanout, connector detection, DPMS, and the hardware
cursor remain hardware-driven; “CPU-only” describes 2D drawing, not the entire display
pipeline. Do not use 3D/GL performance as evidence that 2D software rendering works.

The current `intel_extreme` PCI device table does not include first-generation Intel
graphics (for example, i740). Do not mark Gen1 supported by this driver without adding
and validating a matching device implementation. The first listed family is i830/i845
(Gen2). The practical coverage target is therefore **Gen2–Gen8**, with Gen1 recorded as
not supported/not testable by this driver.

Static source validation is not a substitute for a successful Haiku build or physical
hardware test. This document is a test plan; only mark a row PASS after running it on
the named hardware and recording the Haiku revision, device ID, display connection, and
results.

## Test matrix

| Intel generation | Representative target (verify PCI ID with `listdev`) | Main risk areas |
|---|---|---|
| Gen1 | i740 / first-generation graphics | Not in the current `intel_extreme` device table; expected unsupported |
| Gen2 | i830/i845 family | Legacy mode timing, framebuffer stride, VGA/DDC |
| Gen3 | 915/945 family | Legacy PLL/modes, VGA and LVDS |
| Gen4 | 965/G33/G45 family | Framebuffer layout, cursor, LVDS/HDMI/DVI/VGA |
| Gen5 | Ironlake | PCH-based display power, digital ports, DPMS |
| Gen6 | Sandy Bridge | BCS setup must not be required for software drawing; modeset/DPMS/cursor |
| Gen7 | Ivy Bridge | Digital ports, DP AUX/DDC, multi-pipe mode selection |
| Gen7.5 | Haswell | DDI/DP/HDMI, PCH/port routing, hotplug and EDID |
| Gen8 | Broadwell | DDI/DP/eDP/HDMI, native panel mode, DPMS and cursor |

These are representative families, not a claim that every SKU, laptop firmware,
motherboard routing, or connector has been tested. For each generation, test at least
one desktop-style VGA/HDMI/DP configuration where physically available and one LVDS/eDP
laptop panel where available. Record unavailable connector types as N/A, not PASS.

## Preflight

1. Build a fresh Haiku image from the exact branch/revision under test; record the build
   architecture and compiler/toolchain. A source-only checkout is not a test result.
2. Boot with the Intel Extreme driver selected and capture `/var/log/syslog` plus
   `listdev` output. Confirm the detected PCI vendor/device/revision and accelerant.
3. Save the current display mode and ensure a recovery route exists (boot menu, alternate
   graphics option, serial/SSH if available). Do not run mode-switch tests remotely
   without a recovery route.
4. Run the static check:
   `python3 src/add-ons/accelerants/intel_extreme/tests/validate_gen1_8_cpu_only.py`

## Required functional tests (each supported generation)

### A. Confirm CPU-only 2D dispatch

- Launch a BView/ app_server-based 2D test, not an OpenGL test.
- Exercise screen-to-screen copies, solid fills, inverted rectangles, and spans.
- Repeat with rapid invalidation, overlapping windows, window moves/resizes, and full
  screen redraws. Look for stale pixels, corruption, tearing beyond ordinary scanout,
  hangs, or app_server/accelerant crashes.
- Confirm the four hardware drawing hooks are absent in source/static validation. If
  runtime tracing is available, verify no Intel 2D blit/fill command is submitted for
  these operations. A NULL hook is expected; do not treat it as a failure.
- Verify the display remains usable if ring/engine initialization reports unavailable.
  Capture logs. Do not infer that CPU drawing is correct merely because the desktop
  appears; exercise all four operations.

### B. Framebuffer, modes, and pixel formats

- Test native/preferred mode, 800x600, 1024x768, and 1280x720 where EDID and pixel-clock
  limits permit. Also test one higher supported mode.
- Test 32-bit RGB and 16-bit RGB where exposed by Screen preferences; test indexed 8-bit
  only if the accelerant actually advertises it.
- In each mode, run full-screen color bars, checkerboard, gradients, text scrolling,
  window compositing, and repeated redraws. Verify stride/row alignment, no wraparound,
  no stale rows, and correct colors.
- Switch modes repeatedly (at least 20 cycles), including returning to native mode.
  Confirm a failed mode change does not leave a blank display or stale framebuffer.
- Exercise virtual desktop dimensions/panning only if exposed by the mode list.

### C. Cursor

- Move cursor to all four corners and each screen edge; test negative/off-screen
  positions during window drags.
- Test hotspot-sensitive pointers, small and 64x64 shapes, visibility toggle, and rapid
  cursor movement over changing backgrounds.
- Confirm cursor remains independent of CPU-rendered window contents and does not leave
  trails. Record hardware-cursor defects separately from software drawing defects.

### D. DPMS and resume

- Exercise On -> Standby -> On, On -> Suspend -> On, and On -> Off -> On using Screen
  preferences or a supported DPMS test utility.
- Repeat transitions at least 10 times, then repeat after mode changes and after a
  monitor disconnect/reconnect. Verify no indefinite hang, black-screen lockup, or
  panel that remains powered off.
- For LVDS/eDP panels, record whether power-on/off polling times out. A timeout must be
  reported, not worked around by forcing repeated transitions.

### E. Connector, EDID/DDC, and hotplug

- For each available VGA, HDMI, DisplayPort/mini-DP, DVI, LVDS/eDP path: cold boot with
  display attached, read EDID, confirm preferred/native mode, unplug/replug where
  supported, and re-read EDID.
- Check that failed EDID reads remain bounded and a later reconnect can recover; capture
  logs for DDC/GMBUS or DP AUX failures.
- Test one direct cable and one adapter only if available. Do not report HDMI-over-DP
  adapter results as native HDMI support.
- Multi-monitor testing is exploratory; record each port and pipe mapping, but do not
  claim complete multi-display support unless every intended topology passes.

### F. Stability and regression

- Run a 30-minute continuous 2D redraw/scroll workload, then 50 mode changes if the
  hardware safely supports them.
- Run CPU/memory pressure alongside 2D redraws; watch for system hangs, corrupted
  framebuffer, memory exhaustion, or unbounded logs.
- Verify suspend/resume only if supported by the machine and test setup; it is distinct
  from monitor DPMS.
- Repeat cold boot three times. Keep syslog and the exact test steps for every failure.

## Result template

For every physical machine, record:

- Generation / product name / PCI vendor:device ID / revision:
- Haiku revision and build configuration:
- System board or laptop model / BIOS or firmware version:
- Connector, cable/adapter, monitor model, EDID checksum:
- Native mode and tested modes / color spaces:
- CPU-only drawing operations (copy/fill/invert/span):
- Cursor / DPMS / EDID reconnect / mode-switch results:
- Logs, screenshots, reproducer, PASS/FAIL/N/A and failure severity:

## Exit criteria

A generation is **hardware-validated for CPU-only 2D** only after: (1) a build succeeds,
(2) all four drawing operations pass with the hardware drawing hooks unavailable,
(3) mode/framebuffer stability and supported cursor/DPMS paths pass, (4) applicable
connector/EDID paths pass, and (5) the test record is attached to the result. Static
checks establish only source-policy invariants. Gen1 remains unsupported until its PCI
device and generation-specific display implementation are added and independently
validated.

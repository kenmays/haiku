# Rocket Lake, Alchemist, and Battlemage support status

## Policy

The Intel Extreme driver target remains **CPU-only 2D rendering**. Hardware is used
only for display scanout/modesetting and related display functions after the relevant
display engine is correctly implemented. Do not enable GPU blit/BCS/ring submission,
engine synchronization, or overlays as a shortcut to make a newer device work.

A PCI allowlist match is not a support claim. New devices must not be allowed to reach
legacy register writes until their display-engine generation, initialization sequence,
power wells, pipe/transcoder topology, PLLs, ports, AUX/DDC, DPMS, cursor, framebuffer
addressing, and teardown have been audited.

## Family-by-family status

| Family | Representative PCI IDs | Display model evidence | Current Intel Extreme status |
|---|---|---|---|
| Rocket Lake integrated graphics | 8086:4c8a, 4c8b, 4c90, 4c9a | Intel publishes a Rocket Lake PRM including Volume 4: Display Engine. Intel and Linux classify this family as Xe-LP / display version 12. | IDs already exist in driver.cpp as INTEL_MODEL_RKL. This is existing source allowlisting, not proof of successful build or physical validation. Audit the Gen12 path and validate each board's connectors/DPMS/cursor. |
| Arc Alchemist discrete graphics | 8086:56a0 A770, 56a1 A750, 56a2 A580, 56a5 A380, 56a6 A310; mobile and Pro IDs also exist | Intel publishes the 2023 Alchemist/Arctic Sound-M PRM family. Upstream Linux uses a separate display model (display version 13 / DG2), not the Rocket Lake display version 12 model. | Not enabled. The current generic legacy display path is not sufficient evidence of DG2 display-engine compatibility. Add a dedicated display-device descriptor and audited register/port paths before allowlisting. |
| Arc Battlemage discrete graphics | 8086:e20b B580, e20c B570, e211 B60, e212 B50; additional Pro SKUs exist | Battlemage is Xe2. Upstream Linux distinguishes Battlemage display version 14.1 from Alchemist display version 13. Intel's public GPU architecture material is not, by itself, a display-register programming specification. | Not enabled. Requires its own display-engine audit; do not reuse the Gen12 or Alchemist path by assumption. Confirm the applicable public PRM/register reference before implementation. |

PCI IDs above are examples, not an exhaustive allowlist. Confirm the exact device ID
from the target machine and the current Intel hardware table before adding any ID.
Do not use broad ID masks.

## Implementation sequence

### 1. Rocket Lake — validate existing support

- Keep only the explicit existing Rocket Lake IDs unless a new ID is independently
  verified.
- Audit the INTEL_MODEL_RKL / Gen12 branches in device_type, mode.cpp, Pipes.cpp,
  Ports.cpp, pll.cpp, dpms.cpp, cursor code, and kernel driver initialization.
- Verify four-pipe assumptions, DDI port bounds, transcoder selection, PLL selection,
  framebuffer address/stride limits, interrupt/vblank handling, and panel/connector
  recovery against Rocket Lake's display-engine PRM.
- Run the family validator and Gen10–12 fail-closed validator, then a Haiku build.
- Hardware-test native/preferred mode, HDMI/DP, EDID, hotplug, DPMS, cursor, mode changes,
  and sustained CPU-rasterized redraw. Report each target separately.

### 2. Alchemist — implement a separate display model before enabling IDs

- Add explicit DG2/Alchemist device descriptors and PCI IDs only after source review.
- Separate display-engine identification from the driver's older marketing-generation
  enum. Do not encode Alchemist as Gen12 just to reuse existing code.
- Audit MMIO register definitions and initialization/power-well requirements, pipe and
  transcoder topology, DDI/Type-C routing, AUX, hotplug, PLLs, DPMS, cursor, and memory
  scanout constraints against the applicable PRM and upstream display implementation.
- Leave hardware drawing hooks and engine/sync hooks unavailable. CPU rasterization does
  not remove the need for a valid display engine and correct scanout memory setup.
- Until those items are implemented and source/build-tested, leave all Alchemist IDs
  unclaimed so the system can use a fallback display path.

### 3. Battlemage — implement a separate Xe2 display model

- Treat Battlemage as a distinct display engine (display version 14.1 in upstream Linux),
  not as Alchemist or Gen12.
- Verify the applicable register programming reference before writing MMIO. Identify
  the Xe2 display register offsets, power domains, pipe/transcoder and port topology,
  link training/AUX, Type-C routing, clocking, DPMS, cursor, and framebuffer constraints.
- Implement a dedicated device descriptor and generation-specific operations; unsupported
  operations must return a clean error or remain unavailable rather than touching
  guessed registers.
- Keep the PCI allowlist closed until the initialization and modeset path is reviewed,
  built, and tested on physical Battlemage hardware.

## CPU-only rendering invariants

For all three families:
- B_SCREEN_TO_SCREEN_BLIT, B_FILL_RECTANGLE, B_INVERT_RECTANGLE, and B_FILL_SPAN remain
  unavailable and are handled by Haiku's software rasterizer.
- Engine acquisition, release, idle wait, and sync-token hooks remain unavailable.
- Do not enable overlays on an unvalidated display engine.
- Hardware cursor must be disabled if the target's cursor format/registers are not
  confirmed; software cursor fallback is preferable to corrupting display state.
- Waits must be bounded, failed EDID/AUX transactions must be recoverable, and failed
  modesets must not leave the display permanently blank.

## Source-level validator

Run from the Haiku source root:

    python3 src/add-ons/accelerants/intel_extreme/tests/validate_rkl_alchemist_battlemage.py
    python3 src/add-ons/accelerants/intel_extreme/tests/validate_gen10_12_fail_closed.py

The first script checks source policy and expected PCI allowlist state only. It is not a
compiler test and does not establish that Rocket Lake, Alchemist, or Battlemage works.

## Completion criteria

Report these states independently for each family: source-reviewed, source-validator
passed, build-tested, boot-tested, and hardware-validated. Hardware validation requires
recorded PCI ID/revision, board and firmware, monitor/adapter topology, logs, and
pass/fail results for cold boot, native mode, framebuffer redraw, mode changes, EDID,
DPMS, cursor, hotplug, and stability. No family should be described as fully supported
until its applicable checks pass.

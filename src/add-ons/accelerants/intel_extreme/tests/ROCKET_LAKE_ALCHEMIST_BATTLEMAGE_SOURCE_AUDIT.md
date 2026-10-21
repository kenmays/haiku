# Source audit: Rocket Lake, Alchemist, Battlemage

Audit date: 2026-10-20
Branch: work/intel-extreme-hd530-edid-2d

This is a source inspection report, not a hardware certification.

## Findings

### Rocket Lake

- \`src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp\` explicitly lists
  PCI IDs \`8086:4c8a\`, \`4c8b\`, \`4c90\`, and \`4c9a\` as \`INTEL_MODEL_RKL\`.
- The kernel driver has Gen8-era interrupt helpers that include Gen12 conditionals
  (for example, pipe D is permitted when \`Generation() >= 12\`). This proves that
  some later-generation assumptions exist, not that every Rocket Lake pipe/port
  configuration is correct.
- \`src/add-ons/accelerants/intel_extreme/mode.cpp\` has a newer-family framebuffer
  path using \`INTEL_DISPLAY_A_SURFACE\` and a family-specific display offset. It
  classifies certain newer families using broad family/group predicates. This must
  be cross-checked against the Rocket Lake display-engine PRM, especially base vs
  surface addressing, stride, plane enable sequencing, and framebuffer memory limits.
- \`src/add-ons/accelerants/intel_extreme/Ports.cpp\` still contains generic legacy
  port/pipe assignment logic alongside DDI-specific paths. An RKL audit must verify
  that each detected port uses the correct DDI/AUX/PLL/transcoder sequence.
- CPU-only 2D drawing hooks are intentionally unavailable in \`hooks.cpp\`. Keep
  them unavailable; that is not a blocker for display scanout itself.

**Decision:** retain the existing explicit Rocket Lake IDs, but do not claim the
family fully validated. Do not expand the allowlist until the source audit, build,
and physical tests pass.

### Alchemist

- Intel's public PRM index lists a 2023 Alchemist/Arctic Sound-M platform manual
  with a Display Engine volume. That is a valid source for a future DG2 display path.
- The current driver's supported-device table does not include Alchemist IDs.
- Reusing the Rocket Lake/Gen12 descriptor would be unsafe without separate display
  version detection and register/topology handling.

**Decision:** keep Alchemist IDs disabled until a dedicated display-engine descriptor
and audited mode/port/DPMS/cursor paths exist.

### Battlemage

- Intel's public PRM index currently documents Alchemist and Rocket Lake but does not
  list a Battlemage/Xe2 display-engine PRM. An Intel Community support response from
  August 2025 also stated that a public Xe2 PRM was not then available. This status
  should be rechecked before any future implementation.
- A general Xe2 architecture overview is not a register programming specification.
- The current driver's supported-device table does not include Battlemage IDs.

**Decision:** keep Battlemage IDs disabled. Do not guess register offsets or reuse the
Alchemist display path. Implement only when a sufficiently authoritative display
register specification and a matching reviewed implementation are available.

## Required validation matrix

For each target machine, record the exact PCI vendor/device/revision, board/system
firmware, monitor and adapter chain, and test logs. Pass each test independently:

1. Cold boot and repeated warm reboot.
2. EDID over DDC/AUX; disconnect/reconnect; failed read recovery.
3. Native/preferred mode and a safe fallback mode.
4. CPU-rendered framebuffer redraw under load; verify stride, pixel format and surface
   base remain correct after mode changes.
5. DPMS off/on and suspend/resume where supported.
6. Hardware cursor only when its register format and bounds are confirmed; otherwise
   use software cursor.
7. Hotplug and mode changes; no hangs on missing monitors or link-training failure.
8. Repeated modesets and extended stability run; no blank screen, MMIO fault, or lockup.
9. Haiku build plus all source validators.

Do not mark a family "supported" solely because the PCI ID is recognized or a display
lights up once. Track source-reviewed, build-tested, boot-tested, and hardware-validated
as separate states.

## References

- Intel PRM index: https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/overview.html
- Rocket Lake PRM (includes Volume 4: Display Engine):
  https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/rocket-lake.html
- Alchemist/Arctic Sound-M PRM:
  https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/alchemist-arctic-sound-m.html
- Intel support article on PRM availability:
  https://www.intel.com/content/www/us/en/support/articles/000101642/graphics.html
- Intel Community Xe2 PRM discussion (availability status must be rechecked):
  https://community.intel.com/t5/Graphics/Xe2-Programmer-s-Reference-Manual/td-p/1708752

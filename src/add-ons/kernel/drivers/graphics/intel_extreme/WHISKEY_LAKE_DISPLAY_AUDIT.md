# Whiskey Lake display support audit

Branch: `work/intel-extreme-hd530-edid-2d`

Status: **audit/design only; Whiskey Lake IDs are not enabled by this document.** The goal is CPU-only 2D rendering: the CPU draws into a framebuffer and the display engine scans it out. BCS/GPU command submission is not required.

## Current branch findings

- `driver.cpp` uses an explicit PCI device allowlist. It contains Coffee Lake IDs including `0x3ea5` and `0x3ea6`, but no explicit Whiskey Lake entries such as `0x3ea0` or `0x3ea1`.
- The existing allowlist maps devices into existing model/group types. Adding an ID is therefore not just enumeration: the selected type controls generation/platform branches used by the kernel driver and accelerant.
- The accelerant contains DDI/EDID handling in `Ports.cpp`, pipe/timing/plane setup in `Pipes.cpp`, and generation-dependent pipe construction in `accelerant.cpp`. These are candidate reusable paths, not evidence of Whiskey Lake compatibility.
- The current code's Skylake-style PLL path contains assumptions about PLL selection/routing and lock status that need explicit review before enabling an untested platform.
- CPU-only rendering does not need BCS. Keep the current fail-closed BCS gates intact.

## PCI-ID evidence and caution

The Linux i915 PCI-ID tables group Whiskey Lake-U and Coffee Lake-U IDs in overlapping SKU macros. Public references include:

- Linux i915 PCI ID changes for WHL/CFL-U GT1/GT2/GT3: https://www.spinics.net/lists/intel-gfx/msg176692.html
- Current Linux i915 device table: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/i915_pci.c
- DRM device database: https://drmdb.emersion.fr/devices?driver=i915

Examples include `8086:3EA0` (Whiskey Lake-U GT2) and `8086:3EA1` (Whiskey Lake-U GT1); some neighboring IDs are shared/reclassified between WHL/CFL-U SKU groups. Treat names and SKU labels as evidence to cross-check, not as proof that the Haiku driver's existing `INTEL_MODEL_CFL` path is correct for every ID.

**Decision:** do not add a broad wildcard or infer platform compatibility from the generation alone. Before enabling each ID, record the exact PCI ID, the intended Haiku device type, the resulting generation/DDI/platform flags, and why the display register paths apply.

## Required before enabling a Whiskey Lake ID

1. **Classification:** trace the chosen model/group through `Generation()`, `HasDDI()`, PCH/platform metadata, and any special-case tests in both driver and accelerant. Confirm no branch accidentally selects a legacy PCH/PLL path.
2. **Firmware handoff:** inspect BIOS/VBT-derived state and determine which display resources are already initialized by firmware. Do not assume a cold-init sequence works simply because an already-lit screen survives driver attachment.
3. **Connector detection:** validate DDI port mapping and AUX/I²C EDID reads. Test external DisplayPort/HDMI separately from internal eDP.
4. **Mode setting:** review pixel clock calculation, PLL selection/lock handling, transcoder routing, pipe/plane format, stride, base address, and vblank waits against Whiskey Lake-relevant documentation and Linux reference code.
5. **Framebuffer safety:** prove that the framebuffer allocation and mapping remain valid for the display engine's entire scanout lifetime. CPU drawing must update the memory actually scanned out.
6. **Regression:** test the established HD 530 system after any shared-path change. Record Whiskey Lake results separately; HD 530 success is not Whiskey Lake validation.

## Validation status

The user reports a successful Haiku build and real HD 530 hardware test on this branch. This is treated as the established HD 530 baseline, but it was not independently reproduced in this audit. It does not establish Whiskey Lake compatibility or BCS completion/recovery safety.

No Whiskey Lake hardware test or build result is claimed by this document. No Whiskey Lake PCI IDs are enabled as part of this audit.

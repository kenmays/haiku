# Linux Xe PCI-ID evidence and Intel Extreme policy

**Audit date:** 2026-10-20  
**Project branch:** `work/intel-extreme-hd530-edid-2d`

## Interpretation rule

For this project, an Alchemist/DG2 or Battlemage/BMG PCI ID is **PCI-ID validated** when it appears in the upstream Linux Xe PCI-ID definitions and is included by the Xe PCI device table. This is an identification-evidence classification only. It does **not** mean Intel Extreme has a compatible display engine, working modesetting, a safe register path, or physical hardware validation.

This distinction is intentional: the Intel Extreme target remains CPU-only 2D rendering, with hardware used for scanout/modesetting only after the relevant display path is audited. Do not add IDs to Haiku's active allowlist merely because Linux Xe recognizes them.

## Upstream source checkpoints

- Xe device descriptors and PCI table: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/xe/xe_pci.c
  - DG2/Alchemist descriptors and subplatform ID arrays are built from `INTEL_DG2_G10_IDS`, `INTEL_DG2_G11_IDS`, and `INTEL_DG2_G12_IDS`.
  - The Xe PCI table includes `INTEL_DG2_IDS(..., &dg2_desc)`.
  - Battlemage uses a separate `bmg_desc` and `INTEL_BMG_IDS(..., &bmg_desc)` entry.
- PCI-ID macro definitions: https://github.com/torvalds/linux/blob/master/include/drm/intel/pciids.h

Because these macros are maintained upstream, re-check the current header at review time rather than treating a hand-copied list as exhaustive. Record the Linux commit/revision used for any future exhaustive ID snapshot.

## Family classification

| Family | Upstream Xe evidence | Intel Extreme state |
|---|---|---|
| Alchemist / DG2 | DG2 device descriptor, G10/G11/G12 ID arrays, and DG2 PCI table entry | PCI-ID evidence may be marked validated for IDs present in those macros. Keep IDs out of the active Intel Extreme allowlist until a dedicated display model and register paths are reviewed. |
| Battlemage / BMG | Separate Battlemage descriptor, G21 ID array, and BMG PCI table entry | PCI-ID evidence may be marked validated for IDs present in those macros. Keep IDs out of the active Intel Extreme allowlist until a dedicated Xe2 display model and register paths are reviewed. |

## What is not established by Xe PCI-ID presence

- Intel Extreme build or runtime compatibility.
- Display-engine register compatibility with Gen12 or DG2.
- Correct power-well, pipe/transcoder, PLL, DDI/Type-C, AUX/DDC, hotplug, DPMS, cursor, or scanout-memory behavior.
- Hardware rendering support. Engine submission, blits, synchronization hooks, and overlays remain out of scope.
- Physical validation on any board, firmware, monitor, or adapter topology.

## Required next review

1. Pin the upstream Linux revision used for the audit.
2. Expand the DG2 and BMG PCI-ID macros from `include/drm/intel/pciids.h` and record the exact IDs/revision.
3. Compare that list with the active Intel Extreme `kSupportedDevices` table; no Alchemist/Battlemage IDs should enter it until the display implementation gate is satisfied.
4. Keep source review, static validator, build, boot, and physical hardware validation as separate status fields.

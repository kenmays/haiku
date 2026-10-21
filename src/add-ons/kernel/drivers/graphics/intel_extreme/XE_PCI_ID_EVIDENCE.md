# Linux Xe PCI-ID evidence and Intel Extreme policy

**Audit date:** 2026-10-20  
**Linux source:** `torvalds/linux` `include/drm/intel/pciids.h`, blob `dff389b56eb3bf0ebcf9d8a81c2e76722a0ea54f`  
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

## Current upstream PCI-ID macro evidence

The reviewed Linux header blob defines these IDs in the Xe-consumed macros:

- **DG2 / Alchemist:** `0x56A0`, `0x56A1`, `0x56A2`, `0x56BE`, `0x56BF`, `0x5690`, `0x5691`, `0x5692`, `0x56A5`, `0x56A6`, `0x56B0`, `0x56B1`, `0x56BA`, `0x56BB`, `0x56BC`, `0x56BD`, `0x5693`, `0x5694`, `0x5695`, `0x56A3`, `0x56A4`, `0x56B2`, `0x56B3`, `0x5696`, `0x5697`.
- **Battlemage / BMG:** `0xE202`, `0xE209`, `0xE20B`, `0xE20C`, `0xE20D`, `0xE210`, `0xE211`, `0xE212`, `0xE216`, `0xE220`, `0xE221`, `0xE222`, `0xE223`.

The list above is a snapshot of the named macro groups in that exact upstream header blob, not a promise that it is exhaustive forever. The test guardrail intentionally prevents these known IDs from being added to the active Haiku allowlist before display support is implemented and reviewed.

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

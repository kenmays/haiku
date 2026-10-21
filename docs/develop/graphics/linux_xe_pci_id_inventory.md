# Linux `xe` PCI-ID inventory (upstream-listed; not hardware-validated)

Source: Linux mainline `drivers/gpu/drm/xe/xe_pci.c` at commit 5f2a0b19839d16a6608101e1010943a800722a16, and `include/drm/intel/pciids.h` at commit dff389b56eb3bf0ebcf9d8a81c2e76722a0ea54f.
Extracted 20 unique device IDs from the PCI-ID macros referenced by the xe driver's PCI table.

**Status semantics:** `UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED` means the ID appears in the upstream Linux xe PCI-ID macro set. It does not mean the Haiku driver supports initialization, modesetting, DPMS, or safe operation on that device. Do not use this inventory alone to enable a PCI ID in `intel_extreme`.

| PCI device ID | xe PCI table macro | Haiku validation status |
|---:|---|---|
| 0x4680 | `INTEL_ADLS_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x46a0 | `INTEL_ADLP_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x46d0 | `INTEL_ADLN_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x4905 | `INTEL_DG1_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x4c80 | `INTEL_RKL_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x56a0 | `INTEL_DG2_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x56c0 | `INTEL_ATS_M_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x6420 | `INTEL_LNL_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x674c | `INTEL_CRI_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x7d40 | `INTEL_MTL_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x7d51 | `INTEL_ARL_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0x9a60 | `INTEL_TGL_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0xa720 | `INTEL_RPLP_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0xa721 | `INTEL_RPLU_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0xa780 | `INTEL_RPLS_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0xb080 | `INTEL_PTL_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0xd740 | `INTEL_NVLS_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0xd750 | `INTEL_NVLP_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0xe202 | `INTEL_BMG_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |
| 0xfd80 | `INTEL_WCL_IDS` | UPSTREAM_LISTED_NOT_HARDWARE_VALIDATED |

## DPMS audit

The accelerant already registers `B_DPMS_CAPABILITIES`, `B_DPMS_MODE`, and `B_SET_DPMS_MODE` in `hooks.cpp`, and `dpms.cpp` implements the four Haiku DPMS states. This is legacy `intel_extreme` DPMS, not proof of modern Xe display-engine support. In particular, the current implementation's port programming is incomplete for digital outputs and includes legacy PLL/pipe assumptions. Newer Xe IDs must remain unvalidated until per-generation display power sequencing and connectors are implemented and tested.

## Validation gates before enabling IDs

- Confirm device's graphics/display IP generation and supported connector topology.
- Validate kernel initialization, MMIO/register map, framebuffer/scanout layout, mode setting, suspend/resume, DPMS, and hotplug on hardware or a reliable automated hardware test setup.
- Keep PCI IDs listed for inventory separate from IDs accepted by the operational driver's supported-device table.
- Never map a new generation to an older `device_type` only to make the PCI probe succeed.

# PPC64 Power Mac G5 port

## Implemented

- PPC64 Book III-S exception entry/return and context switching.
- Native hashed-page-table MMU and SLB support.
- G5 U3/U3H/U4 platform discovery and OpenPIC/MPIC interrupt routing.
- Apple DART initialization and 32-bit DMA mapping primitives.
- U3/U4 PowerMac PCI host-controller config access and Open Firmware `ranges` translation.
- K2 SATA controller support with BMDMA PRDs and DART-backed 32-bit DMA.
- K2 OHCI DMA/cache-ordering helper and PPC memory-ordering primitives.
- Open Firmware CPU-node discovery primitives are available to the platform.
- PPC64 libroot syscall ABI smoke test is included in the system/libroot test tree.
- QEMU PowerMac7,3 smoke-test launcher is present and deliberately refuses to silently fall back to `mac99`.

## Hardware-specific constraints

### K2 GMAC / Sun GEM

The PowerMac7,3 K2 GMAC is PCI ID `106b:004c`. The hardware is a Sun GEM-family controller with an Apple K2 PHY path. The FreeBSD GEM implementation is BSD-2-Clause licensed and is the preferred reference for a native Haiku network-stack port; the Linux sungem implementation is GPL-2.0 and is reference material only.

The current branch does **not** claim a finished packet-path GEM driver. A driver is only complete when the Haiku network-device ABI, GEM TX/RX rings, MIF/PHY, interrupt handling, multicast filtering, and DART-backed descriptor/buffer lifetime management have all been compiled and exercised.

### K2 OHCI

K2 OHCI controllers are PCI ID `106b:0040`. The generic Haiku OHCI HCD already recognizes standard OHCI and can be reused once the PowerMac PCI resources, IRQs, and DMA addresses are correct. The K2 DMA helper is intentionally isolated from the generic HCD until descriptor/data-buffer mapping lifetime can be represented correctly rather than leaking DART mappings.

### Secondary CPUs

PowerPC Open Firmware specifies the `start-cpu` client service as:

`start-cpu(nodeid, pc, arg)`

with the started CPU entering at a real-mode PC and receiving `arg` in r3. The branch therefore must use a small real-mode secondary trampoline. The current `g5_smp.cpp` prepares a cache-line-aligned release record but does not falsely report startup success without a verified physical-address/trampoline path.

## Validation status

The repository has not been cross-built in this environment, and no physical Power Mac G5 or current upstream QEMU `powermac7_3` machine is available here. Therefore the following remain hardware-validation gates rather than claimed successes:

1. PPC64 cross-toolchain build.
2. Kernel link/relocation validation.
3. Open Firmware handoff on real G5 hardware.
4. Single-CPU MMU/interrupt/timer validation.
5. Secondary CPU release and SMP IPI validation.
6. PCI enumeration and OF `ranges` validation.
7. DART DMA validation.
8. K2 SATA DMA validation.
9. K2 OHCI USB transfers.
10. K2 GMAC packet TX/RX and PHY link.
11. Native Radeon/GeForce modesetting/accelerant path.
12. Full PPC64 libroot syscall conformance execution.

A green source-tree state must not be interpreted as a green hardware-validation state.

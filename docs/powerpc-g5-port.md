# Power Mac G5 / PPC64 port

This branch is the dedicated 64-bit PowerPC port for Apple Power Mac G5 systems using PPC970/970FX/970MP CPUs and Open Firmware.

## Target architecture

* CPU: IBM PPC970 family
* Firmware: Apple Open Firmware
* Northbridge: U3/U3H
* I/O: K2/Mac-IO family
* Interrupts: Apple/MPIC-style controller exposed by the device tree
* Memory management: Book III-S hash MMU + SLB

PPC970 is not a 32-bit PPC extension. The existing `src/system/kernel/arch/ppc` implementation uses 32-bit segment registers, BATs and 32-bit exception frames, so PPC64 has a separate architecture header and assembly layer.

## Current implementation

* `headers/private/kernel/arch/ppc64/arch_cpu.h`
  * 64-bit exception frame
  * PPC970 MSR definitions
  * 64-bit SPR interfaces
  * SLB primitive declarations
* `src/system/kernel/arch/ppc64/arch_cpu_asm.S`
  * SDR1 access
  * 64-bit MSR access
  * PVR and timebase access
  * SLB invalidate/insert primitives
* `src/system/kernel/platform/apple_g5/`
  * initial platform target
  * PPC970/970FX/970MP identification
* `src/system/boot/platform/openfirmware/arch/ppc64/`
  * PPC64 Open Firmware target
  * 64-bit kernel handoff stack frame
  * CPU/device-tree discovery

## Remaining kernel stages

1. PPC970 exception vectors and complete `iframe` save/restore.
2. SLB bootstrap and kernel/user segment allocation.
3. 64-bit hashed page-table implementation and VM translation map.
4. 64-bit context switch and syscall entry/return.
5. PPC970 decrementer/timebase interrupt path.
6. MPIC/U3 interrupt routing and IPI support.
7. U3/U3H host bridge and K2 device-tree resource discovery.
8. Apple IOMMU/DART support for PCI DMA.
9. PCI/PCI-X/PCIe host bridge support.
10. SATA, USB OHCI/EHCI, Ethernet and NVRAM.
11. Open Firmware framebuffer handoff, then G5 Radeon/NVIDIA accelerants.
12. SMP CPU bring-up and cache-coherent TLB/SLB synchronization.

## Validation policy

The branch must not be described as bootable until the PPC64 kernel has been cross-built and the exception/MMU path has been tested on at least one real G5 or an accurate PPC970 emulator. The current source stage is intentionally marked as work in progress.

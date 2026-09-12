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

* Dedicated PPC64 kernel/boot architecture and ELF64 linker target.
* `powerpc64-unknown-haiku` target-triplet plumbing is already present.
* PPC64 Open Firmware boot target discovers 64-bit memory cells and CPU/timebase data.
* Bootloader reserves and clears a power-of-two Book III-S HPT and passes it through `kernel_args.arch_args.page_table`.
* PPC64 kernel now programs SDR1 from the reserved HPT and installs the initial kernel SLB entry.
* Native 4 KiB HPTE insertion/removal supports primary and secondary hash groups, supervisor protection and normal/device WIMG modes.
* Exception vectors now build the 64-bit `iframe` layout used by the C exception path, including SRR0/SRR1, DAR/DSISR and all GPRs.
* PPC64 ELF glue (`crti.S`, `crtn.S`) is present for the packaging architecture.
* PPC970/970FX/970MP and U3/U3H identification is wired into the kernel platform layer.
* Decrementer/timebase support and 128-byte cache synchronization primitives are present.

## Remaining completion stages

1. Add the real U3/U3H MPIC interrupt controller and IPI routing.
2. Add per-address-space VSID/SLB allocation and user-mode entry/return.
3. Complete VM translation-map semantics including accessed/modified tracking and HPT eviction.
4. Add DART/IOMMU and Apple U3/K2 PCI host bridge support.
5. Add SMP secondary-CPU startup and per-CPU interrupt/MMU state.
6. Add K2 SATA, OHCI/EHCI USB, Ethernet, NVRAM/RTC and other G5 platform devices.
7. Add Open Firmware framebuffer handoff and graphics support.
8. Build with a real `powerpc64-unknown-haiku` cross-toolchain and resolve all compile/link errors.
9. Validate on QEMU's PowerMac G5 model where usable, then on physical PPC970/970FX/970MP hardware.
10. Only after those tests pass, mark the port bootable and promote the remaining device support to production quality.

## Validation policy

The branch must not be described as bootable until the PPC64 kernel has been cross-built and the exception/MMU path has been tested on at least one real G5 or an accurate PPC970 emulator. Current source is substantially further along, but the hardware-facing interrupt, SMP, PCI/DART and device layers remain unfinished.

# Native AMDGPU driver for Haiku

This directory is a new driver, separate from `radeon` and `radeon_hd`.
The existing drivers remain unchanged and continue to own their current
hardware support.

## Current status: foundation only

The first implementation stage adds a build target and a conservative PCI
enumeration probe for AMD display-class functions (PCI vendor 0x1002).

**It does not bind to a device or perform hardware initialization.** It does
not map BARs, alter PCI command bits, install interrupts, load firmware,
allocate VRAM/GTT, submit commands, or publish a graphics device node. This
is intentional: publishing a device before safe resource ownership and full
teardown exist could interfere with the existing Radeon drivers or leave
hardware in an unsafe state.

## Architecture

The port is organized around AMD IP blocks, not Linux kernel abstractions.
Generation-specific SoC descriptions select versioned IP implementations.
The native Haiku layer will own PCI resources, MMIO mappings, DMA constraints,
interrupts, memory allocation, synchronization, device lifetime, and the
accelerant-facing ABI.

Planned subsystems:

- PCI and ASIC identification: vendor/device/revision and subsystem IDs;
- SoC/IP discovery: common, GMC/UMC, IH, PSP, SMU, GC, SDMA, DCN, VCN;
- firmware: bounded parsing, signature policy, version compatibility;
- memory: VRAM, GTT, GART, GPUVM and buffer objects;
- execution: rings, indirect buffers, fences, scheduling and reset;
- display: DCN/DCE, connectors, link training, modes and scanout;
- userspace: Haiku accelerant and Mesa winsys integration.

Linux DRM, TTM, dma_fence, workqueues, IRQ and ioctl implementations are
reference material only; they are not directly portable kernel dependencies.

## Safety invariants

1. Do not write a register until the matching ASIC/IP revision is identified.
2. Validate every BAR index, resource length, offset and alignment before MMIO.
3. Never treat CPU virtual, CPU physical and device DMA addresses as equivalent.
4. Check all size arithmetic for overflow before allocation or mapping.
5. Validate firmware headers, lengths, versions and trust requirements before
   copying or submitting firmware.
6. Do not enable bus mastering until DMA mappings and teardown are correct.
7. Install interrupts only after the handler state is initialized; mask and
   synchronize them before freeing state.
8. Do not submit untrusted command buffers without validating packet streams,
   address ranges and engine permissions.
9. On partial initialization failure, unwind completed stages in reverse order.
10. Keep unsupported IP blocks disabled; never guess register layouts.

## Generation coverage

“All AMDGPU generations” is a project goal, not a claim that one code path or
one PCI-ID table can support every ASIC. Support must be staged by IP revision
and SoC family. Legacy pre-AMDGPU Radeon generations remain the responsibility
of the existing Haiku Radeon drivers unless a deliberate migration is designed.

## Development sequence

1. Buildable, non-binding PCI probe (this commit).
2. Device records, resource validation and safe MMIO mapping.
3. Firmware parser and ASIC/IP discovery.
4. Display-only modesetting and framebuffer scanout.
5. VRAM/GTT/GART and GPUVM.
6. Interrupts, rings, fences, reset and SDMA.
7. GC command submission and Mesa OpenGL integration.
8. DCN/display hardening, video IP, power management and broader generations.
9. Automated tests, hardware stress, suspend/resume and failure injection.

Each stage must build and pass its tests before the next stage is enabled.

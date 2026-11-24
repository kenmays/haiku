# Native AMDGPU driver for Haiku

This directory is a new driver, separate from `radeon` and `radeon_hd`.
The existing drivers remain unchanged.

## Current status: PCI discovery foundation

The driver enumerates AMD (vendor 0x1002) display-class PCI functions and
retains a bounded set of endpoint identity records. A deliberately partial,
curated PCI-ID table labels selected Tahiti, Bonaire, Hawaii, Polaris 10,
Vega 10/20, Navi 10/21 and Navi 31 devices. Unknown IDs remain unknown; the
table is not an enablement whitelist and does not imply support. For type-0 endpoints it
records BAR size/type metadata, including 64-bit BAR slot pairing and
prefetchability.

**This is not yet a functioning graphics driver.** It does not reserve devices,
map BARs, alter PCI command bits, install interrupts, load firmware, allocate
VRAM/GTT, submit commands, or publish a graphics device node. The PCI scan is
bounded to 256 functions and retained records are bounded to 16 devices.
Enumeration beyond those bounds is not claimed as supported.

## Architecture

The implementation is organized around AMD IP blocks, not Linux kernel
abstractions. Generation-specific SoC descriptions will select versioned IP
implementations. The native Haiku layer owns PCI resources, MMIO mappings,
DMA constraints, interrupts, memory allocation, synchronization, device
lifetime, and the accelerant-facing ABI.

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
7. Install interrupts only after handler state is initialized; mask and
   synchronize them before freeing state.
8. Do not submit untrusted command buffers without validating packet streams,
   address ranges and engine permissions.
9. On partial initialization failure, unwind completed stages in reverse order.
10. Keep unsupported IP blocks disabled; never guess register layouts.

## Generation coverage

“All AMDGPU generations” is a project goal, not a claim that one code path or
one PCI-ID table can support every ASIC. Support must be staged by IP revision
and SoC family. Legacy pre-GCN Radeon generations remain the responsibility
of existing Haiku Radeon drivers unless a deliberate migration is designed.

## Development sequence

1. Buildable, non-binding PCI discovery (current stage).
2. PCI resource reservation, mapping and lifetime management.
3. Expand and verify ASIC/IP discovery tables, including revision and subsystem quirks.
4. Firmware parser and secure firmware loading.
5. Display-only modesetting and framebuffer scanout.
6. VRAM/GTT/GART and GPUVM.
7. Interrupts, rings, fences, reset and SDMA.
8. GC command submission and Mesa OpenGL integration.
9. DCN/display hardening, video IP, power management and broader generations.
10. Automated tests, hardware stress, suspend/resume and failure injection.

Each stage must build and pass tests before hardware functionality is enabled.

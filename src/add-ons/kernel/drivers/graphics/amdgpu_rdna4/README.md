# AMD RDNA4 / GFX12 Haiku driver

Experimental native Haiku graphics-driver bring-up for AMD RDNA4.

Supported PCI IDs currently tracked by this driver:

- 0x7550, 0x7551, 0x7580, 0x7581, 0x75a1, 0x75b0 — GFX1201 family
- 0x7590, 0x7591 — GFX1200 family

The driver is split into hardware discovery, memory/VM, GFX, SDMA, display,
IRQ and accelerant layers. The current safe mode is discovery plus an
accelerant ABI handoff: the driver identifies the GPU, records its PCI BAR
layout, creates per-device shared state, and exposes only functionality that
has a real implementation. It does not yet write undocumented GFX12/DCN4
registers.

## Status

- PCI discovery: implemented
- per-device shared state: implemented
- GFX1200/GFX1201 identification: implemented
- BAR/MMIO address discovery: implemented
- multi-GPU device enumeration: implemented
- accelerant device information: implemented
- accelerant cloning: implemented
- boot-framebuffer mode handoff: ABI path implemented; physical framebuffer
  discovery/mapping still requires the display/firmware layer
- standard mode-query ABI: implemented
- preferred-mode ABI: implemented
- framebuffer-config ABI: implemented conservatively
- pixel-clock query ABI: implemented conservatively
- GFX12 VM geometry and PTE/PDE encoding: implemented as architecture helpers
- software GPUVA mapping validation: implemented
- EDID/DDC/AUX: pending DCN4 connector implementation
- DPMS hardware programming: pending DCN4 implementation
- display mode programming: pending DCN4 implementation
- GFX12 command submission: pending exact firmware/register implementation
- GPUVM hardware page-table allocation/TLB invalidation: pending
- SDMA rings: pending
- interrupts/fences/vblank: pending
- cursor/overlay/MST: pending
- VCN: pending
- SMU/power management: pending
- Mesa/radeonsi/RADV winsys: separate integration layer

The VM, GFX, SDMA and display classes currently provide software state and
validation boundaries rather than pretending to perform hardware operations.
The VM layer now records the GFX12 four-level/4 KiB/48-bit address geometry
and architectural PTE/PDE field encoding without enabling unverified MMIO.
Command submission must not be enabled on real hardware until the exact GFX12
register definitions, firmware images and reset sequence for the target ASIC
revision are integrated and validated.

## Reference architecture

Haiku accelerant -> kernel device ioctl -> RDNA4 device -> MMIO/VRAM/VM/GFX/
SDMA/DCN/VCN.

The implementation follows Haiku's existing graphics-driver conventions rather
than attempting to reuse Linux DRM internals directly. Linux amdgpu sources are
used as hardware-programming references only; they are not copied as a Haiku
userspace ABI.

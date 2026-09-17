# AMD RDNA4 / GFX12 Haiku driver

Experimental native Haiku graphics-driver bring-up for AMD RDNA4.

Supported PCI IDs currently tracked by this driver:

- 0x7550, 0x7551, 0x7580, 0x7581, 0x75a1, 0x75b0 — GFX1201 family
- 0x7590, 0x7591 — GFX1200 family

The driver is split into hardware discovery, memory/VM, GFX, SDMA, display,
IRQ and accelerant layers. The safe mode is discovery plus an accelerant ABI
handoff: the driver identifies the GPU, records its PCI BAR layout, creates
per-device shared state, and exposes only functionality with an actual
implementation. It does not issue undocumented GFX12/DCN4 register writes.

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
- software ring storage/reservation and monotonic fence staging: implemented
- GFX12 firmware basenames for PFP/ME/MEC/RLC/TOC: implemented
- EDID/DDC/AUX: pending DCN4 connector implementation
- DPMS hardware programming: pending DCN4 implementation
- display mode programming: pending DCN4 implementation
- GFX12 command submission to hardware: pending exact firmware/register implementation
- GPUVM hardware page-table allocation/TLB invalidation: pending
- SDMA hardware rings: pending
- interrupts/hardware fences/vblank: pending
- cursor/overlay/MST: pending
- VCN: pending
- SMU/power management: pending
- Mesa/radeonsi/RADV winsys: separate integration layer

The VM, GFX, SDMA and display classes provide software state and validation
boundaries rather than pretending to perform hardware operations. The ring
layer supplies bounded staging storage and monotonic fence sequences that can
be connected to a verified hardware ring/interrupt path.

The firmware layer exposes the GFX12 firmware basenames used by the hardware
backend: `amdgpu/gfx1200_{pfp,me,mec,rlc,toc}.bin` and corresponding `gfx1201`
names. DMCUB is intentionally not guessed from the GFX version because its
filename is tied to the DCN revision.

## Reference architecture

Haiku accelerant -> kernel device ioctl -> RDNA4 device -> MMIO/VRAM/VM/GFX/
SDMA/DCN/VCN.

The implementation follows Haiku's existing graphics-driver conventions rather
than attempting to reuse Linux DRM internals directly. Linux amdgpu sources are
used as hardware-programming references only; they are not copied as a Haiku
userspace ABI.

## Hardware-completion gate

A production hardware path still requires all of the following on actual
GFX1200/GFX1201 boards: verified firmware loading and header parsing, ASIC-specific
reset/clock initialization, GPUVM table allocation and TLB invalidation, GFX/SDMA
ring setup and doorbells, interrupt/fence handling, DCN4 connector/AUX/EDID and
modeset programming, cursor/overlay support, VCN integration, power management,
and Mesa winsys integration. These are deliberately not represented as complete
until they can be built and exercised against corresponding hardware.

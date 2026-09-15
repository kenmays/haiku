# AMD RDNA4 / GFX12 Haiku driver

Experimental native Haiku graphics driver bring-up for AMD RDNA4.

Supported PCI IDs currently tracked by this driver:

- 0x7550, 0x7551, 0x7580, 0x7581, 0x75a1, 0x75b0 — GFX1201/Navi 48 family
- 0x7590, 0x7591 — GFX1200/GFX1201 Navi 44 family

The driver is deliberately split into hardware discovery, memory/VM, GFX, SDMA, display, IRQ and accelerant layers. The initial safe mode is firmware/boot-framebuffer handoff: the driver identifies the GPU, maps its PCI BARs, creates shared state, and exposes the Haiku accelerant ABI without programming undocumented GFX12 registers.

## Status

- PCI discovery: implemented
- BAR/MMIO discovery: implemented
- per-device shared state: implemented
- GFX1200/GFX1201 identification: implemented
- boot framebuffer handoff: framework implemented
- accelerant device information: implemented
- display mode programming: hardware-specific implementation pending
- GFX12 command submission: hardware-specific implementation pending
- GPUVM: hardware-specific implementation pending
- SDMA: hardware-specific implementation pending
- DCN4: hardware-specific implementation pending
- VCN: hardware-specific implementation pending
- Mesa/radeonsi/RADV winsys: separate follow-up layer

Do not enable command submission on real hardware until the matching GFX12 register and firmware tables are imported from the exact upstream revision being targeted.

## Reference architecture

Haiku accelerant -> kernel device ioctl -> RDNA4 device -> MMIO/VRAM/VM/GFX/SDMA/DCN/VCN.

The implementation intentionally follows Haiku's existing graphics-driver conventions used by `radeon_hd` rather than attempting to reuse Linux DRM internals directly.

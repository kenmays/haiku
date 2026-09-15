# RDNA4 accelerant

This directory is the Haiku-side boundary for the native AMD RDNA4 driver.

The first milestone intentionally exposes only safe identification and shared-state plumbing. It does not issue undocumented GFX12 or DCN4 register writes. Use the existing `radeon_hd` accelerant as the Haiku ABI reference: its implementation obtains private shared data from the kernel driver and supplies the standard accelerant hooks. The RDNA4 accelerant should follow the same pattern while keeping GFX12-specific implementation in the kernel driver.

Next hardware milestones:

1. Map the boot framebuffer and publish the initial display mode.
2. Implement DCN4 mode validation/programming.
3. Add vblank and hotplug events.
4. Add GPUVM and BO ioctls.
5. Add SDMA/GFX12 queues and fences.
6. Add overlay/cursor planes.
7. Add Mesa winsys.

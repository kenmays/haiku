# Native RDNA4 firmware contract

The RDNA4 driver does not embed proprietary firmware binaries. The runtime
firmware package must provide the AMD-licensed blobs under the Haiku firmware
directory.

For GFX12.0 the kernel requires:

* gc_12_0_0_pfp.bin
* gc_12_0_0_me.bin
* gc_12_0_0_mec.bin
* gc_12_0_0_rlc.bin
* gc_12_0_0_toc.bin

For GFX12.0.1:

* gc_12_0_1_pfp.bin
* gc_12_0_1_me.bin
* gc_12_0_1_mec.bin
* gc_12_0_1_rlc.bin
* gc_12_0_1_rlc_kicker.bin
* gc_12_0_1_toc.bin

RDNA4 systems also require the matching PSP14, SMU14, SDMA7 and MES firmware.
The private ioctl RDNA4_GET_FIRMWARE_INFO exposes the exact expected names to
the accelerant/Mesa integration layer.

The current Linux GFX12 implementation confirms the dedicated GC12 firmware
sets and the GFX12 queue defaults; the native Haiku driver intentionally keeps
the same firmware contract without importing Linux DRM ABI.

Until firmware loading and PSP authentication are implemented, the driver
reports GFX/MES/SDMA as DISCOVERED rather than RUNNING and does not advertise
3D submission. This prevents a partially initialized GPU from being exposed
as a working accelerated device.

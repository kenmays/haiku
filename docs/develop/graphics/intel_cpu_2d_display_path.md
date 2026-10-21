# Intel CPU-only 2D display path for Haiku

## Scope and non-goals

This document defines a native Haiku implementation path for software-rendered 2D graphics on Intel integrated and discrete graphics devices. It is deliberately separate from the Linux `xe` driver: Linux kernel driver code cannot be transplanted into Haiku because its DRM, GEM, TTM, dma-fence, runtime-PM, and display-framework interfaces do not exist as drop-in Haiku APIs.

“CPU-only 2D” means that rasterization, fills, copies, alpha blending, and cursor composition run on the CPU. It does **not** remove the need for a chipset-specific display initialization and modesetting path to light the panel or connector. Nor does recognizing a PCI ID prove that the device can be safely initialized by an older generation's register programming.

## Recommended Haiku architecture

1. **Kernel driver (device ownership and safe hardware access)**
   - Use Haiku's `device_manager`, PCI module, `open/close`, `ioctl`, shared areas, and kernel locks.
   - Match Intel vendor ID `0x8086` plus a maintained device table. Keep identification separate from a per-generation capability table.
   - Own PCI enablement, BAR mapping, interrupt/vblank resources where implemented, and teardown. Validate all BAR sizes and MMIO offsets before access.
   - Never expose arbitrary MMIO or physical addresses to userland. Publish only validated framebuffer metadata and the minimum display-control operations required by the accelerant.
   - Fail closed for IDs whose display generation, power wells, stolen-memory layout, or register programming is not implemented.

2. **Accelerant (native app_server contract)**
   - Implement Haiku's accelerant hooks for device information, mode enumeration, `get_frame_buffer_config()`, `set_display_mode()`, cloning, and DPMS as supported by the target Haiku headers.
   - Preserve a stable shared-info ABI between the kernel driver and accelerant; version it and validate the version/size on every ioctl.
   - Reuse the existing `intel_extreme` accelerant and modesetting code where it is correct for a given generation rather than duplicating register programming in a second driver.
   - Expose only modes whose timing, pixel clock, link rate, lane count, memory layout, and connector constraints have been validated.

3. **CPU raster backend**
   - Use Haiku's native software drawing path / app_server interfaces. The CPU backend must not submit render commands to the Intel render engine or depend on firmware/GPU scheduling.
   - Support a conservative initial format set (XRGB32 and RGB16 only when the framebuffer configuration advertises them); reject unsupported formats rather than guessing.
   - Use checked arithmetic for pitch, height, bytes-per-pixel, offsets, and allocation sizes. Treat framebuffer pitch as authoritative; never assume `pitch == width * bytesPerPixel`.
   - Clip all rectangles to framebuffer bounds before pointer arithmetic. Use overlap-safe row ordering for copies and `memmove` for overlapping rows.
   - Implement solid fill, blit/copy, and software cursor composition first. Add alpha blending only after pixel-format and premultiplication conventions are explicit.
   - Use write-combining only when Haiku's supported mapping/cache APIs permit it. Avoid speculative cache-policy changes to an existing framebuffer mapping.

4. **Display modesetting and hotplug**
   - Keep connector discovery, EDID/DDC, panel power sequencing, PLL calculation, pipe/transcoder setup, and link training in the generation-specific display layer.
   - For failure during a mode transition, restore the last known-good mode or return an error; do not report success until the hardware state and framebuffer configuration agree.
   - Use Haiku semaphores/interrupt handlers for vblank only when the hardware interrupt is correctly acknowledged and teardown is synchronized. A CPU renderer does not require GPU interrupts to draw.

## Linux `xe` PCI-ID import policy

Linux `xe` PCI tables are useful for identifying devices, but they are not a safe list of devices that Haiku can initialize. Maintain three separate states:

- **Recognized:** vendor/device ID is known and reported for diagnostics.
- **Display-supported:** the ID maps to a display IP generation with implemented power, pipe, PLL, connector, and framebuffer code.
- **Validated:** tested on real hardware (or a documented emulator/test fixture) for boot, mode set, mode restore, suspend/resume where supported, and clean unload/reload.

Do not map every `xe` ID to the nearest old Haiku `DeviceType` just to make it bind. In particular, Xe-era discrete devices and newer integrated generations need substantially different display, memory, and power-management handling from the legacy `intel_extreme` path. Recognition can be broad; initialization must remain conservative.

## Incremental implementation and acceptance criteria

- [ ] Audit the current `intel_extreme` driver's BAR mapping, framebuffer pitch/format, area lifetime, locking, and error-unwind paths.
- [ ] Add a versioned shared-info ABI and strict validation for all ioctl structures.
- [ ] Extract a small, hardware-independent CPU raster library with unit tests for clipping, overflow, padded pitch, RGB16/XRGB32 fills, overlapping copies, and cursor bounds.
- [ ] Connect the raster library to the native accelerant/app_server software path without changing modesetting semantics.
- [ ] Build a generation capability matrix from the Linux `xe`/i915 PCI tables and the applicable Intel display programming references; do not infer display compatibility from the PCI ID alone.
- [ ] Enable each generation only after register sequencing, panel timing, EDID, PLL, and mode-switch tests pass.
- [ ] Add a boot-time safe fallback: if hardware modesetting is unsupported, leave the firmware-provided framebuffer untouched where Haiku's boot/display stack permits it, and do not claim the Intel hardware driver is fully operational.
- [ ] Run Haiku builds and hardware tests for every changed generation; attach build/test logs before marking support complete.

## Important limitation

This document is an architecture and acceptance checklist, not a claim that a complete display driver has been implemented. A truly functional driver requires code changes and testing against Haiku's current graphics APIs and Intel's generation-specific display programming documentation. A CPU-only renderer alone cannot provide universal display output for all Linux `xe` PCI IDs.

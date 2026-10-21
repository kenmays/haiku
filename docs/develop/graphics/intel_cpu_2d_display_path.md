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


## Framebuffer, scanout, ring-buffer, and back-buffer rules

### Current implementation boundary

The CPU 2D backend writes synchronously to the currently mapped framebuffer that the display engine scans out. It submits no GPU commands and therefore does not use a render ring or blitter ring. Do not add ring-buffer head/tail programming to this CPU backend as a supposed fix for tearing: a GPU ring is a command queue, not a display back buffer, and it cannot make CPU writes to a live scanout surface atomic.

A separate back buffer is also not equivalent to page flipping. Copying a completed shadow buffer into the currently scanned-out surface can still tear. Tear-free presentation requires a second scanout-capable surface plus a supported, synchronized plane/surface flip (or another documented display synchronization mechanism), and the old surface must not be freed until hardware is finished reading it.

### Allocation and layout invariants

- Treat `frame_buffer_config::bytes_per_row` as the authoritative CPU row stride returned by the accelerant; never assume it equals visible width times bytes per pixel.
- Allocate for `bytes_per_row * virtual_height`, with overflow checks and an allocator-range check against the mapped graphics-memory aperture.
- Keep CPU address, graphics-memory offset, physical/DMA address, stride, pixel format, virtual dimensions, and the hardware plane's surface/offset registers as one consistent layout description.
- Verify the required pitch and surface alignment and the pitch register's units against the PRM for the exact display-engine generation. The current 64-byte pitch rounding and Lake-family pitch-register scaling must not be generalized to every Intel generation without evidence.
- For panning, validate the complete fetched rectangle against the allocation, including start X/Y, visible width/height, bytes per pixel, and stride. Apply each generation's documented address/offset model; do not combine a surface base and start offset twice.
- Only advertise color spaces whose Haiku byte layout and hardware plane format have both been verified on the target generation. Indexed `B_CMAP8`, RGB565, legacy RGB555, and 32-bit xRGB are distinct formats, not interchangeable labels.

### Safe future back-buffer design

A future tear-free path should be implemented separately from the CPU renderer and only for generations with verified display-flip support:

1. Allocate two independently aligned scanout surfaces, both large enough for the full virtual surface and pitch.
2. Keep the currently displayed surface and render target explicit in shared state. Never let CPU hooks infer that the render target is the active scanout buffer.
3. Render into the non-displayed surface; ensure CPU writes are visible to the display engine using the documented memory/cache ordering for that platform.
4. Request a plane/surface flip through the generation-specific documented mechanism, synchronized to vblank or the documented flip event. Do not emulate a flip by merely rewriting a register while scanout is active unless the relevant PRM explicitly permits it.
5. Wait for flip completion (or another documented guarantee that scanout no longer references the old surface) before reusing or freeing the old surface.
6. On failure, preserve the old surface and restore all changed plane/pipe state. Test DPMS, suspend/resume, mode changes, multiple pipes, and unplug/replug with both surfaces allocated.
7. Keep a single-buffer fallback when flip support, event completion, or memory requirements cannot be verified.

The current Haiku accelerant API and this CPU backend do not yet provide a verified, cross-generation page-flip completion path. Until one exists, retain the single scanout buffer and document that tearing is possible rather than claiming tear-free output.

### Ring-buffer boundary

Implement a GPU ring only when implementing a GPU command-submission backend. That work must be based on the exact generation's PRM and should cover ring allocation/alignment, start/control/head/tail registers, wrap and free-space calculations, command encoding, memory visibility, engine reset/hang recovery, and synchronization tokens. A ring buffer does not itself provide a second display surface, and the display plane does not automatically wait for arbitrary rendering unless the required fences or synchronization are programmed.

Do not mix legacy render-ring or blitter-ring programming with newer command streamer or display-flip mechanisms without per-generation documentation. The display engine and the GPU command engine are separate programming domains.

### Primary references

- Intel's official [Graphics for Linux Programmer's Reference Manuals](https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/ice-lake.html) index provides generation-specific display-engine, command, memory-format, and command-stream volumes. Select the manual matching the actual GPU/display generation.
- Intel's [DG1 Display Engine PRM](https://cdrdv2-public.intel.com/682696/intel-gfx-prm-osrc-dg1-vol12-displayengine.pdf) describes plane-flip sequencing and flip completion. It is an example of the required level of generation-specific evidence, not a specification for older Gen2–Gen9 hardware.
- The [Intel Open Source Graphics PRM archive](https://github.com/Igalia/intel-osrc-gfx-prm) organizes older Gen4 through Gen9.5 documentation.
- Haiku's [intel_extreme kernel driver](https://github.com/haiku/haiku/tree/master/src/add-ons/kernel/drivers/graphics/intel_extreme) and accelerant are the implementation reference for Haiku's mapping, allocation, shared-state, and accelerant contracts. Validate behavior against the checked-out Haiku headers and implementation rather than copying Linux DRM interfaces directly.

### Acceptance tests for framebuffer presentation

- Test every supported format with pixel-pattern and stride-padding tests, including non-visible virtual width.
- Exercise source/destination rectangles with negative origins, all four edges, overlap in all directions, and transparent color keys.
- Verify allocation bounds and start offsets using guard regions and integer-overflow tests.
- For any future flip implementation, log old/new surface addresses, requested and completed flip events, vblank sequence, and the point at which the old allocation becomes reusable.
- Record results by exact PCI ID, display generation, connector, mode, refresh rate, pixel format, and test build. A result from one SKU does not prove another SKU or generation.

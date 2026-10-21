# Intel Extreme multihead and multimonitor implementation plan

## Status and scope

The current accelerant is not an independent multihead implementation. It has a single shared `current_mode`, `frame_buffer`, `frame_buffer_offset`, `bytes_per_row`, and `bits_per_pixel`. The current mode setter allocates one surface and applies the same requested mode to every connected port. `set_frame_buffer_base()` programs only the A/B display-plane register banks and deliberately gives both the same surface. This is at best a clone/mirror foundation for supported A/B configurations; it is not independent multi-monitor support and it does not correctly cover pipe C/D.

Do not label all connected ports as independently supported merely because connector detection finds them. Port detection, port-to-pipe assignment, pipe timing programming, plane scanout, per-head mode state, and app_server multihead enumeration are separate requirements.

## Required architecture

### 1. Separate physical outputs, display pipes, planes, and logical heads

- A connector/port is a physical output (DP/eDP/HDMI/DVI/VGA/LVDS).
- A pipe/transcoder produces timings and feeds a compatible output.
- A display plane scans out a surface for a pipe. Plane availability and register layout differ by generation.
- A logical head is the Haiku-visible display configuration associated with one independently timed pipe/surface.
- Multiple connectors may mirror one head only if the hardware routing and all sinks accept the same mode. Independent extended desktop requires separate per-head state.

The existing `Port::SetPipe()` assignment is useful, but it is not sufficient: each active pipe needs its own timing, color mode, plane/surface base, pitch, display start, DPMS state, and mode rollback state.

### 2. Replace singleton framebuffer/mode state

The shared driver ABI must be extended in a versioned manner. Introduce a bounded per-head record with at least:

- stable logical head ID and pipe/transcoder index;
- active/connected output mask or stable connector identifiers;
- requested and programmed `display_mode`;
- color space, bits per pixel, stride, virtual dimensions, and display start;
- front surface base, graphics-memory offset, allocation size, and ownership;
- optional back surface plus flip-pending/completion state only when a verified flip path exists;
- per-head enabled/DPMS and last modeset error state.

Keep shared global fields for device-wide properties, memory allocator state, register mapping, and synchronization. Do not silently reinterpret the existing singleton fields: accelerant and kernel driver must agree on structure layout/version, and clone-area users must reject incompatible versions.

### 3. Allocate scanout memory per independent head

For each independent head, calculate `stride * virtual_height` with overflow checks, validate format/pitch/alignment against that generation's PRM, allocate a distinct surface, and prove the full range lies within the graphics-memory aperture. One shared surface is only valid for clone outputs that intentionally show identical content. Do not allocate a separate framebuffer per connector when two connectors are routed to the same pipe; those are mirror sinks for one head.

Use a small allocation transaction: reserve all new head surfaces before changing hardware; if any reservation fails, release only the new allocations and preserve the existing modes. Do not release old surfaces until every affected pipe has been restored or switched successfully.

### 4. Per-generation plane and pipe programming

Implement a generation-specific scanout backend instead of inferring register offsets from the pipe enum:

- Determine the supported pipe/transcoder/plane topology from the actual device generation and fuse/strap configuration.
- Program only register banks documented for that generation and pipe. Pipe D or a third plane cannot be assumed to follow the A/B register stride.
- Validate surface base alignment, pitch units and limits, pixel-format encoding, tiling/compression state, start offset, and update/latch sequence against the exact Intel PRM.
- Configure timing/PLL/link training for each head's own mode. A shared PLL or link resource may constrain the set of simultaneously valid modes; reject incompatible combinations explicitly.
- Do not enable a pipe until its plane surface, format, stride, timings, and port routing are coherent. Preserve readbacks and timeout/error propagation where the PRM requires waiting.

The existing mode code that writes the same `bytes_per_row` and color mode for both A and B must be replaced with per-head programming before independent modes are enabled. Legacy display-engine families and DDI-era families need distinct implementations.

### 5. Mode-set transaction and rollback

A multihead mode change must be transactional:

1. Validate all requested head modes together, including shared PLL/link constraints and total framebuffer memory.
2. Allocate and initialize replacement surfaces without changing current state.
3. Save the old mode, plane surface, routing, pipe/PLL/link state, and power state for every affected head.
4. Disable or blank only the affected pipes as required by that generation's programming sequence.
5. Program per-head timing, link, plane, pitch, surface address and start offset in the documented order.
6. Verify lock/training/enable completion and then atomically publish the new per-head state.
7. Retain old surfaces until hardware is known to no longer scan them.
8. On any failure, restore every changed head; if restoration fails, keep both old and new allocations alive and leave the affected head safely disabled rather than freeing a possibly scanned surface.

A partial success across two outputs must not be reported as full success. A failed second head must not leave the first head silently running a mode inconsistent with shared state.

### 6. Haiku accelerant/app_server integration

Independent heads require correct Haiku display enumeration and per-head mode handling, not just additional MMIO writes. Inspect the current Haiku accelerant API and app_server multi-monitor expectations for the branch's target revision. Keep the existing single-head hooks working, but add the required per-head hooks/state only where the API supports it. If Haiku's current accelerant ABI represents the device as a single logical head, independent heads may require a separate accelerant instance/head-selection model or broader API work; do not fake it by returning one global mode for every head.

Framebuffer access hooks must operate on the currently selected head's surface and stride. Any direct framebuffer configuration exposed to clients must clearly identify the correct head's mapping. Mode changes and drawing operations must synchronize against the same head state to avoid stale pointers.

### 7. Clone/mirror mode versus extended desktop

Implement these as distinct modes:

- **Clone/mirror:** two or more compatible outputs share one head, mode, scanout surface, and content. Validate mode compatibility across all sinks and program each port's routing to the same supported pipe. No duplicate framebuffer allocation is needed.
- **Extended desktop:** each head has an independent mode and surface. This requires per-head state and independent app_server integration. Never infer extended-desktop support from clone support.

Initially support only the explicitly validated subset (for example, two outputs on documented A/B pipe/plane combinations). Return an error or hide unsupported head combinations rather than writing unverified C/D registers.

## Acceptance matrix

For each supported generation and exact PCI ID, test:

1. One monitor on each supported physical output individually.
2. Two-monitor clone with matching mode, then disconnect/reconnect either sink.
3. Two-monitor extended desktop with different resolutions, refresh rates, and pixel formats.
4. Independent display starts and virtual dimensions.
5. Concurrent drawing to both heads, with no shared stride or surface-pointer contamination.
6. Failure injection during second-head allocation, PLL setup, link training, and plane enable; old modes and allocations must remain safe.
7. DPMS per head, sleep/wake, hotplug, suspend/resume, and repeated mode changes.
8. Memory pressure with both heads enabled and with replacement modes requiring larger surfaces.
9. Back-buffer flips only where generation-specific flip completion has been implemented; never reuse a surface based solely on command-ring completion.
10. Record pipe, plane, port, timings, stride, format, surface offset, and completion/readback for every run.

## Reference sources

- Haiku source in this branch: `accelerant.cpp` (port/pipe discovery and assignment), `Ports.cpp` (connector routing and per-port mode setup), `Pipes.cpp` (pipe register offsets/timing), and `mode.cpp` (currently singleton framebuffer and A/B plane base).
- Intel Graphics Programmer Reference Manuals (PRMs): use the display-engine and command-stream volumes for the exact generation; register-bank topology, plane availability, pitch encoding, and latch sequences must be validated per generation. Do not use a DG1 PRM as a register specification for Gen2–Gen9.
- Haiku accelerant/display API headers and app_server behavior for the exact target revision must be treated as the ABI contract.

## Current honest support statement

Until per-head shared state, per-head surface allocation, generation-specific scanout programming, transactional rollback, and Haiku display enumeration are implemented and tested, report this driver as single-head with limited shared-mode clone behavior where verified. Independent multi-monitor/extended-desktop support is not yet complete.

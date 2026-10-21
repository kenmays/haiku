# CPU-only renderer: implementation status and validation

The `intel_extreme` accelerant now has a first synchronous CPU raster backend in
`cpu_renderer.cpp`. The build includes that file, and the public accelerant
hook dispatcher routes engine synchronization and the four 2D operations to
this backend instead of submitting those operations to the Intel ring buffer.

## Implemented

- Solid rectangle fill and invert, with inclusive-coordinate clipping.
- Screen-to-screen copies with clipping, padded-stride support, and overlap-safe
  row order plus `memmove` for horizontal overlap.
- Scanout mapping derived from the current shared framebuffer offset, pitch,
  virtual dimensions, and color space; invalid formats, dimensions, and
  out-of-range framebuffer spans are rejected before drawing.
- RGB32, RGB16, RGB15, and indexed 8-bit stores; the indexed path writes the
  supplied low-byte index.
- Fill-span hook enabled; CPU drawing is synchronous and does not submit render
  commands or depend on ring-buffer progress.

## Important limitations

This is an initial CPU renderer, not a replacement for Linux `xe` as a whole.
It reuses the existing Haiku `intel_extreme` kernel driver and modesetting
implementation. It does not add Xe2/Xe3 hardware initialization, firmware/GuC,
GPU memory management, interrupts, power management, new connector support, or
universal PCI-ID support. A Linux PCI ID being recognized is not evidence that
the existing Haiku kernel driver can safely initialize that GPU.

The indexed-color path assumes the hook's color argument is already an 8-bit
palette index. RGB formats assume Haiku's packed RGB color argument convention;
these details need verification against app_server callers and hardware
readback. The renderer also has not yet been built or tested on hardware in
this environment.

## Required validation before claiming production readiness

1. Build `intel_extreme.accelerant` in a Haiku build tree and fix any compiler
   diagnostics.
2. Add host/unit tests for clipping, invalid pitch/offsets, padded stride,
   each pixel format, fill spans, and overlapping copies in all directions.
3. Verify color packing and indexed palette behavior against Haiku's
   accelerant contract.
4. Stress engine locking and concurrent mode changes; ensure mode changes
   cannot expose stale framebuffer metadata to a drawing operation.
5. Test boot, mode setting, DPMS, hotplug, and rendering on real devices in each
   claimed hardware generation before enabling additional PCI IDs.


## DPMS safety improvement

LVDS panel-status polling in `dpms.cpp` now has a bounded 500 ms timeout instead of an unbounded loop. This prevents a missing/stuck status bit from trapping the accelerant forever. A timeout is logged, but the existing DPMS routine does not yet propagate the failure because its helper is `void`; digital-port DPMS and newer display-engine power sequencing remain unresolved and unvalidated.

## Additional operation

- Transparent screen-to-screen blit now copies only pixels that differ from the encoded transparent key, with clipped extents and overlap-aware traversal. Pixel-key encoding still needs verification against app_server color-space semantics.


## EDID/DDC/DVI and direct framebuffer access

- The existing `Port::GetEDID()` path reads the EDID 1.x base structure using Haiku's `ddc2_read_edid1()` helper. The port layer configures GPIO/I2C DDC for legacy ports and DP AUX-I2C for DisplayPort/DDI, with a per-port fallback hook.
- Legacy DVI detection remains a conservative fallback after DisplayPort/HDMI discovery and validates presence by successfully reading EDID. DVI is not inferred solely from a PCI ID or connector register.
- `B_GET_EDID_INFO` now rejects null output/version pointers and undersized output buffers. The hook still exports Haiku's `edid1_info` structure; this is not a raw 128-byte-block/extension API.
- **E-EDID extension limitation:** this path currently calls `ddc2_read_edid1()` and exposes only Haiku's parsed EDID 1.x structure. It does not preserve or expose arbitrary raw extension blocks (for example CTA-861 data blocks) to clients. Full extension-block transport/validation needs a suitable Haiku common DDC API or a separately designed raw-EDID interface; do not claim complete E-EDID extension support.
- `B_PARALLEL_ACCESS` is now set on enumerated and proposed modes because the framebuffer is CPU mapped and the accelerant's 2D operations execute synchronously on the CPU. This advertises concurrent framebuffer access; it does not imply hardware validation of every PCI ID or guarantee that an untested chipset's display initialization is safe.

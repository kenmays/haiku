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

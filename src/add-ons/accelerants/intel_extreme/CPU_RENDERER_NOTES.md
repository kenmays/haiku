# CPU-only renderer integration notes

The current `intel_extreme` accelerant advertises these native Haiku hooks in `hooks.cpp`:

- `B_SCREEN_TO_SCREEN_BLIT`
- `B_FILL_RECTANGLE`
- `B_INVERT_RECTANGLE`
- engine acquire/release and synchronization hooks

The implementations in `engine.cpp` are coupled to the Intel ring buffer and GPU command submission. A CPU-only backend must not merely return success from those functions: it must synchronously update the active framebuffer, respect the active display mode's bytes-per-row and color space, and serialize access with the same shared engine lock used by app_server.

## Safe integration sequence

1. Add a separately testable CPU raster module with no MMIO or GPU dependencies.
2. Derive framebuffer base, size, pitch, width, height, and color space from the active framebuffer configuration; validate each before drawing.
3. Implement clipped solid fills, invert, and overlap-safe screen-to-screen copies for the explicitly supported pixel formats.
4. Add a runtime backend capability flag. In CPU mode, dispatch the 2D hooks to the raster module and avoid ring-buffer commands; preserve GPU mode as a separate opt-in path for hardware that has validated acceleration.
5. Ensure acquire/release, sync-token behavior, mode changes, and framebuffer remapping cannot race with raster operations. Do not retain a framebuffer pointer across a mode change.
6. Keep unsupported or unvalidated PCI IDs from entering hardware modesetting merely because they are present in a Linux PCI table.

## Acceptance checks

- Rectangle clipping with negative origins and edges beyond the display.
- Integer-overflow rejection for pitch, height, offsets, and rectangle extents.
- Padded pitch and every advertised pixel format.
- Overlapping copies in both directions and same-row overlap.
- Concurrent app_server drawing versus mode change.
- No writes after framebuffer remapping or accelerant teardown.
- Boot and mode-set tests on physical hardware for each claimed device family.

This is an integration plan; it does not claim that the CPU backend or universal PCI-ID support has already been implemented or tested.

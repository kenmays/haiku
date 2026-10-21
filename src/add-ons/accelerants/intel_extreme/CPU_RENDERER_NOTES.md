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

## Back-buffer safety contract (not yet implemented)

The current renderer writes directly to the active scanout framebuffer. It has no independent back buffer and does not promise tear-free presentation. Keep this distinction explicit in code and user-facing support claims.

Do not add a shadow buffer and then silently advertise it as a back buffer: applications can access the framebuffer directly through `frame_buffer_config`, so rendering hooks alone cannot keep a shadow surface coherent with direct client writes. A shadow buffer would require a defined copy/present contract covering every writer, including app_server and direct framebuffer access.

A future hardware double-buffer implementation must use two independently allocated scanout-capable surfaces and a per-generation flip mechanism verified against the matching Intel PRM. Its state machine must distinguish at least:

- **Front**: the surface currently owned by scanout; never write to or free it while scanout can fetch it.
- **Back**: the non-displayed surface that may be rendered into.
- **Flip pending**: a flip was requested but completion has not been observed; neither reuse nor free the old front surface.
- **Recovery**: the flip timed out or mode/DPMS/hotplug changed; keep both allocations alive until the hardware state is known, then restore a known-good front or disable the experimental path.

Required invariants:

1. Allocate and range-check both surfaces before changing live display state. Validate overflow, pitch, surface alignment, and full virtual-height size independently for each surface.
2. Publish the back-buffer metadata only after allocation succeeds; do not overload `shared_info.frame_buffer` to mean both the active scanout surface and render target.
3. Before a flip request, ensure all CPU/GPU writes to the target are visible to scanout using the documented memory-ordering/cache-flush method for that platform. A CPU lock alone is not a device cache-coherency guarantee.
4. Use only the documented per-generation flip command/register sequence and synchronization signal. A ring command being consumed, an engine sync token completing, or a generic flush is not proof that the display flip completed.
5. After the flip completion event, atomically update the front/back roles under the appropriate shared-state lock. Reuse the old front only after hardware completion is confirmed.
6. On timeout, mode change, DPMS-off, suspend/resume, or unplug, stop issuing new flips. Keep both buffers allocated until scanout is disabled or its current surface is known; never free a possibly active surface on a timeout alone.
7. Fall back to the existing single-buffer path when generation, plane, flip completion, memory coherency, or synchronization semantics are not fully supported.

Intel PRMs explicitly distinguish a flip request from flip completion. For example, the Ironlake-era `MI_DISPLAY_FLIP` documentation notes that command-stream progress / `MI_FLUSH` does not by itself guarantee that a pending display flip has completed; the documented flip-pending or wait-for-event synchronization is required. This is not a universal recipe: command support, packet fields, cache flush requirements, and completion mechanisms differ by generation. See the [Ironlake PRM excerpt](https://kiwitree.net/~lina/intel-gfx-docs/prm/ilk/ilk_ihd_os_vol1_part3r2.pdf) and the [Intel Open Source Graphics PRM archive](https://github.com/Igalia/intel-osrc-gfx-prm).

Until the state machine above exists and has hardware evidence, the robust behavior is to retain one scanout buffer, avoid speculative flips, and state plainly that tearing may occur.

## Multihead / multimonitor status

The current modeset path still has one shared mode and framebuffer. It programs A/B plane scanout banks with one common surface; it is not independent extended-desktop support. The modeset path now rejects a connected port assigned to pipe C or D with `B_NOT_SUPPORTED` rather than returning success after failing to program that pipe's scanout surface. This is a safety guard, not C/D support.

See `docs/develop/graphics/intel_multihead_multimonitor_design.md` for the per-head state model, per-generation scanout requirements, transactional modeset/rollback design, clone versus extended-desktop distinction, and hardware acceptance matrix. Full independent multi-monitor support requires a versioned shared-state ABI, per-head mode/surface allocation, validated per-pipe plane programming, and Haiku display enumeration integration. Do not claim that feature until those pieces are implemented and tested.

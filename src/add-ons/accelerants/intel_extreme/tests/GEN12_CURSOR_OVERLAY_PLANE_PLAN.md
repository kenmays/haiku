# Gen12 cursor and overlay display-plane plan

**Status:** design and source-audit plan; not an implementation or hardware-validation claim.  
**Target:** Rocket Lake / Gen12, with CPU-only 2D rendering preserved.

## Current safety state

- Hardware drawing hooks and engine/synchronization hooks remain disabled. Display-plane features must not introduce a general-purpose GPU command-submission dependency.
- The legacy hardware cursor hooks are now withheld for Generation 12 and newer. The existing cursor implementation uses `INTEL_CURSOR_*` register definitions and a two-plane monochrome payload, but the source audit has not established that those registers, formats, or pipe routing are correct for Rocket Lake.
- The legacy overlay API remains unavailable on Generation 6 and newer. Its implementation relies on the legacy overlay engine and primary-ring command path, hard-codes pipe 0, and does not provide a safe Gen12 implementation template.
- Cursor shape input rejects zero dimensions, dimensions above 64x64, null masks, and a missing cursor-memory allocation. This is basic input hardening only; it does not prove register or allocation correctness.
- The Gen12 PLL code now uses bounded polling and returns timeout errors; the DDI mode-set caller propagates PLL programming failures. DPLL selection and port-clock masks remain unresolved blockers.

## Cursor register audit before re-enabling Gen12 hooks

1. Trace each `INTEL_CURSOR_CONTROL`, `INTEL_CURSOR_POSITION`, `INTEL_CURSOR_SIZE`, `INTEL_CURSOR_PALETTE`, and `INTEL_CURSOR_BASE` definition to the register header and its documented generation scope.
2. Compare the exact register offsets, bit definitions, address alignment, pixel formats, and position encoding with Rocket Lake display-engine documentation and the corresponding Linux i915 display code.
3. Establish the active pipe/transcoder association and whether the cursor plane is selected by pipe-specific registers on this generation. Do not infer it from the allocated cursor page.
4. Confirm the cursor allocation size, physical address width, required alignment, cache/visibility rules, and whether the memory is accessible to the display engine.
5. Keep Gen12 cursor hooks disabled until the source mapping is demonstrated and the tests below pass on actual hardware.

## Modern Gen12 overlay / plane design

Do not revive the legacy overlay engine. Build a separate modern plane path only after completing a per-pipe inventory against the Rocket Lake display register documentation and upstream Linux i915 implementation.

Inventory and document:

- Primary, sprite/overlay, and cursor planes available per pipe/transcoder, including plane-to-pipe routing and restrictions.
- Supported pixel formats and modifier/layout constraints; begin with a narrow format set rather than claiming legacy RGB/YUV formats automatically work.
- Minimum/maximum source and destination dimensions, stride and base-address alignment, source-coordinate granularity, scaling ratios, clipping rules, and color-conversion capabilities.
- Required register programming order, shadow/latch behavior, vblank synchronization, commit completion/error reporting, and plane-disable sequence.
- Buffer lifetime and ownership: reject undersized allocations, integer overflow, invalid source rectangles, out-of-range destination rectangles, unsupported scaling, and invalid stride/alignment before any MMIO.
- Interactions with modeset, DPMS, hotplug, pipe reassignment, framebuffer resize, and client release. Every failure path must disable or leave the plane in a documented safe state.
- Whether Haiku's overlay API semantics can be mapped without a GPU command ring. Keep all overlay hooks null until the plane commit path is complete and tested.

The implementation should program display-plane registers directly through a generation-specific, bounded and validated modeset/commit path. Do not enable engine, synchronization, fill, blit, or span hooks as a side effect.

## Required tests

### Cursor
- 64x64, 32x32, 16x16 and 1x1 shapes; reject zero dimensions and dimensions above 64.
- Transparent, black, white, and invert pixels; alternating AND/XOR patterns.
- Repeated shape changes and hotspot changes while visible and hidden.
- Move across all four screen edges, off-screen positions, and signed-coordinate boundaries.
- Hide/show, DPMS off/on, mode changes, hotplug, and multi-display pipe routing.
- Verify no memory overwrite, malformed base address, stale cursor, or display-engine hang.

### Overlay / sprite plane
- Every advertised format, including color conversion and alpha behavior where applicable.
- Source and destination bounds, clipping on each edge, no intersection, and partially off-screen windows.
- Minimum/maximum scaling, unsupported scaling rejection, stride/base alignment, short buffers, and overflow-sized dimensions.
- Repeated buffer flips synchronized to vblank; verify no tearing, stale frames, or use-after-release.
- Disable/release while visible, DPMS cycles, mode changes, hotplug, and competing clients.

### Regression and qualification
1. Run `python3 src/add-ons/accelerants/intel_extreme/tests/validate_rkl_alchemist_battlemage.py` from the Haiku source root.
2. Run the other CPU-only / Gen10-12 fail-closed source validators.
3. Build the affected kernel driver and accelerant in a supported Haiku build environment.
4. Test boot/runtime behavior and mode-set failure recovery.
5. On actual Rocket Lake hardware, test EDID, mode changes, DPMS, hotplug, cursor behavior, and plane behavior; record hardware, BIOS, monitor/adapter, logs, and results.

Until all relevant stages pass, report only source-level guardrails and implementation status. Do not claim hardware validation from Linux driver support or PCI-ID matches.

## Related source
- `../ROCKET_LAKE_DISPLAY_AUDIT.md`
- `../ROCKET_LAKE_ALCHEMIST_BATTLEMAGE_SOURCE_AUDIT.md`
- `../ROCKET_LAKE_ALCHEMIST_BATTLEMAGE_SUPPORT.md`

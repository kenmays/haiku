# Gen12 cursor and overlay display-plane plan

**Status:** design and source-audit plan; not an implementation or hardware-validation claim.  
**Target:** Rocket Lake / Gen12, with CPU-only 2D rendering preserved.

## Current safety state

- Hardware drawing hooks and engine/synchronization hooks remain disabled. Display-plane features must not introduce a general-purpose GPU command-submission dependency.
- The legacy hardware cursor hooks are now withheld for Generation 12 and newer. The existing cursor implementation uses `INTEL_CURSOR_*` register definitions and a two-plane monochrome payload, but the source audit has not established that those registers, formats, or pipe routing are correct for Rocket Lake.
- The legacy overlay API remains unavailable on Generation 6 and newer. Its implementation relies on the legacy overlay engine and primary-ring command path, hard-codes pipe 0, and does not provide a safe Gen12 implementation template.
- The current Gen12 combo-PHY modeset path now maps DDI A to DPLL0 and DDI B to DPLL1; DDI C and later ports fail with `B_NOT_SUPPORTED` until a TC/MG PHY-specific path exists.
- Gen12 DP currently uses a fixed 2.7 Gbit/s link reference. The code decodes 1/2/4-lane widths and rejects modes over the configured lane capacity with 5% headroom before modeset register writes. It does not implement higher-rate selection or link training/retraining.
- GitHub Actions source-only validation passes for the current branch; no Haiku C++ build or physical Rocket Lake test has been performed.
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
- `../../../kernel/drivers/graphics/intel_extreme/ROCKET_LAKE_DISPLAY_AUDIT.md`
- `ROCKET_LAKE_ALCHEMIST_BATTLEMAGE_SOURCE_AUDIT.md`
- `ROCKET_LAKE_ALCHEMIST_BATTLEMAGE_SUPPORT.md`


## Follow-up source comparison: upstream i915 plane architecture (2026-10-20)

The upstream Linux i915 source confirms that Display version 12 uses a generation-specific universal-plane implementation: `skl_universal_plane.c` selects `tgl_plane_caps()` and `tgl_plane_funcs` for display version 12 and later, rather than treating a legacy overlay engine as the modern sprite-plane interface. The plane setup also associates each plane with a pipe and advertises per-plane format/capability sets. See:
- https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/display/skl_universal_plane.c
- https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/display/intel_display_device.h

This is architectural corroboration, not a register-programming recipe to copy verbatim: Linux relies on atomic state validation, display power management, watermarks, plane update/latch sequencing, and hardware-specific workarounds that Haiku's current accelerant does not have as an equivalent transaction framework.

### Decisions from this comparison

1. Keep the existing legacy `overlay.cpp` API path disabled on Gen6+; do not route it to the Gen12 primary ring or reuse its legacy overlay register structure.
2. Treat the desired Haiku video path as a dedicated Gen12 sprite/universal-plane implementation with a small explicit supported-format set, strict geometry/buffer validation, and a bounded update/disable sequence. The legacy Haiku overlay API can be adapted only if its lifetime and synchronization semantics can be satisfied.
3. Keep the cursor implementation separate from the sprite/overlay plane path. Cursor programming must use the verified Gen12 cursor-plane register definitions and format rules; the existing legacy monochrome AND/XOR payload and `INTEL_CURSOR_*` offsets are not sufficient evidence.
4. Before coding register writes, produce a register map for Rocket Lake from the actual definitions in Haiku's private Intel header and upstream i915's Gen12 cursor/plane helpers. For each register, record generation, pipe/plane indexing, writable fields, alignment, and latch requirements. If that map cannot be supported by source evidence, return `B_NOT_SUPPORTED` and retain the hook gate.
5. No broad hook exposure, primary-ring initialization, or hardware drawing-hook changes are permitted as part of this display-plane work. CPU-only 2D rendering remains the recovery path.

### Immediate implementation checkpoint

- Strengthened `validate_rkl_alchemist_battlemage.py` to assert that all nine overlay hooks are inside the Gen6+ fail-closed gate and that cursor shape/move/show each retain an explicit Gen12 gate.
- These are source guardrails only. The validator has not been run in this execution environment after the latest edit; the configured GitHub Actions workflow is the authoritative next check.
- Do not claim a Gen12 cursor or overlay implementation from this documentation update. The actual register-map and runtime implementation tasks remain open.


### Cursor-specific upstream comparison

A further review of upstream i915's `intel_cursor.c` and `intel_cursor_regs.h` identifies several concrete differences from Haiku's legacy cursor path:
- i915 advertises an ARGB8888 cursor format, whereas Haiku currently builds a monochrome AND/XOR bitmap and programs a two-color palette. The format must not be assumed compatible with Gen12 cursor control.
- i915 derives the cursor surface address from the platform's memory-addressing requirements and the framebuffer plane offset; a CPU pointer or a legacy physical-address assumption is not enough to validate Haiku's cursor allocation.
- i915 documents platform-dependent update-arm behavior involving cursor control, position, and base writes. Register write ordering matters; a correct offset alone does not guarantee that a cursor move or shape update latches.
- The upstream implementation also has explicit clipping/position handling and plane-state validation. Haiku must preserve its own API semantics while validating coordinates and memory bounds before MMIO.

Reference: https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/display/intel_cursor.c

**Implementation gate:** before enabling Gen12 cursor hooks, resolve the exact Rocket Lake cursor-control format bits and CURBASE address requirements from the matching register definitions, and implement a Gen12-specific path with explicit ARGB8888 conversion (or another documented supported format), checked buffer sizing/alignment, signed position encoding, and documented update-arm order. Do not remove the existing generation gate based on this architectural comparison alone.

# Multihead app_server and Scene Manager Design

Status: staged design with partial geometry, configuration, damage, and device-enumeration groundwork. Full multi-output rendering, compositor, input routing, and hotplug are not yet implemented.

## Current implementation status

Implemented on the working branch:
- Global output geometry, scale metadata, negative coordinates, lookup/intersection helpers, and coordinate transforms.
- Saved output-position restoration with overlap fallback and persistence of the selected geometry.
- Output-local damage clipping with scale-aware outward rounding, and per-output background fills using each output's drawing engine.
- Per-window drawing engines can bind to unit-scale outputs; renderer offsets translate global drawing coordinates and damage/copy invalidation into framebuffer-local coordinates. Expose redraws and decorations are fanned out to intersected unit-scale outputs, while cross-output moves fall back to repaint instead of blitting between framebuffers. Client-issued drawing command streams are still bound to one selected output, so this is not yet correct full spanning-window rendering.
- Enumeration of distinct graphics device nodes into separate accelerant interfaces, while retaining the legacy VESA/framebuffer fallback.
- Hardware cursor shape/position routing to the output under the global pointer, plus use of the input server's global event stream when multiple outputs are active.
- Geometry tests for scene transforms, selection, invalid input, and output lifecycle.

Still required before calling this a complete multi-monitor implementation:
- Output enumeration for multiple connectors/heads on a single graphics device; separate `/dev/graphics` nodes are not equivalent to physical-output enumeration.
- Full rendering of client-issued drawing commands across all intersected outputs, complete per-output copy/overlay handling, and renderer scaling for non-unit DPI remain incomplete. Server-side expose and decoration redraws are fanned out on unit-scale outputs, but a spanning window's client content is not yet replicated across all outputs.
- Full input aggregation/routing for independent hardware devices, output hotplug/removal recovery, and transactional mode/layout changes. Cursor routing is only wired to the current global pointer stream and does not replace those missing pieces.
- End-to-end virtual-output tests and a real Haiku build/test run. No build or test execution has been verified for the current changes.


## Goal

Evolve `kenmays/haiku` into a native multi-output desktop architecture with modern compositor and window-manager capabilities while preserving the BeOS/Haiku public API and existing applications. The target is feature parity in the relevant desktop areas with Wayland compositors, X11 environments, and Windows 11; this is a staged engineering goal, not a claim of immediate full parity.

## Non-negotiable architectural decisions

1. Keep one `app_server` process and one coherent application/session model. Do not run one `app_server` per monitor.
2. Separate session/window policy from per-output rendering state.
3. Add a versioned, optional multi-output accelerant interface; retain the existing accelerant path as a compatibility fallback.
4. Preserve existing `BWindow`, `BView`, and `BScreen` behavior where possible. Add APIs instead of changing existing ABI contracts in place.
5. Make every implementation stage independently buildable and testable, with safe fallback to current single-framebuffer behavior.

## Proposed components

- **DisplayManager**: owns stable display IDs, connector/EDID metadata where available, mode lists, scale, rotation, logical geometry, enabled state, primary display, clone/extend layout, and hotplug notifications.
- **Desktop / SessionManager**: owns workspaces, global window identity, focus policy, input routing, application/session state, and global shortcuts. It must not own output-specific drawing engines.
- **DesktopScreen**: one instance per active output. Owns that output's window ordering/view, visible regions, clipping, damage tracking, cursor state, frame scheduling, and drawing backend.
- **SceneManager**: maintains a scene tree of windows/surfaces with parent-child relationships, transforms, opacity, clipping, stacking, workspace membership, and output visibility.
- **Compositor**: schedules per-output frames, composites supported surfaces, tracks damage, and provides a fallback path for legacy server-rendered windows. Begin with conservative 2D composition; GPU acceleration is an optional backend, not a prerequisite for correct multihead behavior.
- **InputRouter**: maps physical input devices and pointer coordinates to logical desktop space, selects the target output, routes keyboard focus and pointer/touch events, and handles hot-unplug recovery.
- **Accelerant v2**: versioned hooks for output enumeration, per-output mode setting, per-output buffer/presentation operations, vertical-blank/frame completion, and hotplug/event delivery. Capability-query every optional operation.
- **Interface Kit bridge**: expose display discovery and geometry through compatible APIs; ensure existing applications can move windows across outputs without reconnecting to another server.

## Data model sketch

Each output should have a stable ID distinct from its logical desktop rectangle. Physical pixel dimensions, refresh rate, color space, scale, and rotation belong to output state. Logical desktop coordinates are global and may be negative for monitors positioned left or above the primary display.

Each window has one global identity and geometry in logical coordinates. Its scene representation can intersect multiple outputs; output-local damage and clipping are derived from that geometry. Moving a window between outputs must not recreate the client connection or lose focus, view state, or drawing state.

Workspaces are desktop/session concepts, not per-monitor server instances. The initial implementation should preserve current workspace semantics; later work may support per-output workspace assignment behind an explicit policy/API.

## Feature targets

- Independent resolutions, refresh rates, rotation, scale, and primary-output selection.
- Extend, clone/mirror, and single-output layouts.
- Hotplug and unplug without restarting `app_server`; deterministic migration of windows and pointer state when an output disappears.
- Seamless window movement across output boundaries, including different DPI/scale settings.
- Correct focus, stacking, clipping, invalidation, screenshots, and cursor placement across outputs.
- Mixed legacy and accelerated rendering paths; software fallback for unsupported drivers.
- Multi-output configuration UI with preview, apply/revert timeout, and persistent settings.
- Per-output frame pacing and damage tracking; avoid repainting unchanged outputs.
- Accessibility, keyboard-only display recovery, and safe-mode fallback.

## Compatibility and rollout

### Stage 0: inventory and tests

Map `Desktop`, `Window`, `DrawingEngine`, `HWInterface`, `BScreen`, screen preferences, accelerant hooks, and input-server assumptions. Add tests to `test_app_server` using virtual outputs represented by host windows. Establish baseline single-monitor behavior and ABI checks.

### Stage 1: multi-output model without new hardware hooks

Introduce `DisplayManager` and `DesktopScreen` behind the current rendering backend. Support two or more virtual outputs, global logical geometry, cross-output window movement, output-local clipping/damage, and hot-unplug simulation. Keep a single virtual framebuffer where that is the only supported driver model.

### Stage 2: versioned accelerant interface

Add a capability-negotiated v2 interface with output IDs and hotplug events. Do not silently change signatures of existing accelerant hooks. Adapt one test/fake driver first, then hardware drivers with available test hardware. Legacy drivers continue to expose a single logical output or their existing virtual-desktop behavior.

### Stage 3: scene/compositor path

Add scene nodes and compositing incrementally. Start with opaque surfaces and software composition; then add alpha blending, transformed surfaces, overlays, and GPU-backed buffers only when the backend advertises support. Keep legacy drawing requests functioning through a compatibility surface/backend.

### Stage 4: user-facing features

Implement multi-monitor preferences, layout persistence, per-output mode selection, clone/extend, recovery timer, keyboard navigation, and sensible handling of monitor removal while windows are open.

### Stage 5: performance and hardening

Add frame pacing, damage coalescing, buffer lifetime validation, race/deadlock tests, failure injection, and benchmarks with mixed-resolution outputs. Verify restart/reconnect behavior and run the full Haiku build/test suite.

## Required correctness invariants

- One global window/session identity across all outputs.
- A disconnected output cannot leave dangling drawing engines, stale input targets, or unbounded damage regions.
- No client-visible ABI break for existing Interface Kit and accelerant users.
- Every output mode change is transactional: either all required state changes succeed or the previous mode/layout is restored.
- A failed accelerated path falls back safely rather than leaving a blank desktop.
- Coordinate conversion is explicit between logical desktop, output-local logical, and physical pixel coordinates.
- Locks and callbacks must not introduce lock-order inversions between window, desktop, drawing, and display-manager state.

## Validation matrix

Test one, two, and three virtual outputs; mixed resolutions and scales; negative logical coordinates; output reorder; hotplug/unplug while dragging, fullscreen, and modal dialogs are active; mode-set failure and timeout rollback; window spanning outputs; screenshot and cursor correctness; workspace switching; app_server restart/reconnect; legacy driver fallback; and accelerated/software rendering parity.

## Non-goals for the first milestone

- Replacing Haiku's application model with Wayland protocols.
- Creating one server per monitor.
- Requiring modern GPU support before multihead works.
- Claiming full Windows 11 or X11 feature parity in the first patch.
- Breaking existing BeOS-compatible application binaries.

## Initial implementation files to investigate

The exact paths and current signatures must be verified against the selected branch before coding. The expected areas are the app_server desktop/window/drawing engine and hardware interface, accelerant public/private headers and driver implementations, Interface Kit screen/window APIs, input_server coordinate routing, Screen preferences, and app_server test harness.

## Acceptance criteria for the first code milestone

1. A virtual two-output test passes with distinct output geometry and independent mode state.
2. A window can move between outputs without recreating its server connection.
3. Window invalidation and clipping are correct on both outputs.
4. Simulated unplug migrates windows and pointer focus to a surviving output.
5. Existing single-output tests and legacy accelerant behavior remain unchanged.
6. The full build and relevant tests pass; hardware-driver claims are limited to devices actually tested.

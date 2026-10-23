# Multihead Client Rendering Fan-out and Physical Output Discovery

Status: implementation contract for the next multihead stage. This document does not claim that client drawing fan-out or connector enumeration is implemented.

## Implementation snapshot

The current branch now has a pointer-free `OutputRenderTarget` record and
`VirtualScreen::BuildRenderPlan()`. The plan calculates per-output logical
clips, physical-pixel damage, scale, and integer renderer offsets. Each target
is stamped with `MultiheadScene::Generation()`, which advances on effective
output membership, frame, scale, and enabled-state changes; no-op updates do
not invalidate batches. A consumer must compare the target generation again
before execution and rebuild or discard stale work. Targets also snapshot the
framebuffer's virtual pixel dimensions, and validation rejects a target after
a mode-size change even when its logical frame and scale stayed the same. The existing single-output
server dispatcher now checks this token before accepting the clip, so a stale
output binding cannot reintroduce an unrestricted global clip. The
`ServerWindow` drawing path uses it to constrain the currently bound output,
and invalid targets are skipped without invalidating surviving outputs.
Geometry-level regression tests cover negative origins, mixed-scale pixel
clipping, largest-intersection output selection, and generation invalidation
for output changes versus no-op updates.

The private `LinkReceiver` API now supports copying the complete selected
message (header and payload) and installing a validated copy as a replay
message. Replay buffers are bounded by the protocol's maximum message size and
the header is copied before validation to avoid alignment assumptions. This
provides a stable payload source for a future fan-out dispatcher; it is not
currently used to execute commands on multiple engines.

This does **not** yet fan out a client drawing command to multiple engines.
The dispatcher still consumes and executes each protocol message once against
the window's currently bound engine. Do not treat the plan as a completed
render dispatcher. The remaining work is to parse each message once into a
replay-safe immutable command representation, execute it on every planned
target, and preserve canonical state/reply/layer/picture semantics. The plan's
pixel clip is currently metadata for that execution stage; the existing engine
continues to consume its logical clip.


## 1. Client drawing fan-out

### Required invariant

A client drawing command is accepted once in the existing ServerWindow protocol and interpreted against the window's global logical coordinates. For each command, app_server determines the enabled outputs intersecting the command's conservative global damage bounds and executes the command once per eligible output. Each execution uses that output's DrawingEngine and output-local render state. Do not replay commands by re-entering the client protocol handler, and do not mutate shared DrawState while visiting outputs.

### Dispatch boundary

Implement fan-out at the server-side drawing-command dispatch boundary in ServerWindow, not in the public BView API and not by creating a second application-level driver selection.

A dispatch batch should snapshot:
- the command payload and immutable drawing-state values needed by that command;
- the window's global frame and current effective drawing/visible region;
- the set of output IDs intersecting the command's conservative bounds;
- a generation token for the output configuration.

The batch must retain no raw Screen, HWInterface, or DrawingEngine pointer across an unlock or asynchronous handoff. Resolve output IDs to live targets while holding the established server-side ownership/locking discipline. If the output generation changes before execution, discard or rebuild the batch.

### Per-output execution

For each target output:

1. Intersect the command's global damage with the window's effective visible region and that output's logical frame.
2. Convert the clipped damage outward to physical pixels using the output scale, then clip to actual framebuffer bounds.
3. Configure an output-local drawing context from a copy/snapshot of the command's draw state. Global logical coordinates must remain the command coordinate space; apply the output transform exactly once.
4. Execute the same command payload against that output's DrawingEngine.
5. Invalidate/present only that output's physical damage.

The target selection is based on conservative command bounds, not just the window frame. Text, strokes, antialiasing, gradients, transformed shapes, and bitmap filtering may extend beyond nominal geometry; their damage bounds must include the renderer's fringe.

### State and compositing safety

- Never replay state-setting commands as side effects on the shared window DrawState. State-setting protocol messages update the canonical state once; render execution consumes an immutable snapshot.
- Preserve drawing mode, source-alpha mode, alpha function, pattern origin, pen size, font, transform, clipping, and view-to-window offsets per target.
- A command that uses destination-dependent operations (invert, erase, alpha compositing, copy-region, readback, or overlays) must operate on each target's own destination. Never copy pixels between unrelated framebuffers as a substitute for replaying the original draw command.
- CopyRegion is only valid within a single output framebuffer. When source and destination cross an output boundary, repaint from retained command/content state or use an explicit shared backing surface; do not blit between output framebuffers.
- Bitmap source rectangles remain in source-bitmap coordinates. Transform destination geometry and clip per output; do not rescale the source rectangle independently per output.
- Text metrics and returned pen positions remain in logical coordinates. Renderer scale must not leak into protocol-visible logical measurements.
- If allocation of the per-command snapshot fails, fail safely without partially mutating canonical draw state. Record damage for a later full repaint where possible.

### Initial implementation stages

A. Introduce a reusable per-output dispatch plan containing stable output IDs, global clip/damage, pixel clip, and transform metadata. Unit-test plan generation independently of hardware.

B. Route simple primitives and bitmap draws through the plan using independent output contexts. Do not advertise full spanning-window support until text, gradients, shapes, clipping, alpha modes, copy operations, and bitmap paths are covered.

C. Make state-setting commands canonical-only and ensure render execution uses a snapshot. Add a regression test where a state-setting message precedes a spanning draw and verify each output receives identical state.

D. Add damage coalescing per output and frame scheduling. A failed or removed target must not abort rendering to surviving outputs.

E. Keep the current single-output path as a fast path when the plan contains one target and its scale is 1.0.

## 2. Physical output discovery

### Distinguish devices from outputs

A /dev/graphics node identifies a graphics device/accelerant instance. It does not necessarily identify a physical connector or independently addressable display head. The current recursive device-node scan can discover separate devices and watch for node changes, but must not manufacture one logical display per connector unless the driver actually exposes separate independent framebuffers.

### Versioned optional accelerant capability

Add a versioned, optional multi-output capability surface while preserving existing accelerant hooks as the fallback. Capability detection must be explicit and size/version guarded. A driver that does not advertise the capability remains a single-output device.

The capability should support:
- querying the number of output records and retrieving records by index;
- stable per-device output identity distinct from the transient enumeration index;
- connector type and connection state (connected, disconnected, unknown);
- EDID or equivalent monitor identification when available;
- supported/current display modes per output;
- current physical pixel dimensions, color space, refresh timing, rotation, and scale metadata;
- setting a mode on one output without implicitly reconfiguring unrelated outputs;
- notifications for connection, disconnection, mode, and capability changes;
- optional per-output framebuffer/presentation operations when the hardware can actually drive heads independently.

All optional hooks must be capability-queried and failure-checked. Never call a hook merely because a function pointer exists in a newer header; the loaded accelerant may be older. Validate counts, sizes, IDs, mode records, and returned dimensions before publishing output state.

### Identity and reconciliation

Use a composite identity consisting of the stable graphics-device identity plus the driver's stable output ID. Connector names and enumeration indices are diagnostic metadata, not durable identity. EDID hashes can help correlate a monitor after movement but must not be the sole identity because identical monitors can share EDIDs.

On every rescan:
1. Enumerate device nodes and reconcile them by stable device path.
2. Query output capability only after the accelerant is initialized.
3. Reconcile output records by stable composite ID.
4. Add new outputs, update changed metadata, and mark missing outputs as removing before notifying owners.
5. Detach windows/render targets and transfer focus/cursor state before releasing output resources.
6. Publish a new output-generation number and trigger layout recomputation/full repaint on surviving outputs.

A disconnected connector is not automatically equivalent to a removed graphics device. Keep the device/accelerant alive when other connectors remain active. Conversely, removal of the device invalidates all output records owned by that device.

### Safe hot-unplug

Do not call Desktop/Window owner callbacks while holding the ScreenManager lock. Mark a target as removing under lock, notify owners outside the lock, and only release its framebuffer/accelerant after all render-target references have drained. Rendering batches carrying an old output generation must be cancelled or rebuilt. If the primary output disappears, select a surviving output deterministically, re-home windows whose geometry is now unreachable, restore a valid global pointer location, and request a full repaint.

### Compatibility and rollout

1. Keep current device-path enumeration and VESA/framebuffer fallback.
2. Add capability-query and output-list APIs with single-output adaptation for legacy drivers.
3. Integrate connector state and output IDs into ScreenManager/VirtualScreen without assuming one Screen per connector.
4. Add per-output mode setting only for drivers advertising it.
5. Add event-driven updates where available, retaining periodic/full reconciliation to recover missed notifications.
6. Test old accelerants, no-EDID monitors, identical monitors, connector churn, whole-device removal, allocation failures, and malformed driver responses.

## 3. Acceptance tests

### Rendering fan-out
- Window entirely on one output is drawn once and retains existing behavior.
- A window spanning two outputs produces clipped, non-overlapping target damage on both outputs.
- Negative logical origins and mixed scales produce correct output-local pixel bounds.
- Replaying a command never changes canonical draw state or changes text metrics.
- Transparent fills, invert/erase, gradients, transformed shapes, subpixel text, and bitmap scaling agree at the shared logical boundary.
- CopyRegion crossing output boundaries falls back safely instead of reading another framebuffer.
- Removing one target during queued work does not dereference freed output resources and does not prevent repaint on survivors.

### Discovery and lifecycle
- A single graphics device with multiple connectors yields multiple outputs only when the driver advertises independent output records.
- Multiple device nodes with one output each remain supported.
- Enumeration order changes do not change stable IDs or duplicate screens.
- Connector unplug leaves other outputs on the same device operational.
- Whole-device unplug drains render batches and removes all outputs owned by that device.
- Reconnect recreates output state without reusing stale pointers.
- Legacy accelerants continue through the single-output path.

## 4. Completion criteria

Do not call the project a complete multi-monitor implementation until the above tests pass, the relevant Haiku build targets compile, and at least one real multi-output driver or a faithful test backend validates independent connector enumeration, rendering fan-out, and hot-unplug recovery.

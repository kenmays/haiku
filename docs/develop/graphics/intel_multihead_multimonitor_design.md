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

## Horizontal spanning compatibility bridge: implementation status

The Intel accelerant now has an initial, explicitly opt-in horizontal-spanning bridge for the existing single logical desktop. It is not independent-screen support.

Implemented in `src/add-ons/accelerants/intel_extreme/mode.cpp`:

1. When initialization finds exactly two connected outputs assigned to distinct pipes A and B, it appends separate horizontal-span variants to the mode list. Each variant uses a virtual width of exactly twice the visible width and carries a private Intel mode marker; normal modes retain their original dimensions and flags.
2. `intel_propose_display_mode()` preserves that marker across mode validation, then rejects span requests if the topology changed, the virtual dimensions do not exactly match the span layout, or display starts are nonzero.
3. The modeset path strips the private marker before passing timings to port-specific hardware programming. The shared mode retains it so scanout-base programming can put pipe B at the right-hand region of the same framebuffer.
4. The scanout-base helper adjusts the B plane's horizontal start by one visible mode width, using the existing generation-family start-offset path. Normal modes still program both planes at the same origin.
5. `intel_move_display()` rejects horizontal panning in span mode, which would otherwise move the right-hand display beyond the exact two-panel surface. Vertical panning remains bounded by the virtual height.
6. Existing allocation range/overflow checks and modeset failure rollback remain in effect. The mode list expansion is optional: if its extra area cannot be allocated, ordinary modes remain available.

Important limitations and remaining work:

- This bridge relies on the existing A/B register programming paths; it has not yet been built or validated on physical Intel generations. Treat hardware correctness as **unverified** until per-generation PRM review and hardware tests are complete.
- The mode list contains duplicate visible timings with different virtual widths. The existing Screen preferences `ScreenMode.cpp` recognizes a horizontal combine mode when `B_SCROLL` is set and virtual width is twice the visible width, and its combine menu separates those modes. This makes the variants selectable through the existing UI path; verify end-to-end on the target build.
- The bridge currently requires exactly two connected outputs and distinct A/B pipe assignments. It does not support C/D or independent modes, and it does not promise automatic fallback if a display disconnects after mode selection; the next mode proposal/set must reject a now-invalid span request.
- Add tests for one output, two outputs assigned to one pipe, A/B clone, selected horizontal span, topology change between proposal and set, normal panning, vertical panning, allocation failure, oversized dimensions, and port setup failure.
- Review whether mode timing flags outside the private marker are preserved by all mode-list and Screen preferences paths on the target Haiku revision.

Do not confuse this bridge with the per-head architecture below. Independent extended desktop still requires per-head mode/framebuffer state and app_server integration.

## app_server and accelerant API capability review

This review was checked against the current branch's app_server and Screen preferences sources, especially `src/servers/app/ScreenManager.cpp`, `VirtualScreen.cpp`, `Screen.cpp`, `Desktop.cpp`, `ServerApp.cpp`, `headers/os/interface/Screen.h`, and `src/preferences/screen/multimon.cpp`.

### What Haiku already has

- `BScreen` and app_server protocol messages carry a `screen_id`; desktop configuration code stores screen configurations by ID, and `VirtualScreen` can hold multiple `Screen` objects. That is useful infrastructure, but it does not make one accelerant's separate pipes into separate screens automatically.
- The legacy Screen preferences combine UI selects virtual-desktop spanning modes by `B_SCROLL` plus virtual width/height exactly twice the visible timing. Its multi-monitor capability probe is specifically Radeon-private: `TestMultiMonSupport()` sends `RADEON_MODE_MULTIMON_REQUEST` through `ProposeMode()` and expects a Radeon reply marker. Intel cannot truthfully pass this probe without a documented cross-driver protocol or a corresponding UI/API change.
- `Desktop::SetScreenMode()` resolves a screen ID to one `Screen` object and calls that object's `SetMode()`. The `Screen` object wraps one `HWInterface`; the current Intel accelerant behind that interface still has one global current mode and framebuffer configuration.
- The existing `display_mode` contains virtual dimensions and display-start coordinates, which are sufficient for one logical virtual desktop or a clone mode, not a collection of independent head configurations in one mode object.

### Blocking app_server limitations found in source

1. `ServerApp.cpp` accepts only `B_MAIN_SCREEN_ID` in `AS_VALID_SCREEN_ID`; `AS_GET_NEXT_SCREEN_ID` always returns `B_ENTRY_NOT_FOUND`. Mode-list and hardware-information requests also ignore the supplied screen ID in several paths. This means the public screen-ID protocol is not currently enumerating multiple independently addressable displays through this path.
2. `Screen::SetFrame()` is a TODO and does nothing. `Screen::Frame()` returns the origin-based virtual dimensions from that screen's mode. `VirtualScreen::UpdateFrame()` computes a horizontal concatenation and maximum height, but explicitly says it does not use actual screen positions. It therefore cannot express arbitrary arrangements such as a display above, left of, or vertically offset from another.
3. `VirtualScreen::AddScreen()` overwrites its `fDrawingEngine` and `fHWInterface` fields for each screen and comments that this only works for single-screen configurations. This is a direct compositor/drawing-engine integration blocker for independently rendered screens, even though a list of `Screen` objects exists.
4. `ScreenManager::_ScanDrivers()` currently initializes one `AccelerantHWInterface` and stops; its source comments that scanning multiple drivers is future work. This is an additional enumeration limit for multiple adapters, distinct from the same-adapter/multiple-pipe problem.
5. The accelerant hook surface in `accelerant_protos.h` is singleton-oriented: `get_mode_list`, `propose_display_mode`, `set_display_mode`, `get_display_mode`, `get_frame_buffer_config`, `move_display`, DPMS, and retrace-semaphore hooks have no head identifier. The current implementation consequently cannot return independent modes, pitches, surfaces, DPMS states, or retrace completion per head through those hooks.
6. `src/preferences/screen/multimon.cpp` has a Radeon-specific private marker handshake, not a generic capability API. The doubled-width combine-mode menu is a legacy spanning UI, not a per-monitor placement or independent-resolution interface.

### Conclusion

**A true multihead/extended-desktop implementation cannot be completed solely inside the Intel accelerant while preserving the current app_server and accelerant interfaces.** The current API can support the existing one-logical-desktop spanning bridge, and app_server has some multi-`Screen` scaffolding, but a single Intel adapter with independent heads needs a new per-head contract plus app_server integration. The review does not establish that every display API must be replaced; it establishes that the current singleton accelerant hooks and current app_server screen enumeration/placement paths are insufficient.

### Recommended implementation sequence

1. **Define a versioned per-head contract.** Add head enumeration and stable IDs; per-head get/propose/set mode, framebuffer config, move/placement, DPMS, monitor/EDID, retrace/flip completion, and capability queries. Define clone groups separately from independently timed heads. Keep existing hooks as head-0 compatibility wrappers.
2. **Extend the accelerant and shared ABI.** Add per-head records and generation-specific plane/pipe state, separate framebuffer allocation/stride/format/start per head, shared PLL/link resource validation, hotplug invalidation, and transactional all-head modesets. Add a clear ABI version/size check for cloned shared areas; never change shared structure layout without coordinating kernel driver and accelerant.
3. **Make app_server enumerate real logical heads.** Update screen-ID validity/next-ID handlers and route mode lists, mode constraints, DPMS, retrace, framebuffer configuration, and monitor information to the selected head. Preserve stable IDs across connector hotplug where possible; invalidate stale IDs safely.
4. **Fix virtual-screen geometry and compositor ownership.** Store an explicit `BRect` origin/bounds per head and compute the union of all rectangles. Route drawing/composition to the correct head surface; replace the single `VirtualScreen::fDrawingEngine/fHWInterface` assumption with a well-defined multi-head compositor or a shared logical framebuffer with per-head scanout views. Update input/cursor bounds and window-screen selection for non-origin-aligned displays.
5. **Update Screen preferences.** Replace the Radeon-only `TestMultiMonSupport()` tunnel with a generic capability query and explicit per-display layout/mode UI. Keep the legacy combine option for spanning; don't use doubled virtual dimensions to represent independent monitors.
6. **Add compatibility and tests.** Existing single-head accelerants should continue working via wrappers. Test two independent modes, arbitrary display arrangement, cursor and window movement across boundaries, different DPMS states, hotplug/reconnect, failure rollback, multi-workspace configurations, direct framebuffer clients, overlays, and simultaneous drawing. Include a non-Intel multi-head implementation in the regression matrix.
7. **Build and stage the work across PRs.** Land the API/app_server scaffolding and tests separately from Intel hardware programming. Enable the Intel capability only after per-generation modeset and hardware validation. Until those layers land, advertise the Intel feature as spanning/clone only, not true independent multihead.

## Gap analysis against Linux Xe / shared Intel display code

This comparison uses the Linux kernel Xe documentation and Intel display documentation. Important architecture distinction: Xe owns the DRM device and GPU-specific services, while the Intel display/KMS implementation is shared with i915 under `drivers/gpu/drm/i915/display`. The right comparison is therefore Haiku's display stack versus Intel's shared display/KMS stack used by Xe, not just files under `drivers/gpu/drm/xe`.

### Capability gap matrix

| Area | Linux Xe + Intel display/KMS | Current Haiku Intel Extreme branch | Gap / action |
|---|---|---|---|
| Head/CRTC model | DRM exposes CRTCs, connectors, encoders, and planes as separate objects with routing constraints. | Ports can be detected and assigned pipes, but the accelerant publishes one current mode and one framebuffer configuration. | **Blocker:** introduce stable per-head objects and explicit port/pipe/plane routing. Do not treat connected ports as independent heads automatically. |
| Atomic modeset | Atomic state collects affected CRTCs/connectors/planes, validates the whole configuration, checks shared resources, then commits or rejects it. | The mode setter applies one target to all connected ports and has hand-written rollback. | **Critical:** add validate/prepare/commit/rollback phases for the complete requested head set. Reject impossible combinations before touching active hardware. |
| Plane state and surfaces | Plane source/destination rectangles, framebuffer format/modifier, offsets, and CRTC assignment are stateful and validated. | One shared framebuffer, pitch, bpp, and current mode feed A/B scanout programming. The current CPU renderer writes the active surface. | **Critical:** separate per-head framebuffer/plane state, validate alignment and stride for each generation, and add a real page-flip/synchronization contract before claiming tear-free rendering. |
| PLL and clock allocation | Display atomic checks reserve/release PLLs and validate clocks across affected CRTCs; shared resources may couple otherwise separate outputs. | PLL calculations and per-port setup exist, but the global modeset path is not a transaction across independently requested modes. | **Critical:** model PLLs as allocatable shared resources, calculate all heads together, and reject conflicts without partially programming outputs. |
| Link training and bandwidth | DisplayPort/eDP link capability negotiation, training, lane/rate selection, and MST topology are part of the Intel display stack. | Port classes and link setup exist, but there is no per-head state machine exposed through a multihead API; feature parity must be assessed by generation and port. | **High:** track negotiated link state and bandwidth per connector/head; implement bounded retries, cancellation, and cleanup. Do not advertise MST or modern link features until end-to-end support exists. |
| Hotplug and connector lifecycle | Hotplug detection and work handling update connector state and trigger reprobe/revalidation. | Port discovery and connection/EDID checks are present, but the existing logical screen configuration is not dynamically re-enumerated as independent heads. | **High:** add hotplug events, stable head identity, stale-ID invalidation, and safe teardown/rebuild of affected modes and surfaces. |
| EDID and monitor modes | Connector-specific probing feeds mode validation and link/resource checks. | Haiku DDC reads parsed EDID 1.x into `edid1_info`; the current public hook returns a single EDID record, not per-head raw E-EDID/CTA data. | **High:** bind EDID/mode data to each connector, validate extension checksums and lengths using supported Haiku APIs, and separate connector capability from current-head mode. |
| DPMS and power | Power domains, panel sequencing, link/PLL state, and runtime power are coordinated with display state. | DPMS is global and touches A/B PLL/plane state; there is no independently addressed per-head power operation. | **High:** per-head DPMS plus shared-resource reference counting and ordered panel/link/PLL sequencing. Preserve power state through rollback and suspend/resume. |
| Vblank, flips, and completion | KMS has per-CRTC vblank/event and page-flip semantics; display code supports synchronous and asynchronous flip paths where supported. | A shared vblank semaphore and bounded wait exist, but there is no per-head flip completion contract for independent scanout. | **High:** add per-head retrace/flip completion and distinguish a register write or generic completion from the hardware latch event. |
| Display geometry and compositor | DRM supplies per-CRTC modes and userspace display servers compose a desktop across output rectangles. | `VirtualScreen` has a single drawing-engine/hardware-interface assumption and computes concatenated geometry without actual monitor positions; `Screen::SetFrame()` is a TODO. | **Blocker outside accelerant:** implement app_server screen enumeration, independent bounds, compositor/drawing routing, and cursor/window geometry. |
| Capability reporting | DRM objects/properties expose supported features and constraints to userspace. | Screen preferences uses a Radeon-private combine-mode marker; Intel has no generic per-head capability query. | **High:** replace private probing with a versioned, generic capability contract and expose only implemented features. |
| Memory and execution services | Xe also provides GPU VM/buffer management, execution queues, synchronization, migration, and other GPU services beyond display. | This branch's requested CPU-only renderer is a software drawing path, not a Xe-equivalent GPU execution/memory stack. | **Separate scope:** do not claim full Linux Xe parity. Implement only the kernel/display/memory services required by the stated CPU-rendered scanout goal. |
| Validation | Linux display code uses atomic validation and has substantial CI/platform-specific checks and hardware workarounds. | No successful Haiku build or per-generation physical-hardware results have been established for the new spanning bridge. | **Release blocker:** compile and run tests first; then validate each generation, output class, failure path, and dual-head configuration on hardware. |

### Priority-ranked gaps

**P0 — architecture blockers**

1. Independent head enumeration and stable head identity across accelerant, shared state, app_server, and preferences.
2. Independent per-head modes and scanout surfaces, with plane/pipe routing represented explicitly.
3. Transactional multi-head mode validation and commit, including rollback of all affected pipes, PLLs, links, power state, and framebuffer ownership.
4. app_server multi-screen enumeration and correct screen geometry/compositing. The accelerant cannot compensate for the current singleton-oriented app_server paths.

**P1 — correctness and feature parity**

5. Joint PLL, pixel-clock, link-bandwidth, lane/rate, and connector routing validation.
6. Per-connector EDID/mode data, hotplug lifecycle, and monitor identity.
7. Per-head DPMS/panel sequencing and per-head vblank/flip completion.
8. Plane/format/pitch/alignment validation and a documented framebuffer ownership/synchronization contract.

**P2 — advanced parity (only where the hardware generation supports it)**

9. DisplayPort MST, DSC, FEC, HDMI/DP protocol features, PSR/DRRS/VRR, HDR/color pipeline, overlays, cursor planes, and audio coordination.
10. Power-domain/runtime-PM integration, underrun diagnostics, link recovery, suspend/resume, and systematic fault injection.

These are capability categories, not claims that every Intel generation supports every feature. The supported matrix must be per-generation and per-connector; the Xe display stack's current features cannot be transplanted wholesale into Gen2–Gen9.5 hardware.

### Important PCI-ID qualification

An upstream Xe PCI ID means the Linux driver recognizes that device for the Xe driver path; it does **not** prove that the Haiku driver can program its display engine, that the ID belongs to the same display IP generation, or that two simultaneous outputs work. The current inventory's user-requested “valid/imported” status must remain distinct from independently hardware-validated status. Keep separate fields for: `upstream_xe_id`, `Haiku_device_match`, `display_ip_identified`, `mode_set_tested`, and `multihead_tested`.

### Recommended Xe-informed implementation plan

1. Define the Haiku per-head API and shared-state version first. Specify ownership, stable IDs, supported capabilities, framebuffer lifetime, and synchronization semantics before changing register programming.
2. Add app_server enumeration and geometry support, with legacy accelerant wrappers for existing single-head drivers. This needs to be a coordinated Haiku API/app_server change, not an Intel-only private marker.
3. Implement a head/connector/pipe/plane resource model for Intel Extreme. Keep physical connectors, logical heads, pipes, planes, and shared PLL/link resources as distinct objects.
4. Implement a complete configuration planner: validate all heads, allocate surfaces, reserve compatible resources, prepare links/panels, commit the new configuration, and roll back safely on any failure.
5. Add per-head EDID, hotplug, DPMS, vblank/flip, and mode reporting. Add MST/DSC/VRR or other advanced features only after the core multihead path is stable.
6. Establish build/test gates and hardware evidence. For every enabled generation, record single-head modes, dual independent modes, clone, hotplug, DPMS, sleep/wake, rollback/fault injection, and concurrent drawing results. Keep untested IDs imported but explicitly unvalidated.

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

## Compatibility bridge versus true multihead

Haiku's legacy Radeon accelerant contains a virtual-desktop spanning approach in `src/add-ons/accelerants/radeon/multimon.c`: one large framebuffer is presented as one logical desktop, while the driver positions the two CRTCs at different offsets. This can be a useful intermediate milestone for Intel, but it is not equivalent to per-screen app_server support: applications still see one large screen, and windows can span the bezel or center between physical displays.

If implementing that bridge, keep it opt-in and explicitly distinguish it from independent heads. It must be enabled only when two compatible outputs are assigned to two supported pipes, the requested virtual dimensions exactly match the chosen layout, and both plane start offsets are representable and validated for that generation. Do not infer a spanning request from arbitrary oversized virtual dimensions, and do not silently change ordinary single-monitor panning semantics. The existing Intel mode list currently publishes visible-size virtual surfaces, so the bridge also needs a deliberate mode-list/configuration path before it can be selected.

The proper long-term API must identify a screen/head in mode enumeration, mode proposal and set/get, framebuffer configuration, display movement, DPMS, and other per-display operations. The app_server must retain a distinct mode, bounds, and framebuffer mapping for each screen and route drawing/window placement accordingly. Until that API exists in the target Haiku revision, driver-only independent heads cannot be exposed honestly through the current single-mode hooks.

## Current honest support statement

Current status: the driver remains a single logical head with shared-mode clone behavior and an initial opt-in horizontal-spanning compatibility bridge for exactly two connected outputs assigned to A/B. The bridge is source-implemented but not yet build- or hardware-validated. Independent multi-monitor/extended-desktop support remains incomplete until per-head shared state, per-head surface allocation, generation-specific scanout programming, transactional rollback, and Haiku display enumeration are implemented and tested.


## Per-head ABI specification

The next architecture artifact is [Haiku per-head display ABI proposal](haiku_per_head_display_abi.md). It defines version negotiation, stable head/connector identity, multi-head validate/prepare/commit/abort semantics, framebuffer ownership, per-head synchronization, app_server integration, and staged acceptance gates. It is a proposal for review; it is not an implemented ABI. Keep the Intel accelerant's current independent-head capability disabled until the contract and app_server work are implemented and validated.


## Incremental app_server changes landed

The branch now has a small app_server foundation change, not full multihead support:

- `AS_VALID_SCREEN_ID` checks IDs against screens currently registered with `ScreenManager`.
- `AS_GET_NEXT_SCREEN_ID` returns the next registered screen ID using the existing private protocol, or `B_ENTRY_NOT_FOUND` at the end.
- `VirtualScreen::ScreenByID()` resolves the main-screen alias to the primary list entry before searching explicit IDs. This avoids the old reverse-loop behavior accidentally resolving the main-screen alias to the last screen.
- The screen-brightness query routes through the selected screen's hardware interface instead of ignoring the requested ID and always using the desktop's default interface.
- `VirtualScreen::SetConfiguration()` now stops and releases the acquired screen set if any screen cannot be configured, rather than silently reporting success with a partial configuration.
- `VirtualScreen::AddScreen()` checks screen-list insertion failure and returns `B_NO_MEMORY` instead of leaving an untracked screen.
- `ScreenManager::ScreenChanged()` ignores notifications for screens that currently have no owner, avoiding a null-owner callback during lifecycle transitions.
- `VirtualScreen::UpdateFrame()` computes the union of the stored screen rectangles, including negative/non-origin bounds, instead of summing widths and taking the maximum height.
- `VirtualScreen::SetScreenFrame()` now updates a registered screen's stored rectangle and recalculates the virtual bounds. Newly added screens receive a temporary left-to-right placement until saved layout is applied.

These changes improve geometry bookkeeping only. They do not yet make `Screen::SetFrame()` functional, route compositor drawing to each screen, or implement independent per-head modes. `ScreenManager::_ScanDrivers()` still initializes one accelerant interface, and the Intel accelerant still exposes singleton mode/framebuffer hooks. Consequently this change does **not** create additional Intel logical heads, implement independent modes, or make the spanning bridge hardware-validated.

Review/build gate: verify the app_server compiles on the target Haiku revision; exercise zero/one/multiple registered screen IDs, invalid IDs, next-ID end-of-list behavior, main-screen alias resolution, and per-screen brightness routing with a test interface. Then continue with the versioned per-head accelerant contract and app_server geometry/compositor work.


### Screen-frame origin implementation

The app_server now stores a desktop origin on each `Screen`. `Screen::Frame()`
derives its bounds from that origin and the active mode dimensions, and
`Screen::SetFrame()` rejects invalid rectangles and records the requested
origin. `VirtualScreen` keeps its per-screen rectangles synchronized with
those origins and recalculates the virtual desktop union from the current
mode sizes. Newly added screens retain their temporary left-to-right
placement through this synchronization.

This is geometry plumbing, not complete multihead support: it does not yet
persist a user-selected layout, route compositor/drawing operations to each
screen, or provide independent per-head modesets and framebuffer surfaces.


### Saved screen-position restoration

`VirtualScreen::AddScreen()` now looks up the matching saved screen
configuration and restores its desktop origin while deriving the rectangle
size from the active mode. Screens without a valid saved frame retain the
deterministic right-of-desktop fallback. The active mode is queried after
fallback mode selection before it is written into the current configuration,
avoiding use of an uninitialized or stale local mode value on that path.

This restores stored positions at configuration setup; it does not add a
user-facing layout editor or prove that drawing and input are correctly
routed across independent heads. Those remain separate integration and test
requirements.

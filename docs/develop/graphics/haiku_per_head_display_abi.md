# Haiku per-head display ABI proposal

## Status

This is a proposed contract, not an existing Haiku ABI and not a claim that Intel Extreme currently implements independent heads. Review it against the target branch's interface conventions before coding. The goal is to avoid adding more singleton state while claiming Linux Xe-like multihead behavior.

## Compatibility rules

- Preserve existing accelerant hooks as the legacy single-head interface. A legacy accelerant is treated as one logical head.
- Add a versioned extension; never silently change the layout of an existing cloned shared structure.
- Every extension structure begins with a size and version field. Reject incompatible versions and undersized records.
- Use fixed-width Haiku types, explicit reserved fields, area IDs and offsets. Never put process-local pointers in cross-process shared records.
- A head ID identifies a logical independently timed display; connector IDs identify physical sinks. They are not interchangeable.
- Capability reporting describes implemented and tested behavior, not merely what silicon might support.

## Proposed operation set

Names are illustrative until agreed with Haiku API maintainers.

| Operation | Purpose | Contract |
|---|---|---|
| Enumerate heads | Discover logical heads | Stable ID, generation counter, connection state, capability bits |
| Get head info | Resolve one head | Pipe/plane assignment, connector IDs, bounds, active mode |
| Get mode list | Enumerate modes per head | Only modes individually supported; joint constraints checked separately |
| Validate configuration | Check a complete set of head modes | Check timings, format, PLL, pipes, planes, routing, link bandwidth, memory and power without touching hardware |
| Prepare configuration | Reserve resources | May fail without altering active scanout; returns a transaction token |
| Commit configuration | Apply prepared state | Program hardware and publish new state only after every requested head succeeds |
| Abort configuration | Cancel a prepared transaction | Release only resources reserved by that transaction |
| Get framebuffer config | Query one head | Format, stride, virtual dimensions, area/offset, allocation size and ownership |
| Move display | Set desktop origin | Operate on the selected head; preserve other origins |
| Set DPMS | Change one head's power | Respect shared resources and report actual state |
| Get monitor data | Query connector EDID | Connector identity, validated length/checksum and supported extension blocks |
| Wait for retrace/flip | Synchronize one head | Per-head sequence/event; command completion is not scanout-latch completion |
| Query capabilities | Report feature support | Separate hardware possibility, implementation and enabled status |

A legacy single-head mode setter can internally build a one-head configuration and use the same planner.

## Proposed state model

A per-head record should include head ID and generation; connector IDs; pipe, transcoder and plane IDs; requested and active mode; color space and pixel format; visible/virtual dimensions and display origin; stride; front and optional back surface area/offset/length; DPMS and connection state; mode-set sequence; and last error.

Device-wide state should include ABI version/size, device identity, area/MMIO references, synchronization, allocator metadata and transaction sequence. It should not duplicate mutable per-head values in unrelated singleton globals.

Validate every offset/length pair with overflow-safe arithmetic against its owning area/aperture. Never assume a shared area is mapped at the same virtual address in every clone.

## Transaction and ownership contract

1. Validate the complete requested configuration before touching active hardware.
2. Reserve all replacement surfaces and shared resources before disabling a pipe.
3. Save enough old state to restore every affected pipe, plane, PLL, link, connector and power state.
4. Program each generation-specific block in documented order with bounded waits and explicit errors.
5. Publish new state only after all requested heads are active and verified.
6. Do not free old scanout memory until hardware can no longer fetch it.
7. If rollback fails, retain possibly scanned allocations and disable the affected head safely. A bounded leak is preferable to use-after-free.
8. Treat hotplug as a topology generation change; a transaction prepared against an old topology must fail commit and release its reserved resources.

A two-head commit must not report success if only one head was programmed. Where hardware cannot switch both pipes at the exact same instant, document the transition window while preserving all-or-fail state publication.

## app_server work required

- Make screen-ID validation and next-screen enumeration return real logical screens rather than only the main screen.
- Route mode, DPMS, retrace, EDID and framebuffer requests by screen/head identity.
- Persist explicit bounds in Screen::SetFrame; calculate the union of actual screen rectangles, including negative origins and vertical offsets.
- Replace VirtualScreen's single drawing-engine/hardware-interface ownership assumption with a compositor contract that routes damage to the correct head or to a shared desktop surface with defined scanout views.
- Keep cursor, window placement, screen selection, workspaces and direct-framebuffer clients consistent with those bounds and surface lifetimes.
- Use a generic capability query in Screen preferences. Radeon-specific private markers and doubled-width spanning modes remain legacy paths, not the independent-head API.

## Staged implementation and acceptance gates

### Stage A — contract and app_server scaffolding
Agree on structure layout, hook negotiation, stable IDs, errors and transaction ownership. Test version/size negotiation, invalid IDs, removed-head generations, bounds union, negative origins and legacy single-head fallback. Do not advertise independent heads yet.

### Stage B — shared state and Intel planner
Add per-head records while keeping existing fields as head-0 compatibility aliases. Add pure validation for pipe/plane/connector/PLL/link/memory constraints before register programming. Add allocation-failure injection and rollback tests. Continue rejecting unsupported C/D and generation-specific paths rather than guessing register offsets.

### Stage C — app_server routing and geometry
Implement enumeration and explicit screen bounds; route drawing and screen operations to the right interface. Test window/cursor movement, mixed resolutions, hotplug and independent DPMS using a fake/test HWInterface before enabling physical Intel heads.

### Stage D — hardware enablement
For each exact PCI ID and display IP generation, record build identity, connector, pipe/plane, mode, format, stride, EDID, link training, DPMS, hotplug, rollback, suspend/resume and dual-head results. Track separately: upstream driver recognizes ID; Haiku matches device; display IP is identified; single-head modeset tested; independent dual-head modeset tested. Imported IDs must not be marked hardware-validated without results.

## Non-goals

This contract does not by itself implement Linux Xe parity, GPU command submission, GPU virtual memory, MST, DSC/FEC, VRR, HDR, PSR, overlays or page flipping. Each requires a generation-appropriate backend and tests. The initial goal is reliable CPU-rendered scanout with honest connector, mode and multihead reporting.

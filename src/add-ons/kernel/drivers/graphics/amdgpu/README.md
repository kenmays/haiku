# Native AMDGPU driver for Haiku

This directory is a new driver, separate from `radeon` and `radeon_hd`.
The existing drivers remain unchanged.

## Current status: PCI and IP discovery foundation

The driver carries all 312 exact AMD vendor/device IDs present in the
current upstream Linux `amdgpu_drv.c` PCI table in `pci_ids.h`, plus the
upstream class-wide IP-discovery matches for VGA, other display, and
processing-accelerator functions. Exact IDs are binary-searched in the table;
class-wide matching is used for devices supported by Linux's IP-discovery
fallback. The ASIC-family decoder remains incomplete, so an exact PCI match
does not mean that an ASIC is initialized or supported. For type-0 endpoints
the driver records BAR size/type metadata, including 64-bit BAR slot pairing
and prefetchability.

The PCI scan is bounded to 4096 functions and retained records to 64 devices.
The exact-ID table is synchronized against upstream Linux and should be
refreshed when that PCI match table changes.

`amdgpu_discovery.cpp` now provides two hardware-independent parsing layers,
based on Linux's public `drivers/gpu/drm/amd/include/discovery.h` layout:

- `amdgpu_parse_discovery_binary()` validates the enclosing AMD discovery
  binary signature, supported directory versions (v1/v2), declared binary
  length, directory count and bounds, binary byte-sum checksum, IP table
  directory entry, IP table signature/ID, and IP table byte-sum checksum.
- `amdgpu_parse_ip_discovery_table()` parses the bounded IP table versions
  1-4, decodes 32-bit and version-4 64-bit base addresses, and returns
  hardware ID, die ID association, instance, IP revision, sub-revision,
  variant, and base-address metadata.

Both APIs use caller-provided storage and perform no allocation or MMIO.
They validate offsets, lengths, counts, and record boundaries before reading
variable-length records. Empty/duplicate die descriptors and overlapping table
directory ranges are rejected. Binary versions are restricted to v1 and v2;
unknown versions are not guessed.

The `tests` subdirectory contains a `SimpleTest` fixture executable covering
valid v1/v2 binaries, v4 64-bit addresses, multi-die parsing, output-capacity
limits, truncated input, checksum corruption, unsupported binary versions,
invalid directory offsets, overlapping table ranges, and die-ID mismatches.
Run `jam amdgpu_discovery_test` in a Haiku build tree to build the test target,
then execute the resulting test binary. These tests are added but have not
yet been run in this environment. The parser is not yet wired to hardware
discovery or firmware loading.

**This is not yet a functioning graphics driver.** It does not yet activate
PCI ownership, alter PCI command bits, install interrupts, load firmware,
allocate VRAM/GTT, submit commands, or publish a graphics device node.

The new `device_resources.cpp` module provides explicit, currently dormant
lifecycle helpers:

- initialize per-device resource state before use;
- reserve a PCI function through Haiku's PCI bus manager;
- map a caller-selected, validated, non-prefetchable type-0 register BAR with
  `map_physical_memory()`, page alignment, overflow checks, and uncached
  memory attributes; prefetchable VRAM/framebuffer apertures are rejected and
  require a separate write-combining mapping path;
- unmap each area and release the PCI reservation in reverse order.

The caller must zero-initialize the resource structure and call
`amdgpu_init_device_resources()` before claiming a device. The caller must
select the BAR only after the matching ASIC/IP block layout is known. No BAR
is selected automatically. BAR mapping verifies the reserved PCI function
matches the supplied device record, checks the endpoint header and BAR type,
requires a non-prefetchable BAR, a page-aligned physical base and page-multiple
size, rejects address overflow or an address that cannot be represented by
`phys_addr_t`, and maps with uncached attributes. Failed unmaps prevent release
of the PCI reservation, so the driver cannot silently relinquish a device
while its MMIO mapping remains live.

These helpers are not invoked by `init_driver()` yet: activating them before
a working device node, hardware initialization, and teardown path exists
would steal devices from the existing Radeon drivers without providing a
usable replacement.

AMDGPU is the intended preferred owner for AMDGPU-supported devices once its
probe, initialization, teardown, and accelerant paths are complete. It must
not preempt existing Radeon drivers merely by matching vendor ID while it is
still a non-binding discovery stub. Existing Radeon drivers remain the safe
fallback for unimplemented generations and unsupported functions.

The accelerant helper in `accelerant/display_mode_flags.h` adds
`B_PARALLEL_ACCESS` only when the caller has verified safe concurrent
framebuffer access. The current PCI-only stage must pass `false`; the flag
must not be advertised before framebuffer mapping, cache/coherency, and
scanout synchronization are implemented and tested.

## Architecture

The implementation is organized around AMD IP blocks, not Linux kernel
abstractions. Generation-specific SoC descriptions will select versioned IP
implementations. The native Haiku layer owns PCI resources, MMIO mappings,
DMA constraints, interrupts, memory allocation, synchronization, device
lifetime, and the accelerant-facing ABI.

Planned subsystems:

- PCI and ASIC identification: vendor/device/revision and subsystem IDs;
- SoC/IP discovery: common, GMC/UMC, IH, PSP, SMU, GC, SDMA, DCN, VCN;
- firmware: bounded parsing, signature policy, version compatibility;
- memory: VRAM, GTT, GART, GPUVM and buffer objects;
- execution: rings, indirect buffers, fences, scheduling and reset;
- display: DCN/DCE, connectors, link training, modes and scanout;
- userspace: Haiku accelerant and Mesa winsys integration.

Linux DRM, TTM, dma_fence, workqueues, IRQ and ioctl implementations are
reference material only; they are not directly portable kernel dependencies.

## Safety invariants

1. Do not write a register until the matching ASIC/IP revision is identified.
2. Validate every BAR index, resource length, offset and alignment before MMIO.
3. Never treat CPU virtual, CPU physical and device DMA addresses as equivalent.
4. Check all size arithmetic for overflow before allocation or mapping.
5. Validate firmware headers, lengths, versions and trust requirements before
   copying or submitting firmware.
6. Do not enable bus mastering until DMA mappings and teardown are correct.
7. Install interrupts only after handler state is initialized; mask and
   synchronize them before freeing state.
8. Do not submit untrusted command buffers without validating packet streams,
   address ranges and engine permissions.
9. On partial initialization failure, unwind completed stages in reverse order.
10. Keep unsupported IP blocks disabled; never guess register layouts.

## Generation coverage

“All AMDGPU generations” is a project goal, not a claim that one code path or
one PCI-ID table can support every ASIC. Support must be staged by IP revision
and SoC family. Legacy pre-GCN Radeon generations remain the responsibility
of existing Haiku Radeon drivers unless a deliberate migration is designed.

## Development sequence

1. Buildable, non-binding PCI discovery and dormant resource helpers.
2. Bounded discovery-binary and IP-table parsers (implemented; not yet hardware-wired).
3. Integrate a generation-specific discovery source and validate real firmware fixtures.
4. Bind resources to a real device node and add complete initialization/teardown.
5. Expand and verify ASIC/IP discovery tables, including revision and subsystem quirks.
6. Firmware parser and secure firmware loading.
7. Display-only modesetting and framebuffer scanout.
8. VRAM/GTT/GART and GPUVM.
9. Interrupts, rings, fences, reset and SDMA.
10. GC command submission and Mesa OpenGL integration.
11. DCN/display hardening, video IP, power management and broader generations.
12. Automated tests, hardware stress, suspend/resume and failure injection.

Each stage must build and pass tests before hardware functionality is enabled.

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
valid v1/v2 binaries, v4 64-bit addresses, multi-die parsing, firmware and
option-ROM source callbacks, short reads, invalid source kinds, source and
scratch bounds, output-capacity limits,
truncated input, checksum corruption, unsupported binary versions, invalid
directory offsets, overlapping table ranges, and die-ID mismatches.
Run `jam amdgpu_discovery_test` in a Haiku build tree to build the test target,
then execute the resulting test binary. These tests are added but have not
yet been run in this environment. The parser is not yet wired to hardware discovery or firmware loading.

`amdgpu_discovery_source.cpp` adds a bounded provider interface for firmware
and option-ROM data. The provider supplies an exact-read callback, a readable
extent, and a source kind; the reader first fetches the 12-byte binary header,
checks the declared 16-bit length against both the source extent and caller's
scratch capacity, then fetches only the remaining declared bytes and invokes
the validated binary parser. Provider short reads must be reported as errors.
The callback is deliberately the only source-specific operation: firmware
lookup, ROM extraction, mapping, and lifetime remain the caller's job. The
source reader does not parse arbitrary ROM layouts, map a PCI BAR, probe
registers, or access hardware. Hardware-register discovery remains deferred
until an ASIC-specific safe access method is defined.

`amdgpu_vbios.cpp` adds an offline parser for a caller-supplied PCI option-ROM
image chain. It validates each image's 0x55aa signature, PCIR pointer and
structure bounds, nonzero 512-byte image length, image extent, per-image
8-bit checksum, and last-image indicator. It walks chained images without
reading beyond the supplied buffer and selects the first image whose PCIR vendor ID is AMD (0x1002) and has
an ATOM/MOTA ROM header. It returns the PCI vendor/device IDs with the image
metadata. The ATOM header size, format/content revisions, master
data-table pointer, and master data-table extent are checked before returning
offset metadata. The parser does not read PCI configuration space, enable or
map a ROM BAR, allocate memory, or execute ATOM command tables. A valid ROM
chain without ATOMBIOS returns `B_ENTRY_NOT_FOUND`.

The `amdgpu_vbios_test` target builds synthetic single- and multi-image ROM
fixtures and covers valid ATOM selection, valid non-ATOM chains, bad checksums,
invalid PCIR signatures, out-of-range image lengths, unterminated chains,
and invalid ATOM header pointers. These tests are added but have not yet been
run in a Haiku build environment. Synthetic fixtures validate parser
behavior, not compatibility with the full range of vendor VBIOS images;
real-world firmware samples and cross-checking against upstream Linux remain
required before using this parser to initialize hardware.

`amdgpu_vfct.cpp` adds an offline parser for AMD's ACPI VFCT table using
upstream Linux's public VFCT structure layout. It validates the ACPI signature,
declared table length and checksum, the VBIOS image region, each 28-byte PCI
image header, and the embedded image extent. It matches vendor/device and PCI
device/function; a bus mismatch is accepted but reported because VFCT records
the POST-time bus number and PCI bus renumbering can occur. The embedded ROM
is then checked by the existing bounded option-ROM/ATOMBIOS parser, and its
PCIR vendor/device IDs must agree with the VFCT entry. Subsystem IDs and
revision are returned as metadata, not used as a substitute for the mandatory
PCI identity match.

The `amdgpu_vfct_test` target covers multi-entry selection, exact and
renumbered-bus matching, absent devices, PCI candidate identity translation,
rejection of invalid vendors/unrecognized records/bridge functions, no-copy
size queries and their reference cleanup, successful-null-handle cleanup,
ACPI checksum failure, invalid VBIOS offsets, truncated images, and a mismatch
between the VFCT identity and the embedded PCIR IDs. The tests are registered
but have not been run in a Haiku build environment. The parser accepts caller-owned bytes and performs no ACPI
call, allocation, PCI access, or MMIO.

The separate `amdgpu_firmware_context_test` target tests the per-device
firmware state machine with injected provider stubs: initialization, size-query
and load transitions, undersized buffers, stale-query invalidation, PCI-ID
mismatches, changed image size between query and load, and clearing any copied
image bytes after a mismatch. It can be built with
`jam amdgpu_firmware_context_test`. Like the other AMDGPU tests, it is
registered but has not yet been executed in a Haiku build environment.

Haiku's existing `acpi_module_info::get_table()` calls ACPICA
`AcpiGetTable()`, which acquires a reference, but the legacy public module
does not expose `AcpiPutTable()`. To preserve the old module ABI, the ACPI
bus manager now exports a separate `acpi_table_module_info` module named
`B_ACPI_TABLE_MODULE_NAME` (`bus_managers/acpi/tables/v1`). Its
`get_table()` and `put_table()` methods are a paired reference-counted
interface. The table module holds the legacy ACPI module loaded for its own
lifetime, and `put_table()` calls ACPICA `AcpiPutTable()`.

`amdgpu_vfct_provider.cpp` uses that paired interface. It keeps the VFCT
reference while validating the table and copies the selected VBIOS image into
caller-owned storage before releasing the reference on every post-acquisition
path. A 16 MiB maximum table size bounds the provider's input.
`amdgpu_vfct_device_from_pci_device()` translates only a retained, recognized
AMDGPU endpoint candidate into the VFCT BDF/vendor/device key; it rejects
non-AMD functions, unrecognized records, and PCI bridges.
`amdgpu_load_vbios_for_pci_device()` is the per-device convenience entry point:
it performs that translation and then acquires/releases the ACPI table module
around the bounded copy. A no-copy size-query API returns `B_BUFFER_OVERFLOW`
with the required image size, allowing callers to allocate an exact caller-owned
buffer and then fetch the image in a second, independently referenced call.
The caller still owns the destination buffer and must
not treat returned VBIOS metadata as authorization to execute firmware or
initialize hardware. `amdgpu_firmware_context` adds a caller-owned per-PCI-
function state machine (`identified` → `size known` → `loaded`). It queries the
VFCT image size, requires both VFCT and embedded PCIR vendor/device IDs to match
the candidate PCI function, and loads only into caller-supplied storage. It
retains no buffer pointer and performs no allocation, PCI reservation, BAR
mapping, or firmware execution. The context can be reset explicitly after its
caller has finished with the image. The lower-level provider remains independently
testable. The provider does
not reserve or initialize the GPU, map a ROM BAR, or retain a pointer into the
ACPI table.

### Native Haiku provider audit

The current Haiku interfaces were checked before adding a hardware-backed
provider:

- The legacy PCI bus-manager interface in `headers/os/drivers/PCI.h`
  exposes `pci_info.u.h0.rom_base`, `rom_base_pci`, and `rom_size`
  metadata for type-0 functions, plus PCI configuration-space read/write
  callbacks. It does **not** expose a callback that reads or extracts an
  expansion-ROM image. The newer per-device interface in
  `headers/os/drivers/bus/PCI.h` likewise has no ROM-read operation.
- The PCI bus manager's ROM metadata is not proof that the ROM BAR is enabled,
  mapped, readable, or safe to touch. Enabling a ROM BAR requires a controlled
  configuration-space transaction and restoration of its prior state; the
  driver must also account for device ownership, address translation, image
  extent, and mapping lifetime. The AMDGPU driver therefore does not toggle
  the ROM-enable bit or map a ROM BAR as part of discovery.
- Haiku's legacy `acpi_module_info::get_table()` delegates to ACPICA's
  `AcpiGetTable()`, which acquires a reference. The legacy module has no
  paired release callback. A separate `acpi_table_module_info` module now
  exposes `get_table()/put_table()` without changing the layout or name of
  the existing `acpi_module_info` ABI. The AMDGPU VFCT provider holds that
  reference only while parsing and copying the selected image. This is an
  ACPI-defined firmware source, not a generic EFI firmware-file service.
- The private ACPI header describes ACPI table structures and signatures;
  it does not establish a supported kernel API for retrieving arbitrary
  firmware blobs from EFI/UEFI variables or the bootloader.

Consequently there is currently no verified native Haiku API in these
interfaces that can directly populate an AMDGPU discovery source. The generic
bounded reader remains the integration point, but no production firmware or
option-ROM provider is registered. A future provider must be separately
designed and reviewed: it must return a stable, bounded byte extent and exact
read semantics, and must own the complete lifetime of any mapped or copied
source. An option-ROM provider additionally needs an explicit policy for ROM
BAR enable/restore, address translation, image-chain scanning, checksum and
ATOMBIOS validation, and failure cleanup. Until those contracts exist, the
safe behavior is to leave hardware discovery dormant rather than infer a
source from `rom_base` or ACPI metadata.

**This is not yet a functioning graphics driver.** It does not yet activate
PCI ownership, alter PCI command bits, install interrupts, load firmware,
allocate VRAM/GTT, submit commands, or publish a graphics device node.

The new `device_resources.cpp` module provides explicit, currently dormant
lifecycle helpers:

- initialize per-device resource state before use;
- reserve a PCI function through Haiku's PCI bus manager;
- map a caller-selected, validated type-0 register BAR with
  `map_physical_memory()`, page alignment, overflow checks, and uncached
  memory attributes; prefetchable framebuffer/VRAM apertures are rejected,
  with the narrowly validated Vega 10 BAR 5 register-aperture exception mapped
  uncached rather than write-combined;
- unmap each area and release the PCI reservation in reverse order.

The caller must zero-initialize the resource structure and call
`amdgpu_init_device_resources()` before claiming a device. The caller must
select the BAR only after the matching ASIC/IP block layout is known. No BAR
is selected automatically. BAR mapping verifies the reserved PCI function
matches the supplied device record, checks the endpoint header and BAR type,
requires a non-prefetchable BAR except for the exact Vega 10 BAR 5 register
aperture, a page-aligned physical base and page-multiple size, rejects address overflow or an address that cannot be represented by
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


### Per-device lifecycle coordinator

`amdgpu_device_context.h/.cpp` now binds one enumerated PCI function to its
resource bookkeeping and firmware context. The lifecycle states are
`UNINITIALIZED`, `DISCOVERED`, `CLAIMED`, `NODE_READY`, and `STOPPING`. Initialization validates and
copies the candidate identity, initializes the dormant resource structure,
and creates the VBIOS context; it does not reserve the function or map a BAR.
Claiming is a separate explicit operation. Release tears down resources first
and resets firmware state only after resource release succeeds. If unmapping
or unreserving fails, the object is retained intact so cleanup can be retried.

`init_driver()` now constructs these per-function contexts for the bounded
PCI candidate list and unwinds already initialized contexts if a later
candidate fails. `uninit_driver()` releases contexts in reverse order before
dropping the PCI module reference. The driver still returns no published
devices and does not call the explicit claim operation. This is lifecycle
integration only, not device-node registration or GPU activation.

The `amdgpu_device_context_test` target injects resource and firmware
operations to verify discovery initialization is non-binding, failed claims
do not advance state, explicit claims advance state, failed release preserves
ownership bookkeeping for retry, successful release clears the object, and
non-AMD candidates are rejected. Build with
`jam amdgpu_device_context_test`; the test has been registered but not run
in this environment.


### Device-node registration and lifetime boundary

The driver now has a gated legacy device-node hook path. Each discovered
function receives a stable BDF-derived name
(`graphics/amdgpu_<bus><device><function>`, with each value zero-padded to
two hexadecimal digits), and the
registry exposes a node only when its context has explicitly reached
`AMDGPU_DEVICE_NODE_READY`. That transition is allowed only after an explicit
PCI claim and must be called by a future ASIC-specific initialization path
after the node's backing state is complete.

The open hook takes a per-device reference under the registry lock and the
device-context mutex. The close hook completes close-side work; the free hook
drops the reference. Teardown is serialized against registry lookup, moves
the context to a stopping state, and refuses to release resources while any
open cookie remains. Resource-release failure restores the prior lifecycle
state so cleanup can be retried. A statically initialized registry mutex protects the name list; each
context has its own kernel mutex for lifecycle and open-reference state. The
per-device mutex is retained across payload resets so a reused static context
does not reinitialize a live lock.

The current driver does not mark any context node-ready, so
`publish_devices()` returns no device list and `find_device()` returns no
hooks for discovered-only candidates. The hooks deliberately return
`B_NOT_SUPPORTED` for read, write, and control operations until a real
accelerant-facing ABI is implemented. This prevents the foundation driver
from binding to an AMD GPU and blocking the existing Radeon drivers before
it can provide usable graphics functionality.

The lifecycle test now checks stable BDF naming, rejection of opens before
node readiness, the claim-to-ready transition, open reference acquisition,
teardown refusal while a cookie is live, and successful release after the
reference is dropped. The test is registered but has not been run in a Haiku
build environment.


### Reversible ASIC initialization coordinator

`amdgpu_init_sequence.h/.cpp` adds a hardware-independent bring-up coordinator
for the next integration stage. It runs four explicit operations in order:
PCI device claim, firmware load/validation, register-aperture mapping, and
ASIC initialization. Each completed stage is recorded independently and has
a corresponding teardown callback.

If a stage fails, completed stages are unwound in strict reverse order.
Rollback stops at the first cleanup failure so that lower-level resources are
not released while a dependent upper-level resource may still be active. The
sequence retains the remaining completion bits and reports both the original
initialization error and the rollback error, allowing a caller to retry cleanup.
The callback table is required to provide all acquisition and release
operations; missing teardown callbacks are rejected before any operation runs.

The coordinator is deliberately not called by `init_driver()` and does not
supply ASIC-specific register programming. It is an integration primitive:
future generation-specific code must provide audited operations, use validated
IP/ASIC identity before MMIO, and mark a device node ready only after successful
initialization and complete backing-state construction.

The `amdgpu_init_sequence_test` target covers successful staged initialization,
reverse-order teardown, duplicate initialization rejection, failure rollback,
retained ownership after a cleanup failure, and retrying that cleanup. It is
registered but has not been run in a Haiku build environment.


### First ASIC/IP profile: Vega 10 (metadata validation only)

`amdgpu_vega10_profile.h/.cpp` introduces the first conservative ASIC profile
validator. It accepts only exact PCI-ID candidates already classified as
Vega 10, then checks the IP-discovery records against the Vega 10 IP topology
and revisions used by upstream Linux: GC 9.0.1, NBIO 6.1.0, MMHUB/ATHUB 9.0.0,
UMC 6.0.0, MP0/MP1 9.0.0, OSSSYS/HDP 4.0.0, SDMA0/1 4.0.0, DF 2.1.0,
THM/SMUIO 9.0.0, UVD 7.0.0, VCE 4.0.0, and DCI 12.0.0. Hardware IDs are
taken from upstream `soc15_hw_ip.h`; the per-ASIC revision tuple is
cross-checked against upstream `amdgpu_discovery.c`.

The validator rejects missing or duplicated required instances, duplicate
hardware-ID/instance pairs, absent base-address metadata on required register-bearing IPs, unexpected IP
revisions, class-only PCI matches, and non-Vega 10 candidates. Unknown extra
IP blocks are retained and ignored rather than treated as known hardware.
It produces a small profile summary but does not interpret discovery base
addresses as CPU physical addresses, select a PCI BAR, map MMIO, touch
registers, load firmware, or authorize node publication.

This is intentionally a validation boundary, not yet an operational Vega 10
driver. The next step is to bind verified Vega 10 register-block indices to reviewed ASIC register-base tables. The current `amdgpu_register_aperture` layer is deliberately narrower: it accepts only an explicitly selected, already mapped non-prefetchable BAR and an ASIC-reviewed BAR-relative window; it never converts IP-discovery base-address values into CPU addresses. It validates the PCI-function identity, mapping lifetime, window bounds, dword alignment, and every 32-bit register access before returning an offset. It performs no MMIO read or write. The `amdgpu_register_aperture_test` target covers BAR identity, mapping state, prefetchability, aperture bounds, alignment, and end-of-window accesses. It is registered but has not been run in a Haiku build environment.

The `amdgpu_vega10_profile_test` target covers a valid profile, unsupported
GC revision, missing register-base metadata, duplicate IP instances, and
rejection of class-only PCI identification. It is registered but has not been
run in a Haiku build environment.

### Vega 10 register-base metadata (no MMIO)

`amdgpu_vega10_registers.h/.cpp` adds a small native table for the Vega 10
SOC15 register-base indices. The values are transcribed from upstream Linux
`drivers/gpu/drm/amd/include/vega10_ip_offset.h` and the selected IP hardware
IDs from `soc15_hw_ip.h`. The active Linux `vega10_reg_base_init()` path
assigns pointers to these `IP_BASE.instance[]` tables, while
`SOC15_REG_OFFSET` adds a generated register's base-indexed register offset.
These values therefore remain register indices in dword units.

The resolver currently exposes only the table entries that were checked
against the active static table: the NBIO register-base slot (hardware ID
108, named NBIF_HWID in upstream soc15_hw_ip.h), MP0 (255), DF (46), UVD (12),
ATHUB (35), VCE (32), GC (11), MMHUB (34), HDP (41), OSSSYS (40), SDMA0
(42), SDMA1 (43), UMC (150), THM (3), SMUIO (4), and PWR (10). It permits
only instance zero, because all other instances in these table entries are
empty. Unsupported hardware IDs, instances, and empty segments are rejected.
NBIO segment zero is explicitly retained as a valid zero base.

MP1 is intentionally not exposed: the upstream static `MP1_BASE` initializer
used by `vega10_reg_base_init()` and the generated `MP1_BASE__INST0_SEG0`
macro disagree. DCI also remains unsupported because this Vega 10 register
base header does not provide a corresponding DCI table. These are explicit
gaps, not guessed offsets.

`amdgpu_vega10_register_byte_offset()` performs checked dword-base addition
and converts the resulting SOC15 register index to a byte offset. It does not translate IP-discovery addresses, map memory, or perform
MMIO. The separate `amdgpu_vega10_mmio.h/.cpp` layer now validates the exact
Vega 10 PCI identity and revalidates the complete discovery topology before
resolving the already-mapped BAR 5 window through
`amdgpu_resolve_register_aperture()`. This follows upstream Linux's
Bonaire-and-newer BAR 5 selection.

Linux's SOC15 common path sets `rmmio_remap.reg_offset` to
`0x80000 - PAGE_SIZE` (0x7f000 on a 4 KiB page) and its bus address to
BAR 5 plus that offset. This is used by the separate KFD userspace
`MMIO_REMAP` allocation/mmap path; it is not a hole in the kernel's direct
register aperture. Linux's normal `amdgpu_device_rreg()` reads directly from
`rmmio + reg * 4` whenever the byte offset is within `rmmio_size`.
Accordingly, the Haiku resolver preserves linear BAR 5 offsets across the
KFD remap page and relies on mapped-size, dword-alignment, and access-width
checks. The current minimum mapping size is 64 KiB, enough to cover the
whitelisted Vega 10 `GB_ADDR_CONFIG` probe at byte offset 0x98f8.

The generic mapping helper still rejects prefetchable framebuffer/VRAM BARs.
It permits a prefetchable BAR 5 only for an exact Vega 10 PCI-ID/ASIC match;
the mapping is always `B_UNCACHED_MEMORY`, never write-combined. KFD's
userspace remap page is not implemented by this driver yet.

The `amdgpu_vega10_mmio_test` target covers BAR 5 selection, exact identity,
topology revalidation, minimum mapping size, linear offsets before, within, and after the KFD remap page, plus rejection
of unaligned and out-of-range accesses. It is registered but
has not been run in a Haiku build environment.

A narrowly scoped read-only probe is now available as
`amdgpu_vega10_read_gb_addr_config()`. It reads only GC
`mmGB_ADDR_CONFIG` (register index 0x063e, BASE_IDX 0), whose Vega 10 GC
segment-zero base is 0x2000 dwords; the resulting BAR 5 byte offset is
0x98f8. The register definition is taken from upstream
`gc_9_0_offset.h`, and Linux's Vega 10 GC code uses this register in its
configuration path. The probe uses a single aligned volatile 32-bit read,
converts the little-endian register value, and contains no write or
read-modify-write operation. It revalidates the exact PCI identity and full IP-discovery topology, and
checks the existing BAR 5 mapping/resource identity before reading. The caller must
still establish PCI ownership and map BAR 5. It is not called by
`init_driver()` or any device publication path.

The `amdgpu_vega10_probe_test` target uses a RAM-backed fake aperture to
check the fixed register offset, endian conversion, and rejection of invalid
aperture/output arguments. It does not exercise a GPU or validate actual
hardware behavior. It is registered but has not been run in a Haiku build
environment. No hardware probe has been performed.

The `amdgpu_vega10_registers_test` target covers known base values, NBIO's
valid zero base, dword-to-byte conversion, unsupported instances/segments
and IP IDs, the MP1 exclusion, null outputs, and arithmetic overflow. It is
registered but has not been run in a Haiku build environment.

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
3. Integrate firmware/ROM providers and validate real binary fixtures (bounded source reader, reference-safe ACPI VFCT provider, and caller-owned per-device VBIOS state machine implemented; real firmware fixtures remain).
4. Add a real device node and accelerant; the per-function context and reverse-order cleanup coordinator are implemented, but node binding and active hardware initialization remain deferred.
5. Expand and verify ASIC/IP discovery tables, including revision and subsystem quirks.
6. Firmware parser and secure firmware loading.
7. Display-only modesetting and framebuffer scanout.
8. VRAM/GTT/GART and GPUVM.
9. Interrupts, rings, fences, reset and SDMA.
10. GC command submission and Mesa OpenGL integration.
11. DCN/display hardening, video IP, power management and broader generations.
12. Automated tests, hardware stress, suspend/resume and failure injection.

Each stage must build and pass tests before hardware functionality is enabled.

### MMIO lifetime serialization

The per-device context now exposes serialized BAR map/unmap operations and a
context-owned Vega 10 `GB_ADDR_CONFIG` probe entry point. The raw
`amdgpu_vega10_read_gb_addr_config_unlocked()` helper is explicitly named and
documented as requiring the caller to hold the owning context mutex. Each operation takes
the device-context mutex and verifies that the device is in the claimed or
node-ready state. The probe keeps that mutex held across profile/aperture
validation and the actual volatile MMIO load. Device release takes the same
mutex before unmapping areas and releasing PCI ownership, so it cannot tear
down the BAR while a context-mediated read or mapping operation is in flight.

Once a context is active, callers must use
`amdgpu_device_context_map_mmio_bar()`,
`amdgpu_device_context_unmap_mmio_bar()`, and
`amdgpu_device_context_read_vega10_gb_addr_config()`; they must not call the
lower-level mapping or probe helpers concurrently with context teardown. The
mutex is a sleepable lifecycle lock: these APIs are for thread context, not
interrupt handlers. No lock is held across interrupt callbacks, and no
interrupt path is implemented.

The device-context test now injects the low-level map, unmap, and probe
operations. It verifies that all three are rejected before PCI claim, that
claimed-state operations are routed through the context, and that the probe
callback executes while the context mutex is held. This is a lock-contract
test using stubs, not a parallel stress test or proof of hardware MMIO safety.
The test is registered but has not been run in a native Haiku build
environment.

### Vega 10 register access policy (read-only allowlist)

`amdgpu_vega10_reg_access.h/.cpp` adds a per-register policy gate keyed by
IP hardware ID, instance, register segment, register index, and access type.
Unknown registers and unreviewed access types are rejected by default. The
initial allowlist contains only GC instance 0, segment 0,
`GB_ADDR_CONFIG` (register index 0x063e), and grants read access only.
Writes to that register and reads or writes to every other register are denied.

The Vega 10 probe now consults this policy before resolving the register
offset or dereferencing the BAR mapping. The policy does not itself touch
MMIO and is not a general-purpose register accessor. New entries must be
reviewed for read side effects, write semantics, reset/interrupt behavior,
and ASIC/IP revision applicability before being added. In particular,
status, FIFO, interrupt, doorbell, index/data, reset, and write-one-to-clear
registers are not implicitly safe to read or write.

The `amdgpu_vega10_reg_access_test` target checks the single allowed read,
write denial, adjacent-register denial, wrong instance/segment/IP denial,
and invalid access-type rejection. The policy and probe tests are registered
but have not been run in a native Haiku build environment. No GPU hardware
validation has been performed.


### MMIO mapping failure injection

The resource mapper now has a narrow per-call OS-operation seam:
`amdgpu_map_mmio_bar_with_ops()` and
`amdgpu_unmap_mmio_bar_with_ops()` accept explicit map/unmap callbacks.
Production context operations continue to use the native Haiku
`map_physical_memory()` and `delete_area()` wrappers. The callbacks are
borrowed only for the duration of one operation; there is no global mutable
test hook and no change to production call sites.

The `amdgpu_device_resources_test` target injects map failure, successful
mapping, failed deletion, retry after failed deletion, and cleanup of an area
whose mapping call returned an area ID without a usable virtual address.
The invariant is that a failed delete preserves the area ID and associated
mapping metadata, while successful deletion clears them. This test target is
registered but has not been run in a native Haiku build environment. It does
not test PCI unreserve failure or real concurrent teardown/read behavior.


### PCI ownership failure injection

The same per-call resource-operation table now includes PCI reserve and
unreserve callbacks. The production wrappers delegate to Haiku's
`pci_module_info` methods; tests can inject deterministic outcomes without
reserving a physical function. The new claim/release-with-ops entry points
share the same validation and bookkeeping paths as the production entry
points.

The resource lifecycle test now covers failed reservation, successful
reservation, failed unreserve with ownership retained, and successful retry.
A failed reserve leaves an initialized but unclaimed resource object; a failed
unreserve preserves the reservation identity so teardown can be retried.
These are callback-contract tests only: they do not validate PCI bus-manager
behavior or actual hardware ownership. The test remains registered but has
not been run in a native Haiku build environment.


### Multi-BAR release failure ordering

The resource lifecycle test now also seeds two independently mapped BAR areas and
forces deletion of the higher-numbered BAR to fail. It verifies that release
stops immediately: the failed area and lower-numbered area remain recorded, and
PCI unreservation is not attempted. After the injected failure is removed, a
second release attempt must delete the remaining areas in descending BAR order
and only then unreserve the PCI function. This specifically exercises the
dependency ordering required for safe retry after partial teardown.

The test uses callback-controlled area IDs and does not invoke real
`delete_area()`, reserve a PCI function, or prove concurrent MMIO safety. It
is registered in `amdgpu_device_resources_test` and remains unexecuted in a
native Haiku build environment.

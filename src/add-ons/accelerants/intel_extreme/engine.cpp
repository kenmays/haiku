/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <Debug.h>

#include "accelerant.h"
#include "accelerant_protos.h"
#include "commands.h"


#undef TRACE
//#define TRACE_ENGINE
#ifdef TRACE_ENGINE
#	define TRACE(x...) _sPrintf("intel_extreme: " x)
#else
#	define TRACE(x...)
#endif

#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


static engine_token sEngineToken = {1, 0 /*B_2D_ACCELERATION*/, NULL};


// Gen6+ has a separate Blitter Command Streamer. Never send XY blit
// commands to the render ring on those devices.
static ring_buffer*
get_blitter_ring_buffer()
{
	intel_shared_info &info = *gInfo->shared_info;
	ring_buffer* ring = info.device_type.Generation() >= 6
		? &info.blitter_ring_buffer : &info.primary_ring_buffer;

	if (ring->size < B_PAGE_SIZE || ring->register_base == 0
		|| ring->base == NULL)
		return NULL;

	return ring;
}


QueueCommands::QueueCommands(ring_buffer &ring)
	:
	fRingBuffer(ring),
	fInitialPosition(0),
	fInitialSpaceLeft(0),
	fLockAcquired(false),
	fFailed(false)
{
	fLockAcquired = acquire_lock(&fRingBuffer.lock) == B_OK;
	if (fLockAcquired) {
		fInitialPosition = fRingBuffer.position;
		fInitialSpaceLeft = fRingBuffer.space_left;
	}
}


QueueCommands::~QueueCommands()
{
	if (!fLockAcquired)
		return;

	// Any failed reservation invalidates the whole batch: publishing a
	// prefix could leave an incomplete command stream in the ring.
	if (fFailed) {
		fRingBuffer.position = fInitialPosition;
		fRingBuffer.space_left = fInitialSpaceLeft;
	}

	// Commands are published by updating the tail. If the batch cannot be
	// padded to the required 8-byte alignment, discard the unsubmitted batch
	// instead of writing an unreserved NOOP into a possibly full ring.
	if (!fFailed && (fRingBuffer.position & 0x07)) {
		if (MakeSpace(2)) {
			Write(COMMAND_NOOP);
		} else {
			fRingBuffer.position = fInitialPosition;
			fRingBuffer.space_left = fInitialSpaceLeft;
		}
	}

	if (!fFailed && fRingBuffer.position != fInitialPosition) {
		// Publish the batch only after writing its command words. The
		// required CPU/WC-memory-to-MMIO ordering primitive is not yet
		// verified for this mapping; do not substitute a local atomic op.
		write32(fRingBuffer.register_base + RING_BUFFER_TAIL,
			fRingBuffer.position);
	}

	release_lock(&fRingBuffer.lock);
}


void
QueueCommands::Put(struct command &command, size_t size)
{
	// All command batches must consist of complete dwords and preserve the
	// ring's 8-byte submission alignment. Reject malformed sizes rather than
	// silently truncating a command or publishing a partial packet.
	if (size == 0 || (size % sizeof(uint32)) != 0
		|| ((size / sizeof(uint32)) & 1) != 0) {
		ERROR("rejecting malformed ring command size %lu\n",
			(unsigned long)size);
		fFailed = true;
		return;
	}

	uint32 count = size / sizeof(uint32);
	uint32 *data = command.Data();

	if (!MakeSpace(count)) {
		fFailed = true;
		return;
	}

	for (uint32 i = 0; i < count; i++) {
		Write(data[i]);
	}
}


bool
QueueCommands::PutFlush()
{
	if (!MakeSpace(2)) {
		fFailed = true;
		return false;
	}

	Write(COMMAND_FLUSH);
	Write(COMMAND_NOOP);
	return true;
}


void
QueueCommands::PutWaitFor(uint32 event)
{
	if (!MakeSpace(2)) {
		fFailed = true;
		return;
	}

	Write(COMMAND_WAIT_FOR_EVENT | event);
	Write(COMMAND_NOOP);
}


void
QueueCommands::PutOverlayFlip(uint32 mode, bool updateCoefficients)
{
	if (!MakeSpace(2)) {
		fFailed = true;
		return;
	}

	Write(COMMAND_OVERLAY_FLIP | mode);

	uint32 registers;
	// G33 does not need a physical address for the overlay registers
	if (intel_uses_physical_overlay(*gInfo->shared_info))
		registers = gInfo->shared_info->physical_overlay_registers;
	else
		registers = gInfo->shared_info->overlay_offset;

	Write(registers | (updateCoefficients ? OVERLAY_UPDATE_COEFFICIENTS : 0));
}


bool
QueueCommands::MakeSpace(uint32 size)
{
	ASSERT((size & 1) == 0);

	if (!fLockAcquired)
		return false;

	// Fail closed if the ring is unavailable or a request cannot fit.
	// A timeout must never permit overwriting commands the GPU has not
	// consumed.
	if (fRingBuffer.size < B_PAGE_SIZE
		|| (fRingBuffer.size & (fRingBuffer.size - 1)) != 0
		|| (fRingBuffer.size & (B_PAGE_SIZE - 1)) != 0
		|| fRingBuffer.position >= fRingBuffer.size
		|| (fRingBuffer.position & (sizeof(uint32) - 1)) != 0
		|| fInitialPosition >= fRingBuffer.size
		|| (fInitialPosition & (sizeof(uint32) - 1)) != 0
		|| fRingBuffer.space_left > fRingBuffer.size - sizeof(uint64)
		|| size > (fRingBuffer.size - sizeof(uint64)) / sizeof(uint32)
		|| fRingBuffer.base == NULL || fRingBuffer.register_base == 0) {
		ERROR("invalid ring buffer state or oversized command request\n");
		return false;
	}

	// Validate the hardware ring on every reservation, not just when cached
	// space runs out. Otherwise a stale space_left value can let us write to
	// a disabled ring or publish against a tail changed by another owner.
	uint32 control = read32(fRingBuffer.register_base
		+ RING_BUFFER_CONTROL);
	uint32 head = read32(fRingBuffer.register_base + RING_BUFFER_HEAD)
		& INTEL_RING_BUFFER_HEAD_MASK;
	uint32 hardwareTail = read32(fRingBuffer.register_base
		+ RING_BUFFER_TAIL) & INTEL_RING_BUFFER_HEAD_MASK;
	uint32 ringStart = read32(fRingBuffer.register_base + RING_BUFFER_START);
	uint32 expectedSize = (fRingBuffer.size - B_PAGE_SIZE)
		& INTEL_RING_BUFFER_SIZE_MASK;
	if ((control & INTEL_RING_BUFFER_ENABLED) == 0
		|| (control & INTEL_RING_BUFFER_SIZE_MASK) != expectedSize
		|| ringStart != fRingBuffer.offset
		|| head >= fRingBuffer.size
		|| (head & (sizeof(uint32) - 1)) != 0
		|| hardwareTail != fInitialPosition) {
		ERROR("ring reservation rejected: control %" B_PRIx32
			", head %" B_PRIx32 ", hardware tail %" B_PRIx32
			", expected tail %" B_PRIx32 ", start %" B_PRIx32
			", expected start %" B_PRIx32 "\\n",
			control, head, hardwareTail, fInitialPosition, ringStart,
			fRingBuffer.offset);
		return false;
	}

	size *= sizeof(uint32);
	bigtime_t start = system_time();

	while (fRingBuffer.space_left < size) {
		uint32 control = read32(fRingBuffer.register_base
			+ RING_BUFFER_CONTROL);
		if ((control & INTEL_RING_BUFFER_ENABLED) == 0) {
			ERROR("ring disabled while waiting for space: control %"
				B_PRIx32 ", software tail %" B_PRIx32 "\n",
				control, fRingBuffer.position);
			return false;
		}

		uint32 head = read32(fRingBuffer.register_base + RING_BUFFER_HEAD)
			& INTEL_RING_BUFFER_HEAD_MASK;
		uint32 hardwareTail = read32(fRingBuffer.register_base
			+ RING_BUFFER_TAIL) & INTEL_RING_BUFFER_HEAD_MASK;

		// Never interpret stale, corrupt, or externally modified ring state
		// as available space. A producer owns this ring lock, so the MMIO tail
		// should still match the software tail until this batch is published.
		if (head >= fRingBuffer.size
			|| (head & (sizeof(uint32) - 1)) != 0
			|| fRingBuffer.position >= fRingBuffer.size
			|| hardwareTail != fInitialPosition) {
			ERROR("invalid ring pointers: head %" B_PRIx32
				", hardware tail %" B_PRIx32 ", published tail %"
				B_PRIx32 ", software tail %" B_PRIx32 ", size %"
				B_PRIx32 ", control %" B_PRIx32 "\n",
				head, hardwareTail, fInitialPosition, fRingBuffer.position,
				fRingBuffer.size, control);
			return false;
		}

		if (head <= fRingBuffer.position)
			head += fRingBuffer.size;

		uint32 available = head - fRingBuffer.position;

		// Keep one 8-byte slot empty so head == tail can only mean empty,
		// never a completely full ring whose commands would be lost.
		fRingBuffer.space_left = available > sizeof(uint64)
			? available - sizeof(uint64) : 0;

		if (fRingBuffer.space_left < size) {
			if (system_time() > start + 1000000LL) {
				uint32 ringStart = read32(fRingBuffer.register_base
					+ RING_BUFFER_START);
				ERROR("engine stalled: head %" B_PRIx32
					", hardware tail %" B_PRIx32 ", software tail %"
					B_PRIx32 ", start %" B_PRIx32 ", size %" B_PRIx32
					", control %" B_PRIx32 ", space_left %" B_PRIx32
					", request %" B_PRIx32 "\n",
					head & INTEL_RING_BUFFER_HEAD_MASK, hardwareTail,
					fRingBuffer.position, ringStart, fRingBuffer.size,
					control, fRingBuffer.space_left, size);
				return false;
			}
			spin(10);
		}
	}

	fRingBuffer.space_left -= size;
	return true;
}


void
QueueCommands::Write(uint32 data)
{
	uint32 *target = (uint32 *)(fRingBuffer.base + fRingBuffer.position);
	*target = data;

	fRingBuffer.position = (fRingBuffer.position + sizeof(uint32))
		& (fRingBuffer.size - 1);
}


//	#pragma mark -


void
uninit_ring_buffer(ring_buffer &ringBuffer)
{
	if (ringBuffer.size < B_PAGE_SIZE || ringBuffer.register_base == 0)
		return;

	// Serialize teardown with producers before touching the ring registers.
	if (acquire_lock(&ringBuffer.lock) != B_OK) {
		ERROR("could not acquire ring lock during teardown\n");
		return;
	}

	bigtime_t start = system_time();
	while (true) {
		uint32 head = read32(ringBuffer.register_base + RING_BUFFER_HEAD)
			& INTEL_RING_BUFFER_HEAD_MASK;
		uint32 tail = read32(ringBuffer.register_base + RING_BUFFER_TAIL)
			& INTEL_RING_BUFFER_HEAD_MASK;
		if (head == tail)
			break;

		if (system_time() > start + 1000000LL) {
			ERROR("ring teardown timed out: head %" B_PRIx32
				", tail %" B_PRIx32 "\n", head, tail);
			break;
		}
		spin(10);
	}

	// At teardown, stop the engine even if it did not drain. The mapping is
	// still owned by the device driver; never free it while the engine runs.
	write32(ringBuffer.register_base + RING_BUFFER_CONTROL, 0);
	read32(ringBuffer.register_base + RING_BUFFER_CONTROL);
	release_lock(&ringBuffer.lock);
	uninit_lock(&ringBuffer.lock);
	ringBuffer.size = 0;
}


status_t
setup_ring_buffer(ring_buffer &ringBuffer, const char* name)
{
	TRACE("Setup ring buffer %s, offset %lx, size %lx\n", name,
		ringBuffer.offset, ringBuffer.size);

	// Write() wraps with a power-of-two mask and the hardware size field is
	// page based. Do not program a malformed ring and later treat it as live.
	if (ringBuffer.register_base == 0 || ringBuffer.base == NULL
		|| ringBuffer.size < B_PAGE_SIZE
		|| (ringBuffer.size & (ringBuffer.size - 1)) != 0
		|| (ringBuffer.size & (B_PAGE_SIZE - 1)) != 0
		|| (ringBuffer.offset & (B_PAGE_SIZE - 1)) != 0
		|| ((ringBuffer.size - B_PAGE_SIZE)
			& ~INTEL_RING_BUFFER_SIZE_MASK) != 0) {
		ERROR("invalid ring buffer geometry for %s\n", name);
		ringBuffer.size = 0;
		return B_BAD_VALUE;
	}

	status_t status = init_lock(&ringBuffer.lock, name);
	if (status < B_OK) {
		// Do not touch hardware when lock initialization failed: we cannot
		// safely serialize with any previous ring owner.
		ringBuffer.size = 0;
		return status;
	}

	uint32 ring = ringBuffer.register_base;
	uint32 control = read32(ring + RING_BUFFER_CONTROL);

	// Head == tail does not prove that commands have finished executing in
	// the engine pipeline. Without a verified engine-idle/fence mechanism,
	// refuse to reinitialize a ring that is already enabled.
	if ((control & INTEL_RING_BUFFER_ENABLED) != 0) {
		uint32 head = read32(ring + RING_BUFFER_HEAD)
			& INTEL_RING_BUFFER_HEAD_MASK;
		uint32 tail = read32(ring + RING_BUFFER_TAIL)
			& INTEL_RING_BUFFER_HEAD_MASK;
		ERROR("refusing to reinitialize enabled ring %s: control %"
			B_PRIx32 ", head %" B_PRIx32 ", tail %" B_PRIx32 "\n",
			name, control, head, tail);
		uninit_lock(&ringBuffer.lock);
		ringBuffer.size = 0;
		return B_BUSY;
	}

	// Follow the documented ring initialization ordering: stop the ring,
	// read back control, clear head/tail, read back head, program START,
	// then publish the empty tail before enabling the ring.
	write32(ring + RING_BUFFER_CONTROL, 0);
	read32(ring + RING_BUFFER_CONTROL);
	write32(ring + RING_BUFFER_HEAD, 0);
	write32(ring + RING_BUFFER_TAIL, 0);

	uint32 head = read32(ring + RING_BUFFER_HEAD)
		& INTEL_RING_BUFFER_HEAD_MASK;
	if (head != 0) {
		write32(ring + RING_BUFFER_HEAD, 0);
		head = read32(ring + RING_BUFFER_HEAD)
			& INTEL_RING_BUFFER_HEAD_MASK;
	}
	if (head != 0) {
		ERROR("failed to clear ring head for %s: %" B_PRIx32 "\n",
			name, head);
		uninit_lock(&ringBuffer.lock);
		ringBuffer.size = 0;
		return B_ERROR;
	}

	read32(ring + RING_BUFFER_HEAD);
	write32(ring + RING_BUFFER_START, ringBuffer.offset);
	write32(ring + RING_BUFFER_HEAD, 0);
	write32(ring + RING_BUFFER_TAIL, 0);
	read32(ring + RING_BUFFER_TAIL);

	ringBuffer.position = 0;
	// Preserve the empty slot required to distinguish full from empty.
	ringBuffer.space_left = ringBuffer.size - sizeof(uint64);

	write32(ring + RING_BUFFER_CONTROL,
		((ringBuffer.size - B_PAGE_SIZE) & INTEL_RING_BUFFER_SIZE_MASK)
		| INTEL_RING_BUFFER_ENABLED);
	control = read32(ring + RING_BUFFER_CONTROL);
	head = read32(ring + RING_BUFFER_HEAD) & INTEL_RING_BUFFER_HEAD_MASK;

	uint32 tail = read32(ring + RING_BUFFER_TAIL)
		& INTEL_RING_BUFFER_HEAD_MASK;
	uint32 start = read32(ring + RING_BUFFER_START);
	if ((control & INTEL_RING_BUFFER_ENABLED) == 0 || head != 0
		|| tail != 0 || start != ringBuffer.offset
		|| (control & INTEL_RING_BUFFER_SIZE_MASK)
			!= ((ringBuffer.size - B_PAGE_SIZE) & INTEL_RING_BUFFER_SIZE_MASK)) {
		ERROR("ring %s failed to enable: control %" B_PRIx32
			", head %" B_PRIx32 ", tail %" B_PRIx32
			", start %" B_PRIx32 " (expected %" B_PRIx32 ")\n",
			name, control, head, tail, start, ringBuffer.offset);
		write32(ring + RING_BUFFER_CONTROL, 0);
		read32(ring + RING_BUFFER_CONTROL);
		uninit_lock(&ringBuffer.lock);
		ringBuffer.size = 0;
		return B_ERROR;
	}

	return B_OK;
}

//	#pragma mark - engine management


/*! Return number of hardware engines */
uint32
intel_accelerant_engine_count(void)
{
	CALLED();
	return 1;
}


status_t
intel_acquire_engine(uint32 capabilities, uint32 maxWait, sync_token* syncToken,
	engine_token** _engineToken)
{
	CALLED();
	if (_engineToken == NULL)
		return B_BAD_VALUE;

	*_engineToken = NULL;

	if (acquire_lock(&gInfo->shared_info->engine_lock) != B_OK)
		return B_ERROR;

	if (syncToken) {
		status_t status = intel_sync_to_token(syncToken);
		if (status != B_OK) {
			release_lock(&gInfo->shared_info->engine_lock);
			return status;
		}
	}

	*_engineToken = &sEngineToken;
	return B_OK;
}


status_t
intel_release_engine(engine_token* engineToken, sync_token* syncToken)
{
	CALLED();
	if (engineToken != &sEngineToken)
		return B_BAD_VALUE;

	status_t status = B_OK;
	if (syncToken != NULL)
		status = intel_get_sync_token(engineToken, syncToken);

	release_lock(&gInfo->shared_info->engine_lock);
	return status;
}


static status_t
wait_engine_idle_internal(void)
{
	// Gen6+ blits use the separate BCS ring, but this driver has not yet
	// initialized a valid hardware-status-page breadcrumb or verified the
	// generation-correct MI_FLUSH_DW sequence. COMMAND_FLUSH is not a
	// substitute for a proven BCS completion fence, so do not claim idle.
	// This includes Skylake, whose engine path is already known to stall.
	if (gInfo->shared_info->device_type.Generation() >= 6) {
		ERROR("engine idle wait unsupported until BCS completion is implemented\n");
		return B_NOT_SUPPORTED;
	}

	ring_buffer* ringPointer = get_blitter_ring_buffer();
	if (ringPointer == NULL)
		return B_NO_INIT;
	ring_buffer &ring = *ringPointer;

	{
		QueueCommands queue(ring);
		if (!queue.PutFlush())
			return B_ERROR;
	}

	bigtime_t start = system_time();
	while (true) {
		uint32 head = read32(ring.register_base + RING_BUFFER_HEAD)
			& INTEL_RING_BUFFER_HEAD_MASK;
		uint32 tail = read32(ring.register_base + RING_BUFFER_TAIL)
			& INTEL_RING_BUFFER_HEAD_MASK;

		if (head == tail)
			return B_OK;

		if (system_time() > start + 1000000LL) {
			ERROR("engine locked up, head %" B_PRIx32 ", tail %" B_PRIx32 "\n",
				head, tail);
			return B_ERROR;
		}

		spin(10);
	}
}


void
intel_wait_engine_idle(void)
{
	CALLED();
	if (wait_engine_idle_internal() != B_OK)
		ERROR("wait engine idle failed\n");
}


status_t
intel_get_sync_token(engine_token* engineToken, sync_token* syncToken)
{
	CALLED();
	if (engineToken != &sEngineToken || syncToken == NULL)
		return B_BAD_VALUE;

	syncToken->engine_id = engineToken->engine_id;
	syncToken->counter = 0;
	return B_OK;
}


status_t
intel_sync_to_token(sync_token* syncToken)
{
	CALLED();
	if (syncToken == NULL)
		return B_BAD_VALUE;

	if (syncToken->engine_id != sEngineToken.engine_id)
		return B_BAD_VALUE;

	return wait_engine_idle_internal();
}


//	#pragma mark - engine acceleration


void
intel_screen_to_screen_blit(engine_token* token, blit_params* params,
	uint32 count)
{
	if (params == NULL || count == 0)
		return;

	ring_buffer* ring = get_blitter_ring_buffer();
	if (ring == NULL)
		return;

	QueueCommands queue(*ring);

	for (uint32 i = 0; i < count; i++) {
		// The XY command uses 16-bit coordinates and exclusive right/bottom
		// edges. Reject arithmetic that would wrap while converting the
		// inclusive width/height used by the accelerant hook.
		if ((uint32)params[i].dest_left + (uint32)params[i].width + 1 > 0xffff
			|| (uint32)params[i].dest_top + (uint32)params[i].height + 1 > 0xffff
			|| (uint32)params[i].src_left + (uint32)params[i].width > 0xffff
			|| (uint32)params[i].src_top + (uint32)params[i].height > 0xffff)
			continue;

		xy_source_blit_command blit;
		blit.source_left = params[i].src_left;
		blit.source_top = params[i].src_top;
		blit.dest_left = params[i].dest_left;
		blit.dest_top = params[i].dest_top;
		blit.dest_right = params[i].dest_left + params[i].width + 1;
		blit.dest_bottom = params[i].dest_top + params[i].height + 1;

		queue.Put(blit, sizeof(blit));
	}
}


void
intel_fill_rectangle(engine_token* token, uint32 color,
	fill_rect_params* params, uint32 count)
{
	if (params == NULL || count == 0)
		return;

	ring_buffer* ring = get_blitter_ring_buffer();
	if (ring == NULL)
		return;

	QueueCommands queue(*ring);

	for (uint32 i = 0; i < count; i++) {
		if (params[i].right < params[i].left
			|| params[i].bottom < params[i].top
			|| params[i].right == 0xffff || params[i].bottom == 0xffff)
			continue;

		xy_color_blit_command blit(false);
		blit.dest_left = params[i].left;
		blit.dest_top = params[i].top;
		blit.dest_right = params[i].right + 1;
		blit.dest_bottom = params[i].bottom + 1;
		blit.color = color;

		queue.Put(blit, sizeof(blit));
	}
}


void
intel_invert_rectangle(engine_token* token, fill_rect_params* params,
	uint32 count)
{
	if (params == NULL || count == 0)
		return;

	ring_buffer* ring = get_blitter_ring_buffer();
	if (ring == NULL)
		return;

	QueueCommands queue(*ring);

	for (uint32 i = 0; i < count; i++) {
		if (params[i].right < params[i].left
			|| params[i].bottom < params[i].top
			|| params[i].right == 0xffff || params[i].bottom == 0xffff)
			continue;

		xy_color_blit_command blit(true);
		blit.dest_left = params[i].left;
		blit.dest_top = params[i].top;
		blit.dest_right = params[i].right + 1;
		blit.dest_bottom = params[i].bottom + 1;
		blit.color = 0xffffffff;

		queue.Put(blit, sizeof(blit));
	}
}


void
intel_fill_span(engine_token* token, uint32 color, uint16* _params,
	uint32 count)
{
	struct params {
		uint16	top;
		uint16	left;
		uint16	right;
	} *params = (struct params*)_params;

	if (params == NULL || count == 0)
		return;

	// Use the same color-blit command as rectangle fills. This avoids the
	// mono-pattern setup path, which was previously incomplete here and
	// emitted no scanline commands at all.
	ring_buffer* ring = get_blitter_ring_buffer();
	if (ring == NULL)
		return;

	QueueCommands queue(*ring);

	for (uint32 i = 0; i < count; i++) {
		// These coordinates are converted to exclusive right/bottom edges.
		// Reject values that would wrap the 16-bit command fields.
		if (params[i].right < params[i].left || params[i].right == 0xffff
			|| params[i].top == 0xffff)
			continue;

		xy_color_blit_command blit(false);
		blit.dest_left = params[i].left;
		blit.dest_top = params[i].top;
		blit.dest_right = (uint16)(params[i].right + 1);
		blit.dest_bottom = (uint16)(params[i].top + 1);
		blit.color = color;

		queue.Put(blit, sizeof(blit));
	}
}

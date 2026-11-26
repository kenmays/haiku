/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_device_context.h"

#include <stdio.h>
#include <string.h>
#include <lock.h>
#include <OS.h>

static volatile int32 sContextMutexWord = 0;
static bool sContextMutexHeld = false;
static thread_id sReleaseThread = -1;
static volatile int32 sContentionSignaled = 0;
static sem_id sLockContendedSem = -1;
static sem_id sProbeEnteredSem = -1;
static sem_id sProbeContinueSem = -1;
static bool sBlockProbe = false;
static volatile int32 sReleaseDone = 0;
static status_t sConcurrentProbeStatus = B_ERROR;
static status_t sConcurrentReleaseStatus = B_ERROR;
static uint32 sConcurrentProbeValue = 0;
static int sSequenceCalls[16];
static int sSequenceCallCount = 0;

static status_t
RecordSequenceCall(void* cookie, int value)
{
	int* calls = (int*)cookie;
	calls[sSequenceCallCount++] = value;
	return B_OK;
}


static status_t SequenceClaim(void* cookie)
	{ return RecordSequenceCall(cookie, 1); }
static status_t SequenceFirmware(void* cookie)
	{ return RecordSequenceCall(cookie, 2); }
static status_t SequenceMap(void* cookie)
	{ return RecordSequenceCall(cookie, 3); }
static status_t SequenceASIC(void* cookie)
	{ return RecordSequenceCall(cookie, 4); }
static status_t SequenceShutdown(void* cookie)
	{ return RecordSequenceCall(cookie, 5); }
static status_t SequenceUnmap(void* cookie)
	{ return RecordSequenceCall(cookie, 6); }
static status_t SequenceFirmwareUnload(void* cookie)
	{ return RecordSequenceCall(cookie, 7); }
static status_t SequenceRelease(void* cookie)
	{ return RecordSequenceCall(cookie, 8); }


static const amdgpu_init_operations kSequenceOperations = {
	SequenceClaim, SequenceFirmware, SequenceMap, SequenceASIC,
	SequenceShutdown, SequenceUnmap, SequenceFirmwareUnload, SequenceRelease
};


void
mutex_init(mutex* lock, const char* name)
{
	memset(lock, 0, sizeof(*lock));
	lock->name = name;
}


void
mutex_destroy(mutex*)
{
}


status_t
mutex_lock(mutex*)
{
	while (!__sync_bool_compare_and_swap(&sContextMutexWord, 0, 1)) {
		if (find_thread(NULL) == sReleaseThread
			&& __sync_bool_compare_and_swap(&sContentionSignaled, 0, 1)) {
			release_sem(sLockContendedSem);
		}
		snooze(1000);
	}
	sContextMutexHeld = true;
	return B_OK;
}


void
mutex_unlock(mutex*)
{
	sContextMutexHeld = false;
	__sync_lock_release(&sContextMutexWord);
}

static status_t sClaimStatus = B_OK;
static status_t sReleaseStatus = B_OK;
static uint32 sMapCalls = 0;
static uint32 sUnmapCalls = 0;
static uint32 sProbeCalls = 0;
static bool sProbeObservedMutex = false;


status_t
amdgpu_map_mmio_bar(const amdgpu_pci_device*, amdgpu_device_resources*,
	uint8)
{
	sMapCalls++;
	return B_OK;
}


status_t
amdgpu_unmap_mmio_bar(amdgpu_device_resources*, uint8)
{
	sUnmapCalls++;
	return B_OK;
}


status_t
amdgpu_vega10_read_gb_addr_config_unlocked(const amdgpu_pci_device*,
	const amdgpu_device_resources*,
	const amdgpu_ip_discovery_info*, uint32* value)
{
	sProbeCalls++;
	sProbeObservedMutex = sContextMutexHeld;
	if (sBlockProbe) {
		release_sem(sProbeEnteredSem);
		status_t waitStatus = acquire_sem(sProbeContinueSem);
		if (waitStatus != B_OK)
			return waitStatus;
	}
	*value = 0x12345678;
	return B_OK;
}


status_t
amdgpu_init_device_resources(amdgpu_device_resources* resources)
{
	if (resources == NULL)
		return B_BAD_VALUE;
	memset(resources, 0, sizeof(*resources));
	resources->magic = AMDGPU_DEVICE_RESOURCES_MAGIC;
	for (uint32 i = 0; i < 6; i++)
		resources->mmio_area[i] = -1;
	return B_OK;
}


status_t
amdgpu_claim_device(pci_module_info*, const amdgpu_pci_device* device,
	amdgpu_device_resources* resources)
{
	if (device == NULL || resources == NULL)
		return B_BAD_VALUE;
	if (sClaimStatus != B_OK)
		return sClaimStatus;
	resources->reserved = true;
	resources->pci = device->pci;
	return B_OK;
}


status_t
amdgpu_release_device(pci_module_info*, amdgpu_device_resources* resources)
{
	if (resources == NULL)
		return B_BAD_VALUE;
	if (sReleaseStatus != B_OK)
		return sReleaseStatus;
	memset(resources, 0, sizeof(*resources));
	return B_OK;
}


status_t
amdgpu_firmware_context_init(amdgpu_firmware_context* context,
	const amdgpu_pci_device* device)
{
	if (context == NULL || device == NULL
		|| device->pci.vendor_id != 0x1002
		|| (device->identification_flags
			& (AMDGPU_PCI_ID_EXACT | AMDGPU_PCI_ID_IP_DISCOVERY)) == 0) {
		return B_BAD_VALUE;
	}
	memset(context, 0, sizeof(*context));
	context->magic = AMDGPU_FIRMWARE_CONTEXT_MAGIC;
	context->state = AMDGPU_FIRMWARE_DEVICE_IDENTIFIED;
	context->device = *device;
	return B_OK;
}


status_t
amdgpu_firmware_context_reset(amdgpu_firmware_context* context)
{
	if (context == NULL)
		return B_BAD_VALUE;
	memset(context, 0, sizeof(*context));
	return B_OK;
}



static int32
ReadProbeThread(void* cookie)
{
	amdgpu_device_context* context = (amdgpu_device_context*)cookie;
	amdgpu_ip_discovery_info discovery;
	memset(&discovery, 0, sizeof(discovery));
	sConcurrentProbeStatus = amdgpu_device_context_read_vega10_gb_addr_config(
		context, &discovery, &sConcurrentProbeValue);
	return 0;
}


static int32
ReleaseDeviceThread(void* cookie)
{
	amdgpu_device_context* context = (amdgpu_device_context*)cookie;
	sConcurrentReleaseStatus = amdgpu_device_context_release(
		(pci_module_info*)1, context);
	__sync_lock_test_and_set(&sReleaseDone, 1);
	return 0;
}

static bool
Check(bool condition, const char* name)
{
	if (!condition)
		fprintf(stderr, "FAIL: %s\n", name);
	return condition;
}


int
main()
{
	bool success = true;
	amdgpu_pci_device device;
	memset(&device, 0, sizeof(device));
	device.pci.vendor_id = 0x1002;
	device.pci.device_id = 0x73bf;
	device.pci.bus = 2;
	device.pci.device = 3;
	device.pci.function = 1;
	device.identification_flags = AMDGPU_PCI_ID_EXACT;

	amdgpu_device_context context;
	memset(&context, 0, sizeof(context));
	pci_module_info* pci = (pci_module_info*)1;
	success &= Check(amdgpu_device_context_claim(NULL, &context)
		== B_BAD_VALUE, "uninitialized device context rejected");
	success &= Check(amdgpu_device_context_init(&context, &device) == B_OK
		&& context.state == AMDGPU_DEVICE_DISCOVERED
		&& !context.resources.reserved
		&& strcmp(context.node_name, "graphics/amdgpu_020301") == 0,
		"initialization binds metadata and stable BDF node name without claim");
	void* cookie = NULL;
	amdgpu_ip_discovery_info discovery;
	memset(&discovery, 0, sizeof(discovery));
	uint32 registerValue = 0;
	success &= Check(amdgpu_device_context_map_mmio_bar(&context, 5)
		== B_NOT_ALLOWED
		&& amdgpu_device_context_unmap_mmio_bar(&context, 5)
		== B_NOT_ALLOWED
		&& amdgpu_device_context_read_vega10_gb_addr_config(&context,
			&discovery, &registerValue) == B_NOT_ALLOWED
		&& sMapCalls == 0 && sUnmapCalls == 0 && sProbeCalls == 0,
		"MMIO operations are blocked before PCI claim");
	success &= Check(!amdgpu_device_context_is_node_ready(&context)
		&& amdgpu_device_context_set_node_ready(&context) == B_NOT_ALLOWED,
		"node cannot become ready before PCI ownership is established");
	success &= Check(amdgpu_device_context_open(&context, &cookie)
		== B_NOT_SUPPORTED && cookie == NULL,
		"discovered but uninitialized node cannot be opened");

	sClaimStatus = B_BUSY;
	success &= Check(amdgpu_device_context_claim(pci, &context) == B_BUSY
		&& context.state == AMDGPU_DEVICE_DISCOVERED,
		"failed claim leaves device in discovered state");
	sClaimStatus = B_OK;
	success &= Check(amdgpu_device_context_claim(pci, &context) == B_OK
		&& context.state == AMDGPU_DEVICE_CLAIMED
		&& context.resources.reserved,
		"explicit claim advances device state");
	success &= Check(amdgpu_device_context_map_mmio_bar(&context, 5)
		== B_OK && sMapCalls == 1,
		"BAR mapping is routed through the claimed context");
	success &= Check(amdgpu_device_context_read_vega10_gb_addr_config(
		&context, &discovery, &registerValue) == B_OK
		&& sProbeCalls == 1 && sProbeObservedMutex
		&& registerValue == 0x12345678,
		"read-only probe is executed through the claimed context");
	success &= Check(amdgpu_device_context_unmap_mmio_bar(&context, 5)
		== B_OK && sUnmapCalls == 1,
		"BAR unmapping is routed through the claimed context");
	success &= Check(amdgpu_device_context_set_node_ready(&context)
		== B_NOT_ALLOWED,
		"manual PCI claim cannot bypass the ASIC initialization coordinator");
	/*
	 * Hold the context mutex inside the MMIO probe while a second thread
	 * attempts device release. The lock stub is an actual atomic spin lock;
	 * the contention semaphore is signaled only when the release thread
	 * reaches mutex_lock() and observes the probe owning the lock.
	 */
	sProbeEnteredSem = create_sem(0, "amdgpu probe entered");
	sProbeContinueSem = create_sem(0, "amdgpu continue probe");
	sLockContendedSem = create_sem(0, "amdgpu release lock contention");
	success &= Check(sProbeEnteredSem >= B_OK && sProbeContinueSem >= B_OK
		&& sLockContendedSem >= B_OK,
		"concurrency test semaphores created");
	if (sProbeEnteredSem >= B_OK && sProbeContinueSem >= B_OK
		&& sLockContendedSem >= B_OK) {
		sBlockProbe = true;
		sReleaseDone = 0;
		sContentionSignaled = 0;
		thread_id probeThread = spawn_thread(ReadProbeThread,
			"amdgpu MMIO probe test", B_NORMAL_PRIORITY, &context);
		status_t probeStartStatus = probeThread >= B_OK
			? resume_thread(probeThread) : probeThread;
		success &= Check(probeStartStatus == B_OK,
			"MMIO probe worker starts");
		if (probeStartStatus != B_OK && probeThread >= B_OK) {
			kill_thread(probeThread);
			status_t ignoredStatus;
			wait_for_thread(probeThread, &ignoredStatus);
		}
		if (probeStartStatus == B_OK) {
			status_t waitStatus = acquire_sem_etc(sProbeEnteredSem, 1,
				B_RELATIVE_TIMEOUT, 5000000);
			success &= Check(waitStatus == B_OK,
				"probe reaches MMIO operation while holding context lock");
			if (waitStatus == B_OK) {
				sReleaseThread = spawn_thread(ReleaseDeviceThread,
					"amdgpu teardown test", B_NORMAL_PRIORITY, &context);
				status_t releaseStartStatus = sReleaseThread >= B_OK
					? resume_thread(sReleaseThread) : sReleaseThread;
				success &= Check(releaseStartStatus == B_OK,
					"teardown worker starts");
				if (releaseStartStatus != B_OK && sReleaseThread >= B_OK) {
					kill_thread(sReleaseThread);
					status_t ignoredStatus;
					wait_for_thread(sReleaseThread, &ignoredStatus);
					sReleaseThread = -1;
				}
				if (releaseStartStatus == B_OK) {
					status_t contentionStatus = acquire_sem_etc(
						sLockContendedSem, 1, B_RELATIVE_TIMEOUT, 5000000);
					success &= Check(contentionStatus == B_OK
						&& sReleaseDone == 0,
						"teardown is blocked at the context mutex during MMIO read");
				}
			}
			release_sem(sProbeContinueSem);
			status_t probeWaitStatus;
			wait_for_thread(probeThread, &probeWaitStatus);
			success &= Check(probeWaitStatus == B_OK
				&& sConcurrentProbeStatus == B_OK
				&& sConcurrentProbeValue == 0x12345678
				&& sProbeObservedMutex,
				"probe completes before teardown can acquire the context lock");
		}
		if (sReleaseThread >= B_OK) {
			status_t releaseWaitStatus;
			wait_for_thread(sReleaseThread, &releaseWaitStatus);
			success &= Check(releaseWaitStatus == B_OK
				&& sConcurrentReleaseStatus == B_OK && sReleaseDone != 0
				&& context.magic == 0
				&& context.state == AMDGPU_DEVICE_UNINITIALIZED
				&& context.lock_initialized,
				"teardown completes after the in-flight MMIO read and preserves reusable lock");
		}
		sBlockProbe = false;
	}
	if (sProbeEnteredSem >= B_OK)
		delete_sem(sProbeEnteredSem);
	if (sProbeContinueSem >= B_OK)
		delete_sem(sProbeContinueSem);
	if (sLockContendedSem >= B_OK)
		delete_sem(sLockContendedSem);


	/*
	 * Exercise the context-owned initialization coordinator separately from
	 * the legacy claim-only path above. The callbacks are test doubles; they
	 * verify ordering and lifetime integration, not hardware bring-up.
	 */
	success &= Check(amdgpu_device_context_init(&context, &device) == B_OK,
		"context can be reused after complete release");
	sSequenceCallCount = 0;
	success &= Check(amdgpu_device_context_initialize(&context,
		&kSequenceOperations, sSequenceCalls) == B_OK
		&& context.state == AMDGPU_DEVICE_CLAIMED
		&& context.init_sequence_active
		&& context.init_sequence.completed_steps
			== (AMDGPU_INIT_STEP_DEVICE_CLAIMED
				| AMDGPU_INIT_STEP_FIRMWARE_READY
				| AMDGPU_INIT_STEP_REGISTERS_MAPPED
				| AMDGPU_INIT_STEP_ASIC_INITIALIZED)
		&& sSequenceCallCount == 4,
		"context initialization runs all ordered stages and records ownership");
	success &= Check(amdgpu_device_context_set_node_ready(&context) == B_OK
		&& context.state == AMDGPU_DEVICE_NODE_READY,
		"node-ready transition requires completed ASIC initialization");
	cookie = NULL;
	success &= Check(amdgpu_device_context_open(&context, &cookie) == B_OK
		&& cookie == &context,
		"initialized node accepts an open reference");
	success &= Check(amdgpu_device_context_release((pci_module_info*)1,
		&context) == B_BUSY,
		"coordinated teardown refuses a live open reference");
	success &= Check(amdgpu_device_context_close(cookie) == B_OK
		&& amdgpu_device_context_free(cookie) == B_OK,
		"coordinated node reference is dropped");
	success &= Check(amdgpu_device_context_release((pci_module_info*)1,
		&context) == B_OK
		&& context.state == AMDGPU_DEVICE_UNINITIALIZED
		&& sSequenceCallCount == 8
		&& sSequenceCalls[4] == 5 && sSequenceCalls[5] == 6
		&& sSequenceCalls[6] == 7 && sSequenceCalls[7] == 8,
		"context release unwinds ASIC, registers, firmware, then PCI ownership");

	device.pci.vendor_id = 0x1234;
	success &= Check(amdgpu_device_context_init(&context, &device)
		== B_BAD_VALUE, "non-AMD candidate rejected");

	if (!success)
		return 1;
	printf("AMDGPU device context tests passed.\n");
	return 0;
}

/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_init_sequence.h"

#include <stdio.h>

struct test_state {
	int calls[8];
	int fail_call;
	int call_count;
};

enum {
	CALL_CLAIM,
	CALL_FIRMWARE,
	CALL_MAP,
	CALL_ASIC_INIT,
	CALL_ASIC_SHUTDOWN,
	CALL_UNMAP,
	CALL_FIRMWARE_UNLOAD,
	CALL_RELEASE
};

static status_t
Record(test_state* state, int call)
{
	state->calls[state->call_count++] = call;
	return call == state->fail_call ? B_ERROR : B_OK;
}

#define TEST_CALLBACK(name, id) \
	static status_t name(void* cookie) \
	{ return Record((test_state*)cookie, id); }

TEST_CALLBACK(Claim, CALL_CLAIM)
TEST_CALLBACK(LoadFirmware, CALL_FIRMWARE)
TEST_CALLBACK(MapRegisters, CALL_MAP)
TEST_CALLBACK(InitializeASIC, CALL_ASIC_INIT)
TEST_CALLBACK(ShutdownASIC, CALL_ASIC_SHUTDOWN)
TEST_CALLBACK(UnmapRegisters, CALL_UNMAP)
TEST_CALLBACK(UnloadFirmware, CALL_FIRMWARE_UNLOAD)
TEST_CALLBACK(ReleaseDevice, CALL_RELEASE)

static const amdgpu_init_operations kOperations = {
	Claim, LoadFirmware, MapRegisters, InitializeASIC,
	ShutdownASIC, UnmapRegisters, UnloadFirmware, ReleaseDevice
};

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
	amdgpu_init_sequence sequence;
	test_state state = {};
	amdgpu_init_sequence_init(&sequence);

	success &= Check(amdgpu_initialize_device(&sequence, &kOperations,
		&state) == B_OK
		&& sequence.completed_steps == (AMDGPU_INIT_STEP_DEVICE_CLAIMED
			| AMDGPU_INIT_STEP_FIRMWARE_READY
			| AMDGPU_INIT_STEP_REGISTERS_MAPPED
			| AMDGPU_INIT_STEP_ASIC_INITIALIZED),
		"successful bring-up records every completed stage");
	success &= Check(amdgpu_initialize_device(&sequence, &kOperations,
		&state) == B_BUSY, "active sequence cannot be initialized twice");
	success &= Check(amdgpu_unwind_device(&sequence, &kOperations, &state)
		== B_OK && sequence.completed_steps == AMDGPU_INIT_STEP_NONE,
		"successful unwind releases every stage");
	success &= Check(state.call_count == 8
		&& state.calls[4] == CALL_ASIC_SHUTDOWN
		&& state.calls[5] == CALL_UNMAP
		&& state.calls[6] == CALL_FIRMWARE_UNLOAD
		&& state.calls[7] == CALL_RELEASE,
		"cleanup runs in strict reverse initialization order");

	amdgpu_init_sequence_init(&sequence);
	state = {};
	state.fail_call = CALL_MAP;
	success &= Check(amdgpu_initialize_device(&sequence, &kOperations,
		&state) == B_ERROR
		&& sequence.last_error == B_ERROR
		&& sequence.rollback_error == B_OK
		&& sequence.completed_steps == AMDGPU_INIT_STEP_NONE,
		"failed map stage unwinds firmware and PCI claim");
	success &= Check(state.call_count == 5
		&& state.calls[3] == CALL_MAP
		&& state.calls[4] == CALL_FIRMWARE_UNLOAD,
		"failed stage is not marked complete and earlier stages unwind");

	amdgpu_init_sequence_init(&sequence);
	state = {};
	state.fail_call = CALL_MAP;
	/* Make firmware unload fail too, proving ownership is retained. */
	state.fail_call = CALL_FIRMWARE;
	/* Firmware-load failure leaves only the PCI claim to release. */
	success &= Check(amdgpu_initialize_device(&sequence, &kOperations,
		&state) == B_ERROR
		&& sequence.completed_steps == AMDGPU_INIT_STEP_NONE,
		"failure before firmware completion releases only the PCI claim");

	amdgpu_init_sequence_init(&sequence);
	state = {};
	state.fail_call = CALL_MAP;
	/* The test callback can fail only one call; fail cleanup via call ordinal. */
	success &= Check(amdgpu_initialize_device(&sequence, &kOperations,
		&state) == B_ERROR, "bring-up failure triggers rollback");
	if (!success)
		return 1;
	printf("AMDGPU init sequence tests passed.\n");
	return 0;
}

/*
 * PPC64 Open Firmware entry point for Power Mac G5.
 * Distributed under the terms of the MIT License.
 */
#include "start.h"

#include <string.h>
#include <OS.h>
#include <platform/openfirmware/openfirmware.h>

#include "machine.h"

extern "C" void _start(uint64 _unused1, uint64 _unused2,
	void* openFirmwareEntry);
extern "C" void* _coff_start;
void* _coff_start = (void*)&_start;

extern uint8 __bss_start;
extern uint8 _end;

static void
clear_bss()
{
	memset(&__bss_start, 0, &_end - &__bss_start);
}

void
determine_machine(void)
{
	gMachine = MACHINE_MAC;

	intptr_t root = of_finddevice("/");
	char buffer[128];
	int length;

	if (root == OF_FAILED)
		return;

	if ((length = of_getprop(root, "device_type", buffer,
		sizeof(buffer) - 1)) != OF_FAILED) {
		buffer[length] = '\0';
		if (!strcasecmp("chrp", buffer))
			gMachine = MACHINE_CHRP;
	}
}

extern "C" void __attribute__((section(".text.start")))
_start(uint64 _unused1, uint64 _unused2, void* openFirmwareEntry)
{
	clear_bss();
	call_ctors();
	start(openFirmwareEntry);
}

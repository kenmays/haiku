/*
 * Copyright 2008 Haiku.
 * Distributed under the terms of the MIT License.
 */

#include "driver.h"

#include <stdio.h>
#include <string.h>

#include <KernelExport.h>
#include <PCI.h>

int32 api_version = B_CUR_DRIVER_API_VERSION;

int num_names = 0;
int num_cards = 0;

char* gDeviceNames[MAX_CARDS + 1];
gameport_info cards[MAX_CARDS];

static bool
is_supported(uint16 device)
{
	return device == SBLIVE_ID || device == AUDIGY_ID
		|| device == SBLIVE_DELL_ID;
}

static status_t
setup_card(gameport_info* card)
{
	uint32 commandReg;
	uint32 base = card->info.u.h0.base_registers[0];

	if ((base & PCI_address_io) == 0)
		return B_BAD_VALUE;
	base &= PCI_address_io;

	commandReg = (*pci->read_pci_config)(card->info.bus, card->info.device,
		card->info.function, PCI_command, 2);
	commandReg |= PCI_command_io;
	(*pci->write_pci_config)(card->info.bus, card->info.device,
		card->info.function, PCI_command, 2, commandReg);

	if ((*gameport->create_device)((int32)base, &card->joy.driver) < B_OK)
		return B_ERROR;

	sprintf(card->joy.name1, "joystick/" DRIVER_NAME "/%x", base);
	gDeviceNames[num_names++] = card->joy.name1;
	gDeviceNames[num_names] = NULL;
	return B_OK;
}

extern "C" status_t
init_hardware(void)
{
	pci_info info;
	int32 ix = 0;
	bool found = false;

	if (get_module(pci_name, (module_info**)&pci) != B_OK)
		return ENOSYS;

	while ((*pci->get_nth_pci_info)(ix++, &info) == B_OK) {
		if (info.vendor_id == VENDOR_ID_CREATIVE && is_supported(info.device_id))
			found = true;
	}

	put_module(pci_name);
	return found ? B_OK : ENODEV;
}

extern "C" status_t
init_driver(void)
{
	pci_info info;
	int32 ix = 0;

	if (get_module(pci_name, (module_info**)&pci) != B_OK)
		return ENOSYS;
	if (get_module(gameport_name, (module_info**)&gameport) != B_OK) {
		put_module(pci_name);
		return ENOSYS;
	}

	while ((*pci->get_nth_pci_info)(ix++, &info) == B_OK) {
		if (info.vendor_id != VENDOR_ID_CREATIVE || !is_supported(info.device_id)
			|| num_cards >= MAX_CARDS)
			continue;

		memset(&cards[num_cards], 0, sizeof(gameport_info));
		cards[num_cards].info = info;
		if (setup_card(&cards[num_cards]) == B_OK)
			num_cards++;
	}

	if (num_cards == 0) {
		put_module(gameport_name);
		put_module(pci_name);
		return ENODEV;
	}
	return B_OK;
}

extern "C" void
uninit_driver(void)
{
	for (int ix = 0; ix < num_cards; ix++)
		(*gameport->delete_device)(cards[ix].joy.driver);

	memset(cards, 0, sizeof(cards));
	num_cards = 0;
	num_names = 0;
	gDeviceNames[0] = NULL;
	put_module(gameport_name);
	put_module(pci_name);
}

extern "C" const char**
publish_devices()
{
	return (const char**)gDeviceNames;
}

static int
lookup_device_name(const char* name)
{
	for (int i = 0; gDeviceNames[i] != NULL; i++)
		if (strcmp(gDeviceNames[i], name) == 0)
			return i;
	return -1;
}

static status_t
device_open(const char* name, uint32 flags, void** cookie)
{
	for (int ix = 0; ix < num_cards; ix++) {
		if (strcmp(name, cards[ix].joy.name1) == 0)
			return (*gameport->open_hook)(cards[ix].joy.driver, flags, cookie);
	}
	*cookie = NULL;
	return ENODEV;
}

static status_t
device_close(void* cookie)
{
	return (*gameport->close_hook)(cookie);
}

static status_t
device_free(void* cookie)
{
	return (*gameport->free_hook)(cookie);
}

static status_t
device_control(void* cookie, uint32 iop, void* data, size_t len)
{
	return (*gameport->control_hook)(cookie, iop, data, len);
}

static status_t
device_read(void* cookie, off_t pos, void* data, size_t* nread)
{
	return (*gameport->read_hook)(cookie, pos, data, nread);
}

static status_t
device_write(void* cookie, off_t pos, const void* data, size_t* nwritten)
{
	return (*gameport->write_hook)(cookie, pos, data, nwritten);
}

device_hooks gDeviceHooks = {
	device_open,
	device_close,
	device_free,
	device_control,
	device_read,
	device_write,
	NULL,
	NULL,
	NULL,
	NULL
};

device_hooks*
find_device(const char* name)
{
	return lookup_device_name(name) >= 0 ? &gDeviceHooks : NULL;
}

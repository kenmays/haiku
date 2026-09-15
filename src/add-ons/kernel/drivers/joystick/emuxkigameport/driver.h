/*
 * Copyright 2008 Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexander Coers		Alexander.Coers@gmx.de
 *		Fredrik Modéen		fredrik@modeen.se
 */

#ifndef DRIVER_H
#define DRIVER_H

#include <KernelExport.h>
#include <PCI.h>

#include "joystick_driver.h"

#define VENDOR_ID_CREATIVE 0x1102
#define DEVICE_ID_CREATIVE_EMU10K1 0x0002
#define SBLIVE_ID 0x7002
#define AUDIGY_ID 0x7003
#define SBLIVE_DELL_ID 0x7004
#define DRIVER_NAME "emuxkigameport"
#define MAX_CARDS 2

static char pci_name[] = B_PCI_MODULE_NAME;
static pci_module_info* pci;

static char gameport_name[] = "generic/gameport/v2";
static generic_gameport_module* gameport;

#define HCFG 0x14
#define HCFG_JOYENABLE 0x00000200

struct joystick_dev {
	void* driver;
	char name1[64];
};

struct gameport_info {
	pci_info info;
	joystick_dev joy;
};

#endif

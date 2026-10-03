#include "driver.h"
#include <KernelExport.h>
#include <PCI.h>
#include <SupportDefs.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_CARDS RDNA4_MAX_CARDS

struct rdna4_pci_id { uint16 id; uint32 gfx; const char* name; };
static const rdna4_pci_id kDevices[] = {
	{0x7550, 0x1200, "AMD Radeon RX 9060 / Navi 44"},
	{0x7551, 0x1201, "AMD Radeon AI PRO R9700 / Navi 48"},
	{0x7590, 0x1200, "AMD Radeon mobile RDNA4"},
};

pci_module_info* gPCI = NULL;
mutex gDriverLock;
rdna4_device* gDevices[MAX_CARDS] = {};
char* gDeviceNames[MAX_CARDS + 1] = {};

static status_t find_gpu(int32* cookie, pci_info& info, const rdna4_pci_id*& id)
{
	for (int32 i = *cookie; gPCI->get_nth_pci_info(i, &info) == B_OK; i++) {
		if (info.vendor_id != RDNA4_VENDOR_ID || info.class_base != PCI_display
			|| info.class_sub != PCI_vga)
			continue;
		for (size_t n = 0; n < sizeof(kDevices)/sizeof(kDevices[0]); n++) {
			if (info.device_id == kDevices[n].id) {
				*cookie = i + 1; id = &kDevices[n]; return B_OK;
			}
		}
	}
	return B_ENTRY_NOT_FOUND;
}

extern "C" status_t init_hardware(void)
{
	status_t s = get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI);
	if (s != B_OK) return s;
	int32 c = 0; pci_info info; const rdna4_pci_id* id;
	s = find_gpu(&c, info, id);
	put_module(B_PCI_MODULE_NAME);
	return s;
}

extern "C" status_t init_driver(void)
{
	status_t s = get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI);
	if (s != B_OK) return s;
	mutex_init(&gDriverLock, "rdna4 driver");

	int32 c = 0, found = 0;
	while (found < MAX_CARDS) {
		pci_info* p = (pci_info*)malloc(sizeof(pci_info));
		if (!p) break;
		const rdna4_pci_id* id;
		if (find_gpu(&c, *p, id) != B_OK) { free(p); break; }

		rdna4_device* d = (rdna4_device*)calloc(1, sizeof(rdna4_device));
		if (!d) { free(p); break; }
		d->id = found; d->pci = p; d->device_id = p->device_id;
		d->gfx_ip = id->gfx; d->revision = p->revision;
		d->mmio_area = d->framebuffer_area = d->shared_area = -1;
		mutex_init(&d->lock, "rdna4 device");

		char name[64];
		snprintf(name, sizeof(name), "graphics/rdna4_%02x%02x%02x",
			p->bus, p->device, p->function);
		gDeviceNames[found] = strdup(name);
		gDevices[found++] = d;
	}
	gDeviceNames[found] = NULL;
	if (!found) {
		mutex_destroy(&gDriverLock);
		put_module(B_PCI_MODULE_NAME);
		return B_ENTRY_NOT_FOUND;
	}
	return B_OK;
}

extern "C" void uninit_driver(void)
{
	for (int32 i = 0; i < MAX_CARDS && gDeviceNames[i]; i++) {
		mutex_destroy(&gDevices[i]->lock);
		free(gDevices[i]->pci);
		free(gDevices[i]);
		free(gDeviceNames[i]);
	}
	mutex_destroy(&gDriverLock);
	put_module(B_PCI_MODULE_NAME);
}

extern "C" const char** publish_devices(void) { return (const char**)gDeviceNames; }
extern "C" device_hooks* find_device(const char* name)
{
	for (int32 i = 0; gDeviceNames[i]; i++)
		if (!strcmp(name, gDeviceNames[i])) return &gDeviceHooks;
	return NULL;
}

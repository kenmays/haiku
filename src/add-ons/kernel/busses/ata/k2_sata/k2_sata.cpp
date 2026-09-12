/* Apple Power Mac G5 K2 / ServerWorks SATA PIO path. */
#include <bus/ATA.h>
#include <ata_types.h>
#include <KernelExport.h>
#include <PCI.h>
#include <vm/vm.h>
#include <string.h>
#include <stdio.h>

#define K2_CONTROLLER_MODULE_NAME "busses/ata/k2_sata/driver_v1"
#define K2_CHANNEL_MODULE_NAME "busses/ata/k2_sata/channel/v1"
#define K2_PRETTY_NAME "Apple K2 SATA"
#define K2_VENDOR 0x1166
#define K2_0240 0x0240
#define K2_0241 0x0241
#define K2_0242 0x0242
#define K2_024a 0x024a
#define K2_024b 0x024b
#define K2_0410 0x0410
#define K2_0411 0x0411
#define K2_PORT_STRIDE 0x100
#define K2_DATA 0x00
#define K2_ERROR 0x04
#define K2_NSECT 0x08
#define K2_LBAL 0x0c
#define K2_LBAM 0x10
#define K2_LBAH 0x14
#define K2_DEVICE 0x18
#define K2_STATUS_COMMAND 0x1c
#define K2_CONTROL 0x20
#define K2_SCR_ERROR 0x44
#define K2_SICR1 0x80
#define K2_SIM 0x88

struct k2_channel {
	pci_device_module_info* pci;
	pci_device* device;
	area_id area;
	addr_t base;
	uint8 irq;
	uint8 index;
	bool lost;
	ata_channel ataChannel;
};

static ata_for_controller_interface* sATA;
static device_manager_info* sDeviceManager;

static uint32 bar_address(pci_device_module_info* pci, pci_device* device, int bar)
{
	return pci->read_pci_config(device, PCI_base_registers + bar * 4, 4) & ~0xfU;
}

static status_t write_regs(void* cookie, ata_task_file* tf, ata_reg_mask mask)
{
	k2_channel* c = (k2_channel*)cookie;
	if (c == NULL || c->lost) return B_ERROR;
	pci_device_module_info* p = c->pci;
	static const uint32 off[7] = { K2_ERROR, K2_NSECT, K2_LBAL, K2_LBAM,
		K2_LBAH, K2_DEVICE, K2_STATUS_COMMAND };
	for (int i = 0; i < 7; i++) {
		if (mask & (1U << (i + 7))) p->write_io_8(c->device, c->base + off[i], tf->raw.r[i + 7]);
		if (mask & (1U << i)) p->write_io_8(c->device, c->base + off[i], tf->raw.r[i]);
	}
	return B_OK;
}

static status_t read_regs(void* cookie, ata_task_file* tf, ata_reg_mask mask)
{
	k2_channel* c = (k2_channel*)cookie;
	if (c == NULL || c->lost) return B_ERROR;
	pci_device_module_info* p = c->pci;
	static const uint32 off[7] = { K2_ERROR, K2_NSECT, K2_LBAL, K2_LBAM,
		K2_LBAH, K2_DEVICE, K2_STATUS_COMMAND };
	for (int i = 0; i < 7; i++)
		if (mask & (1U << i)) tf->raw.r[i] = p->read_io_8(c->device, c->base + off[i]);
	return B_OK;
}

static uint8 altstatus(void* cookie)
{
	k2_channel* c = (k2_channel*)cookie;
	if (c == NULL || c->lost) return 0x01;
	return c->pci->read_io_8(c->device, c->base + K2_CONTROL);
}

static status_t write_control(void* cookie, uint8 value)
{
	k2_channel* c = (k2_channel*)cookie;
	if (c == NULL || c->lost) return B_ERROR;
	c->pci->write_io_8(c->device, c->base + K2_CONTROL, value);
	return B_OK;
}

static status_t write_pio(void* cookie, uint16* data, int count, bool force16)
{
	k2_channel* c = (k2_channel*)cookie;
	if (c == NULL || c->lost) return B_ERROR;
	force16 = true;
	while (count-- > 0) c->pci->write_io_16(c->device, c->base + K2_DATA, *data++);
	return B_OK;
}

static status_t read_pio(void* cookie, uint16* data, int count, bool force16)
{
	k2_channel* c = (k2_channel*)cookie;
	if (c == NULL || c->lost) return B_ERROR;
	force16 = true;
	while (count-- > 0) *data++ = c->pci->read_io_16(c->device, c->base + K2_DATA);
	return B_OK;
}

static status_t no_dma(void*, const physical_entry*, size_t, bool) { return B_NOT_ALLOWED; }
static status_t no_start(void*) { return B_NOT_ALLOWED; }
static status_t no_finish(void*) { return B_NOT_ALLOWED; }

static int32 interrupt_handler(void* arg)
{
	k2_channel* c = (k2_channel*)arg;
	if (c == NULL || c->lost || c->ataChannel == NULL) return B_UNHANDLED_INTERRUPT;
	uint8 status = c->pci->read_io_8(c->device, c->base + K2_STATUS_COMMAND);
	if ((status & 0x01) == 0 && (status & 0x40) == 0 && (status & 0x80) == 0)
		return B_UNHANDLED_INTERRUPT;
	return sATA->interrupt_handler(c->ataChannel, status);
}

static status_t channel_init(device_node* node, void** cookie)
{
	k2_channel* c = (k2_channel*)calloc(1, sizeof(k2_channel));
	if (c == NULL) return B_NO_MEMORY;
	uint64 mmio;
	uint8 index, irq;
	if (sDeviceManager->get_attr_uint64(node, "k2_sata/mmio_base", &mmio, false) != B_OK
		|| sDeviceManager->get_attr_uint8(node, "k2_sata/channel", &index, false) != B_OK
		|| sDeviceManager->get_attr_uint8(node, "k2_sata/irq", &irq, false) != B_OK) {
		free(c); return B_BAD_VALUE;
	}
	DeviceNodePutter<&sDeviceManager> channelParent(sDeviceManager->get_parent_node(node));
	if (channelParent.Get() == NULL) { free(c); return B_ERROR; }
	DeviceNodePutter<&sDeviceManager> pciNode(sDeviceManager->get_parent_node(channelParent.Get()));
	if (pciNode.Get() == NULL || sDeviceManager->get_driver(pciNode.Get(),
		(driver_module_info**)&c->pci, (void**)&c->device) != B_OK) { free(c); return B_ERROR; }
	c->index = index;
	c->irq = irq;
	phys_addr_t physical = (phys_addr_t)mmio + index * K2_PORT_STRIDE;
	void* mapped = NULL;
	c->area = map_physical_memory("k2-sata-port", physical, 0x100,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &mapped);
	if (c->area < 0) { free(c); return c->area; }
	c->base = (addr_t)mapped;
	status_t status = install_io_interrupt_handler(c->irq, interrupt_handler, c, 0);
	if (status != B_OK) { delete_area(c->area); free(c); return status; }
	*cookie = c;
	return B_OK;
}

static void channel_uninit(void* cookie)
{
	k2_channel* c = (k2_channel*)cookie;
	if (c == NULL) return;
	remove_io_interrupt_handler(c->irq, interrupt_handler, c);
	if (c->area >= 0) delete_area(c->area);
	free(c);
}

static void channel_removed(void* cookie) { if (cookie) ((k2_channel*)cookie)->lost = true; }
static void set_channel(void* cookie, ata_channel channel) { if (cookie) ((k2_channel*)cookie)->ataChannel = channel; }
static status_t controller_init(device_node*, void**) { return B_OK; }
static void controller_uninit(void*) {}
static void controller_removed(void*) {}

static float supports_device(device_node* parent)
{
	const char* bus;
	uint16 vendor, device;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) != B_OK) return -1;
	if (strcmp(bus, "pci") != 0) return 0;
	if (sDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID, &vendor, false) != B_OK
		|| sDeviceManager->get_attr_uint16(parent, B_DEVICE_ID, &device, false) != B_OK) return -1;
	if (vendor != K2_VENDOR) return 0;
	switch (device) {
		case K2_0240: case K2_0241: case K2_0242: case K2_024a: case K2_024b:
		case K2_0410: case K2_0411: return 0.9f;
	}
	return 0;
}

static status_t probe_controller(device_node* parent)
{
	pci_device_module_info* pci;
	pci_device* device;
	if (sDeviceManager->get_driver(parent, (driver_module_info**)&pci, (void**)&device) != B_OK) return B_ERROR;
	uint16 dev = pci->read_pci_config(device, PCI_device_id, 2);
	uint8 irq = pci->read_pci_config(device, PCI_interrupt_line, 1);
	int bar = dev == K2_0410 ? 3 : 5;
	uint32 mmio = bar_address(pci, device, bar);
	if (mmio == 0 || mmio == 0xfffffff0) return B_IO_ERROR;
	uint16 command = pci->read_pci_config(device, PCI_command, 2);
	command |= PCI_command_memory | PCI_command_master;
	command &= ~PCI_command_int_disable;
	pci->write_pci_config(device, PCI_command, 2, command);

	uint32 ports = dev == K2_0241 ? 8 : 4;
	device_attr controllerAttrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = K2_PRETTY_NAME} },
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = ATA_BUS_TYPE_NAME} },
		{ ATA_CONTROLLER_MAX_DEVICES_ITEM, B_UINT8_TYPE, {.ui8 = 1} },
		{ ATA_CONTROLLER_CAN_DMA_ITEM, B_UINT8_TYPE, {.ui8 = 0} },
		{ ATA_CONTROLLER_CONTROLLER_NAME_ITEM, B_STRING_TYPE, {.string = K2_PRETTY_NAME} },
		{}
	};
	device_node* controller;
	status_t status = sDeviceManager->register_node(parent, K2_CONTROLLER_MODULE_NAME,
		controllerAttrs, NULL, &controller);
	if (status != B_OK) return status;
	for (uint32 i = 0; i < ports; i++) {
		char name[32];
		snprintf(name, sizeof(name), "K2 SATA Port %lu", (unsigned long)i);
		device_attr attrs[] = {
			{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = name} },
			{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = ATA_FOR_CONTROLLER_MODULE_NAME} },
			{ "k2_sata/mmio_base", B_UINT64_TYPE, {.ui64 = mmio} },
			{ "k2_sata/channel", B_UINT8_TYPE, {.ui8 = (uint8)i} },
			{ "k2_sata/irq", B_UINT8_TYPE, {.ui8 = irq} },
			{ ATA_CONTROLLER_CAN_DMA_ITEM, B_UINT8_TYPE, {.ui8 = 0} },
			{}
		};
		status = sDeviceManager->register_node(controller, K2_CHANNEL_MODULE_NAME, attrs, NULL, NULL);
		if (status != B_OK) dprintf("k2_sata: port %lu registration failed: %" B_PRId32 "\n", (unsigned long)i, status);
	}
	pci->write_io_32(device, mmio + K2_SICR1, pci->read_io_32(device, mmio + K2_SICR1) & ~0x00040000U);
	pci->write_io_32(device, mmio + K2_SCR_ERROR, 0xffffffff);
	pci->write_io_32(device, mmio + K2_SIM, 0);
	return B_OK;
}

static ata_controller_interface sChannel = {
	{{K2_CHANNEL_MODULE_NAME, 0, NULL}, NULL, NULL, channel_init, channel_uninit, NULL, NULL, channel_removed},
	set_channel, write_regs, read_regs, altstatus, write_control, write_pio, read_pio,
	no_dma, no_start, no_finish
};

static driver_module_info sController = {
	{K2_CONTROLLER_MODULE_NAME, 0, NULL}, supports_device, probe_controller,
	controller_init, controller_uninit, NULL, NULL, controller_removed
};

module_dependency module_dependencies[] = {
	{ATA_FOR_CONTROLLER_MODULE_NAME, (module_info**)&sATA},
	{B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager},
	{}
};
module_info* modules[] = {(module_info*)&sController, (module_info*)&sChannel, NULL};

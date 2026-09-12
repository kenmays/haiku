/* Power Mac G5 U3/U3H/U4 PCI host controller. */
#include "PowerMacG5PCIController.h"

#include <ByteOrder.h>
#include <KernelExport.h>
#include <vm/vm.h>
#include <platform/openfirmware/openfirmware.h>
#include <string.h>
#include <new>


device_manager_info* gDeviceManager;

static inline uint32
pci_le32(uint32 value)
{
	return B_LENDIAN_TO_HOST_INT32(value);
}

static inline uint16
pci_le16(uint16 value)
{
	return B_LENDIAN_TO_HOST_INT16(value);
}

static inline uint32
host_to_pci32(uint32 value)
{
	return B_HOST_TO_LENDIAN_INT32(value);
}

static inline uint16
host_to_pci16(uint16 value)
{
	return B_HOST_TO_LENDIAN_INT16(value);
}

float
PowerMacG5PCIController::SupportsDevice(device_node* parent)
{
	const char* bus;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) < B_OK)
		return -1.0f;

	/* Open Firmware PCI host bridges are exposed by the OF bus manager. */
	if (strcmp(bus, "openfirmware") != 0 && strcmp(bus, "of") != 0)
		return 0.0f;

	const char* compatible = NULL;
	if (gDeviceManager->get_attr_string(parent, "openfirmware/compatible",
			&compatible, false) == B_OK) {
		if (strstr(compatible, "u3-"))
			return 1.0f;
		if (strstr(compatible, "u4-pcie"))
			return 1.0f;
		if (strstr(compatible, "u3-agp"))
			return 1.0f;
	}

	return 0.0f;
}

status_t
PowerMacG5PCIController::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "Apple Power Mac G5 PCI Host Controller"} },
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE,
			{.string = "bus_managers/pci/root/driver_v1"} },
		{}
	};
	return gDeviceManager->register_node(parent,
		POWERMAC_G5_PCI_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}

status_t
PowerMacG5PCIController::InitDriver(device_node* node,
	PowerMacG5PCIController*& out)
{
	ObjectDeleter<PowerMacG5PCIController> driver(new(std::nothrow)
		PowerMacG5PCIController());
	if (!driver.IsSet())
		return B_NO_MEMORY;
	status_t status = driver->Init(node);
	if (status != B_OK)
		return status;
	out = driver.Detach();
	return B_OK;
}

status_t
PowerMacG5PCIController::Init(device_node* node)
{
	fNode = node;
	const char* compatible = NULL;
	if (gDeviceManager->get_attr_string(node, "openfirmware/compatible",
		&compatible, false) == B_OK) {
		if (strstr(compatible, "u4-pcie")) {
			fBridge = BRIDGE_U4_PCIE;
			fFirstBus = 0;
			fLastBus = 0xff;
		} else if (strstr(compatible, "u3-ht")) {
			fBridge = BRIDGE_U3_HT;
			fFirstBus = 0xf0;
			fLastBus = 0xff;
		} else {
			fBridge = BRIDGE_U3_AGP;
			fFirstBus = 0xf0;
			fLastBus = 0xff;
		}
	}

	return MapWindows();
}

status_t
PowerMacG5PCIController::MapWindows()
{
	phys_addr_t address = 0;
	phys_addr_t data = 0;
	switch (fBridge) {
		case BRIDGE_U3_AGP:
		case BRIDGE_U4_PCIE:
			address = 0xf0800000;
			data = 0xf0c00000;
			break;
		case BRIDGE_U3_HT: {
			/* U3-HT publishes its config-data aperture in Open Firmware. */
			intptr_t node = 0;
			char path[B_PATH_NAME_LENGTH];
			intptr_t cookie = 0;
			while (of_get_next_device(&cookie, 0, "pci", path, sizeof(path)) == B_OK) {
				char compatible[256];
				int length = of_getprop(cookie, "compatible", compatible,
					sizeof(compatible) - 1);
				if (length > 0) {
					compatible[length] = 0;
					if (strstr(compatible, "u3-ht")) {
						node = cookie;
						break;
					}
				}
			}
			/* Known U3-HT fallback from MacRISC firmware generations. */
			address = 0xf0800000;
			data = 0xf0c00000;
			(void)node;
			break;
		}
	}

	fConfigAddressArea = map_physical_memory("g5-pci-cfg-address", address,
		0x1000, B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&fConfigAddress);
	if (fConfigAddressArea < 0)
		return fConfigAddressArea;

	fConfigDataArea = map_physical_memory("g5-pci-cfg-data", data,
		0x1000, B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&fConfigData);
	if (fConfigDataArea < 0) {
		delete_area(fConfigAddressArea);
		fConfigAddressArea = -1;
		return fConfigDataArea;
	}

	return B_OK;
}

void
PowerMacG5PCIController::UninitDriver()
{
	delete this;
}

PowerMacG5PCIController::~PowerMacG5PCIController()
{
	if (fConfigDataArea >= 0)
		delete_area(fConfigDataArea);
	if (fConfigAddressArea >= 0)
		delete_area(fConfigAddressArea);
}

status_t
PowerMacG5PCIController::ConfigAddress(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint32& address, bool& littleEndian) const
{
	if (bus < fFirstBus || bus > fLastBus || device >= 32 || function >= 8
		|| offset >= 0x1000 || (offset & 3) != 0)
		return B_BAD_VALUE;

	uint32 devfn = ((uint32)device << 3) | function;
	littleEndian = true;

	if (fBridge == BRIDGE_U3_AGP) {
		if (bus == fFirstBus) {
			/* MacRISC CFA0: one-hot device select, function in bits 8..10. */
			address = (1U << device) | ((uint32)function << 8)
				| (offset & 0xfc);
		} else {
			/* MacRISC CFA1: bus, devfn and register offset. */
			address = ((uint32)bus << 16) | (devfn << 8)
				| (offset & 0xfc) | 1;
		}
		return B_OK;
	}

	if (fBridge == BRIDGE_U3_HT) {
		/* U3-HT uses the same MacRISC config-address encoding. */
		if (bus == fFirstBus)
			address = (1U << device) | ((uint32)function << 8) | (offset & 0xfc);
		else
			address = ((uint32)bus << 16) | (devfn << 8) | (offset & 0xfc) | 1;
		return B_OK;
	}

	/* U4 PCIe uses the same CFA1-compatible address register. */
	address = ((uint32)bus << 16) | (devfn << 8) | (offset & 0xfc) | 1;
	return B_OK;
}

status_t
PowerMacG5PCIController::ReadConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32& value)
{
	if (size != 1 && size != 2 && size != 4)
		return B_BAD_VALUE;
	if (offset + size > 0x1000)
		return B_BAD_VALUE;

	uint32 configAddress;
	bool little;
	status_t status = ConfigAddress(bus, device, function, offset & ~3,
		configAddress, little);
	if (status != B_OK)
		return status;

	InterruptsSpinLocker locker(fLock);
	*(vuint32*)fConfigAddress = host_to_pci32(configAddress);
	asm volatile("sync" ::: "memory");
	uint32 raw = *(vuint32*)fConfigData;
	asm volatile("sync" ::: "memory");
	raw = pci_le32(raw);
	uint32 shift = (offset & 3) * 8;
	switch (size) {
		case 1: value = (raw >> shift) & 0xff; break;
		case 2: value = (raw >> shift) & 0xffff; break;
		default: value = raw; break;
	}
	return B_OK;
}

status_t
PowerMacG5PCIController::WriteConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	if (size != 1 && size != 2 && size != 4)
		return B_BAD_VALUE;
	if (offset + size > 0x1000)
		return B_BAD_VALUE;

	uint32 configAddress;
	bool little;
	status_t status = ConfigAddress(bus, device, function, offset & ~3,
		configAddress, little);
	if (status != B_OK)
		return status;

	InterruptsSpinLocker locker(fLock);
	*(vuint32*)fConfigAddress = host_to_pci32(configAddress);
	asm volatile("sync" ::: "memory");
	if (size == 4) {
		*(vuint32*)fConfigData = host_to_pci32(value);
	} else {
		uint32 raw = pci_le32(*(vuint32*)fConfigData);
		uint32 shift = (offset & 3) * 8;
		uint32 mask = size == 1 ? 0xffU : 0xffffU;
		raw = (raw & ~(mask << shift)) | ((value & mask) << shift);
		*(vuint32*)fConfigData = host_to_pci32(raw);
	}
	asm volatile("sync" ::: "memory");
	return B_OK;
}

bool
PowerMacG5PCIController::DevicePresent(uint8 bus, uint8 device, uint8 function)
{
	uint32 id;
	if (ReadConfig(bus, device, function, 0, 4, id) != B_OK)
		return false;
	return id != 0xffffffff && id != 0;
}

status_t
PowerMacG5PCIController::GetMaxBusDevices(int32& count)
{
	count = 32;
	return B_OK;
}

status_t
PowerMacG5PCIController::ReadIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8& irq)
{
	uint32 value;
	if (pin == 0)
		return B_BAD_VALUE;
	status_t status = ReadConfig(bus, device, function, 0x3c, 1, value);
	if (status != B_OK)
		return status;
	irq = (uint8)value;
	return B_OK;
}

status_t
PowerMacG5PCIController::WriteIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8 irq)
{
	if (pin == 0)
		return B_BAD_VALUE;
	return WriteConfig(bus, device, function, 0x3c, 1, irq);
}

status_t
PowerMacG5PCIController::GetRange(uint32 index, pci_resource_range* range)
{
	if (index >= fRangeCount)
		return B_BAD_INDEX;
	*range = fRanges[index];
	return B_OK;
}

status_t
PowerMacG5PCIController::Finalize()
{
	return B_OK;
}

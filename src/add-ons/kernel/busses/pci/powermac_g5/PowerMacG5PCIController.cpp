/* Power Mac G5 U3/U3H/U4 PCI host controller. */
#include "PowerMacG5PCIController.h"

#include <ByteOrder.h>
#include <KernelExport.h>
#include <vm/vm.h>
#include <platform/openfirmware/openfirmware.h>
#include <string.h>
#include <new>


device_manager_info* gDeviceManager;

static inline uint32 pci_le32(uint32 value) { return B_LENDIAN_TO_HOST_INT32(value); }
static inline uint16 pci_le16(uint16 value) { return B_LENDIAN_TO_HOST_INT16(value); }
static inline uint32 host_to_pci32(uint32 value) { return B_HOST_TO_LENDIAN_INT32(value); }
static inline uint16 host_to_pci16(uint16 value) { return B_HOST_TO_LENDIAN_INT16(value); }

static uint32 be32(const uint8* p)
{
	return B_BENDIAN_TO_HOST_INT32(*(const uint32*)p);
}

static uint64 be64(const uint8* p)
{
	return B_BENDIAN_TO_HOST_INT64(*(const uint64*)p);
}

static uint8 rangeAddressType(uint32 flags)
{
	switch (flags & 0x03000000) {
		case 0x01000000:
			return PCI_address_type_32;
		case 0x03000000:
			return PCI_address_type_64;
		default:
			return PCI_address_type_32;
	}
}

float
PowerMacG5PCIController::SupportsDevice(device_node* parent)
{
	const char* bus;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) < B_OK)
		return -1.0f;
	if (strcmp(bus, "openfirmware") != 0 && strcmp(bus, "of") != 0)
		return 0.0f;

	const char* compatible = NULL;
	if (gDeviceManager->get_attr_string(parent, "openfirmware/compatible", &compatible, false) == B_OK) {
		if (strstr(compatible, "u3-") || strstr(compatible, "u4-pcie")
			|| strstr(compatible, "u3-agp"))
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
PowerMacG5PCIController::InitDriver(device_node* node, PowerMacG5PCIController*& out)
{
	ObjectDeleter<PowerMacG5PCIController> driver(new(std::nothrow) PowerMacG5PCIController());
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
	fRangeCount = 0;
	const char* compatible = NULL;
	if (gDeviceManager->get_attr_string(node, "openfirmware/compatible", &compatible, false) == B_OK) {
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

	uint8 firstBus = 0, lastBus = 0xff;
	if (gDeviceManager->get_attr_uint8(node, "openfirmware/first-bus", &firstBus, false) == B_OK)
		fFirstBus = firstBus;
	if (gDeviceManager->get_attr_uint8(node, "openfirmware/last-bus", &lastBus, false) == B_OK)
		fLastBus = lastBus;

	return MapWindows();
}

status_t
PowerMacG5PCIController::MapWindows()
{
	/* OF PCI ranges are 7 cells on the G5: child(3), parent(2), size(2).
	 * Keep firmware translations intact; the PCI bus manager uses these
	 * ranges to turn BAR PCI addresses into host physical addresses. */
	const void* raw = NULL;
	size_t rawSize = 0;
	if (gDeviceManager->get_attr_raw(fNode, "openfirmware/ranges", &raw, &rawSize, false) == B_OK
		&& raw != NULL && rawSize >= 28) {
		const uint8* p = (const uint8*)raw;
		const size_t entrySize = 28;
		for (size_t offset = 0; offset + entrySize <= rawSize && fRangeCount < 4;
			offset += entrySize) {
			const uint32 flags = be32(p + offset);
			const uint64 child = ((uint64)be32(p + offset + 4) << 32)
				| be32(p + offset + 8);
			const uint64 host = be64(p + offset + 12);
			const uint64 size = be64(p + offset + 20);
			if (size == 0)
				continue;

			pci_resource_range& range = fRanges[fRangeCount++];
			range = {};
			const uint32 space = flags & 0x03000000;
			if (space == 0x01000000) {
				range.type = B_IO_PORT;
			} else if (space == 0x02000000 || space == 0x03000000) {
				range.type = B_IO_MEMORY;
			} else {
				--fRangeCount;
				continue;
			}
			range.address_type = rangeAddressType(flags);
			if (flags & 0x40000000)
				range.address_type |= PCI_address_prefetchable;
			range.host_address = host;
			range.pci_address = child;
			range.size = size;
		}
	}

	/* Some G5 firmware does not publish ranges through the device manager.
	 * These are the known MacRISC configuration apertures and do not replace
	 * resource windows when OF supplied them. */
	phys_addr_t address = 0;
	phys_addr_t data = 0;
	switch (fBridge) {
		case BRIDGE_U3_AGP:
		case BRIDGE_U4_PCIE:
			address = 0xf0800000;
			data = 0xf0c00000;
			break;
		case BRIDGE_U3_HT:
			address = 0xf0800000;
			data = 0xf0c00000;
			break;
	}

	fConfigAddressArea = map_physical_memory("g5-pci-cfg-address", address, 0x1000,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&fConfigAddress);
	if (fConfigAddressArea < 0)
		return fConfigAddressArea;
	fConfigDataArea = map_physical_memory("g5-pci-cfg-data", data, 0x1000,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&fConfigData);
	if (fConfigDataArea < 0) {
		delete_area(fConfigAddressArea);
		fConfigAddressArea = -1;
		return fConfigDataArea;
	}
	return B_OK;
}

void PowerMacG5PCIController::UninitDriver() { delete this; }

PowerMacG5PCIController::~PowerMacG5PCIController()
{
	if (fConfigDataArea >= 0) delete_area(fConfigDataArea);
	if (fConfigAddressArea >= 0) delete_area(fConfigAddressArea);
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
	if (fBridge == BRIDGE_U3_AGP || fBridge == BRIDGE_U3_HT) {
		if (bus == fFirstBus)
			address = (1U << device) | ((uint32)function << 8) | (offset & 0xfc);
		else
			address = ((uint32)bus << 16) | (devfn << 8) | (offset & 0xfc) | 1;
		return B_OK;
	}
	address = ((uint32)bus << 16) | (devfn << 8) | (offset & 0xfc) | 1;
	return B_OK;
}

status_t
PowerMacG5PCIController::ReadConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32& value)
{
	if ((size != 1 && size != 2 && size != 4) || offset + size > 0x1000)
		return B_BAD_VALUE;
	uint32 configAddress; bool little;
	status_t status = ConfigAddress(bus, device, function, offset & ~3, configAddress, little);
	if (status != B_OK) return status;
	InterruptsSpinLocker locker(fLock);
	*(vuint32*)fConfigAddress = host_to_pci32(configAddress);
	asm volatile("sync" ::: "memory");
	uint32 raw = pci_le32(*(vuint32*)fConfigData);
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
	if ((size != 1 && size != 2 && size != 4) || offset + size > 0x1000)
		return B_BAD_VALUE;
	uint32 configAddress; bool little;
	status_t status = ConfigAddress(bus, device, function, offset & ~3, configAddress, little);
	if (status != B_OK) return status;
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

bool PowerMacG5PCIController::DevicePresent(uint8 bus, uint8 device, uint8 function)
{
	uint32 id;
	if (ReadConfig(bus, device, function, 0, 4, id) != B_OK) return false;
	return id != 0xffffffff && id != 0;
}

status_t PowerMacG5PCIController::GetMaxBusDevices(int32& count) { count = 32; return B_OK; }

status_t
PowerMacG5PCIController::ReadIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8& irq)
{
	if (pin == 0) return B_BAD_VALUE;
	uint32 value;
	status_t status = ReadConfig(bus, device, function, 0x3c, 1, value);
	if (status != B_OK) return status;
	irq = (uint8)value;
	return B_OK;
}

status_t
PowerMacG5PCIController::WriteIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8 irq)
{
	if (pin == 0) return B_BAD_VALUE;
	return WriteConfig(bus, device, function, 0x3c, 1, irq);
}

status_t PowerMacG5PCIController::GetRange(uint32 index, pci_resource_range* range)
{
	if (range == NULL || index >= fRangeCount) return B_BAD_INDEX;
	*range = fRanges[index];
	return B_OK;
}

status_t PowerMacG5PCIController::Finalize() { return B_OK; }

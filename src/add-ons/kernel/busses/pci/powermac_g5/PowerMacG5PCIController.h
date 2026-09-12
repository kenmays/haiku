/* Power Mac G5 U3/U3H/U4 PCI host controller. */
#ifndef _POWER_MAC_G5_PCI_CONTROLLER_H_
#define _POWER_MAC_G5_PCI_CONTROLLER_H_

#include <bus/PCI.h>
#include <AutoDeleterOS.h>
#include <lock.h>

#define POWERMAC_G5_PCI_DRIVER_MODULE_NAME "busses/pci/powermac_g5/driver_v1"

class PowerMacG5PCIController {
public:
	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, PowerMacG5PCIController*& out);
	void UninitDriver();

	status_t ReadConfig(uint8 bus, uint8 device, uint8 function, uint16 offset,
		uint8 size, uint32& value);
	status_t WriteConfig(uint8 bus, uint8 device, uint8 function, uint16 offset,
		uint8 size, uint32 value);
	status_t GetMaxBusDevices(int32& count);
	status_t ReadIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8& irq);
	status_t WriteIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8 irq);
	status_t GetRange(uint32 index, pci_resource_range* range);
	status_t Finalize();

private:
	PowerMacG5PCIController() = default;
	~PowerMacG5PCIController();
	status_t Init(device_node* node);
	status_t MapWindows();
	status_t Read32(uint32 address, uint32& value, bool littleEndian);
	status_t Write32(uint32 address, uint32 value, bool littleEndian);
	status_t ConfigAddress(uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint32& address, bool& littleEndian) const;
	bool DevicePresent(uint8 bus, uint8 device, uint8 function);

private:
	spinlock fLock = B_SPINLOCK_INITIALIZER;
	device_node* fNode = NULL;
	addr_t fConfigAddress = 0;
	addr_t fConfigData = 0;
	area_id fConfigAddressArea = -1;
	area_id fConfigDataArea = -1;
	uint8 fFirstBus = 0;
	uint8 fLastBus = 0xff;
	enum bridge_type { BRIDGE_U3_AGP, BRIDGE_U3_HT, BRIDGE_U4_PCIE } fBridge;
	pci_resource_range fRanges[4] = {};
	uint32 fRangeCount = 0;
};

extern device_manager_info* gDeviceManager;

#endif

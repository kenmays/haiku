#ifndef RDNA4_BACKEND_H
#define RDNA4_BACKEND_H

#include <SupportDefs.h>

#include "rdna4_hw.h"

/* IP blocks are kept separate because RDNA4 is an SoC composed of versioned
 * hardware blocks. The backend selects implementations only after discovery. */
enum rdna4_ip_block {
	RDNA4_IP_COMMON = 0,
	RDNA4_IP_GMC,
	RDNA4_IP_IH,
	RDNA4_IP_PSP,
	RDNA4_IP_SMU,
	RDNA4_IP_DCN,
	RDNA4_IP_GFX,
	RDNA4_IP_SDMA,
	RDNA4_IP_VCN,
	RDNA4_IP_COUNT
};

enum rdna4_backend_state {
	RDNA4_BACKEND_NEW = 0,
	RDNA4_BACKEND_DISCOVERED,
	RDNA4_BACKEND_FIRMWARE_READY,
	RDNA4_BACKEND_MEMORY_READY,
	RDNA4_BACKEND_ENGINES_READY,
	RDNA4_BACKEND_DISPLAY_READY,
	RDNA4_BACKEND_RUNNING,
	RDNA4_BACKEND_FAILED
};

struct rdna4_ip_version {
	uint16 major;
	uint16 minor;
	uint16 revision;
	bool present;
};

struct rdna4_backend_info {
	uint32 gfx_version;
	uint32 dcn_version;
	uint32 vmid_count;
	rdna4_backend_state state;
	rdna4_ip_version ip[RDNA4_IP_COUNT];
};

struct rdna4_backend_ops {
	status_t (*discover)(void* context, rdna4_backend_info& info);
	status_t (*load_firmware)(void* context);
	status_t (*init_memory)(void* context);
	status_t (*init_engines)(void* context);
	status_t (*init_display)(void* context);
	status_t (*start)(void* context);
	status_t (*stop)(void* context);
};

class RDNA4Backend {
public:
	RDNA4Backend();

	status_t Initialize(const rdna4_backend_ops* ops, void* context);
	status_t Start();
	status_t Stop();
	status_t Reset();

	const rdna4_backend_info& Info() const { return fInfo; }
	const rdna4_hw_ops* HardwareOps() const { return fHardwareOps; }
	void SetHardwareOps(const rdna4_hw_ops* ops) { fHardwareOps = ops; }

private:
	const rdna4_backend_ops* fOps;
	const rdna4_hw_ops* fHardwareOps;
	void* fContext;
	rdna4_backend_info fInfo;
};

#endif

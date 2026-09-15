#ifndef RDNA4_FRONTEND_H
#define RDNA4_FRONTEND_H

#include <Accelerant.h>
#include <SupportDefs.h>

/* Frontend-facing state is deliberately independent from the hardware
 * backend and kernel-private driver headers. It owns the Haiku accelerant
 * contract: devices, connectors, modes, framebuffer policy and DPMS. */
enum rdna4_frontend_state {
	RDNA4_FRONTEND_NEW = 0,
	RDNA4_FRONTEND_ATTACHED,
	RDNA4_FRONTEND_MODES_READY,
	RDNA4_FRONTEND_ACTIVE,
	RDNA4_FRONTEND_SUSPENDED,
	RDNA4_FRONTEND_FAILED
};

struct rdna4_frontend_connector {
	uint32 id;
	uint32 flags;
	uint32 mode_count;
	bool connected;
	bool has_edid;
};

struct rdna4_frontend_info {
	uint32 version;
	uint32 connector_count;
	uint32 active_connector;
	rdna4_frontend_state state;
	uint32 dpms_mode;
	bool framebuffer_valid;
};

struct rdna4_frontend_ops {
	status_t (*attach)(void* context, rdna4_frontend_info& info);
	status_t (*refresh_modes)(void* context, uint32 connector);
	status_t (*set_mode)(void* context, uint32 connector,
		const display_mode& mode, uint64 framebuffer);
	status_t (*set_dpms)(void* context, uint32 mode);
	status_t (*get_edid)(void* context, uint32 connector,
		void* buffer, size_t size, size_t* _actual);
	status_t (*detach)(void* context);
};

class RDNA4Frontend {
public:
	RDNA4Frontend();

	status_t Initialize(const rdna4_frontend_ops* ops, void* context);
	status_t RefreshModes(uint32 connector);
	status_t SetMode(uint32 connector, const display_mode& mode,
		uint64 framebuffer);
	status_t SetDPMS(uint32 mode);
	status_t GetEDID(uint32 connector, void* buffer, size_t size,
		size_t* _actual);
	status_t Uninitialize();

	const rdna4_frontend_info& Info() const { return fInfo; }
	const rdna4_frontend_connector* Connector(uint32 index) const;
	bool IsAttached() const { return fInfo.state != RDNA4_FRONTEND_NEW
		&& fInfo.state != RDNA4_FRONTEND_FAILED; }

private:
	const rdna4_frontend_ops* fOps;
	void* fContext;
	rdna4_frontend_info fInfo;
	rdna4_frontend_connector fConnectors[8];
};

#endif

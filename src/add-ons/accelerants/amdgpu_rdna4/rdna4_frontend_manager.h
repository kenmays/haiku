#ifndef RDNA4_FRONTEND_MANAGER_H
#define RDNA4_FRONTEND_MANAGER_H

#include <Accelerant.h>
#include <SupportDefs.h>

#include "rdna4_frontend.h"

struct rdna4_frontend_mode {
	display_mode mode;
	bool preferred;
};

class RDNA4FrontendManager {
public:
	RDNA4FrontendManager();

	status_t Initialize(RDNA4Frontend* frontend);
	status_t Refresh(uint32 connector);
	status_t SetMode(uint32 connector, const display_mode& mode,
		uint64 framebuffer);
	status_t SetDPMS(uint32 mode);
	status_t GetModeList(uint32 connector, display_mode* modes,
		uint32 capacity, uint32* _count) const;
	status_t GetPreferredMode(uint32 connector, display_mode* mode) const;
	status_t GetCurrentMode(display_mode* mode) const;
	status_t GetEDID(uint32 connector, void* buffer, size_t size,
		size_t* _actual);

	uint32 ModeCount(uint32 connector) const;
	bool HasFramebuffer() const { return fFramebuffer != 0; }
	void SetFramebuffer(uint64 address) { fFramebuffer = address; }

private:
	RDNA4Frontend* fFrontend;
	rdna4_frontend_mode fModes[8][32];
	uint32 fModeCounts[8];
	uint32 fCurrentConnector;
	uint64 fFramebuffer;
	display_mode fCurrentMode;
	bool fHasCurrentMode;
};

#endif

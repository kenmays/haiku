#ifndef RDNA4_DISPLAY_H
#define RDNA4_DISPLAY_H

#include <SupportDefs.h>
#include <Accelerant.h>

class RDNA4Display {
public:
	RDNA4Display();
	status_t Initialize();
	status_t GetModeList(display_mode** modes, uint32* count);
	status_t SetMode(const display_mode& mode);
	status_t SetCursor(const uint32* data, uint32 width, uint32 height);
	status_t MoveCursor(int32 x, int32 y);
	status_t ShowCursor(bool visible);

private:
	bool fInitialized;
};

#endif

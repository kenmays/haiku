#ifndef RDNA4_FRONTEND_EVENTS_H
#define RDNA4_FRONTEND_EVENTS_H

#include <SupportDefs.h>

enum rdna4_frontend_event_type {
	RDNA4_FRONTEND_EVENT_HOTPLUG = 0,
	RDNA4_FRONTEND_EVENT_VBLANK,
	RDNA4_FRONTEND_EVENT_PAGE_FLIP,
	RDNA4_FRONTEND_EVENT_FENCE,
	RDNA4_FRONTEND_EVENT_ERROR
};

struct rdna4_frontend_event {
	rdna4_frontend_event_type type;
	uint32 connector;
	uint64 sequence;
	status_t status;
	bigtime_t timestamp;
};

typedef status_t (*rdna4_frontend_event_callback)(void* context,
	const rdna4_frontend_event& event);

class RDNA4FrontendEvents {
public:
	RDNA4FrontendEvents();

	void SetCallback(rdna4_frontend_event_callback callback, void* context);
	status_t Publish(const rdna4_frontend_event& event);
	uint64 LastSequence() const { return fLastSequence; }

private:
	rdna4_frontend_event_callback fCallback;
	void* fContext;
	uint64 fLastSequence;
};

#endif

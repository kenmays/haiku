#include "rdna4_frontend_events.h"

RDNA4FrontendEvents::RDNA4FrontendEvents()
	: fCallback(NULL), fContext(NULL), fLastSequence(0)
{
}

void
RDNA4FrontendEvents::SetCallback(rdna4_frontend_event_callback callback,
	void* context)
{
	fCallback = callback;
	fContext = context;
}

status_t
RDNA4FrontendEvents::Publish(const rdna4_frontend_event& event)
{
	if (event.sequence > fLastSequence)
		fLastSequence = event.sequence;
	if (fCallback == NULL)
		return B_NO_INIT;
	return fCallback(fContext, event);
}

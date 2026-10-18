/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MIDI2_MIDI1_TRANSPORT_H
#define _MIDI2_MIDI1_TRANSPORT_H

#include <SupportDefs.h>
#include <stddef.h>
#include <midi2/Midi1Codec.h>

namespace BPrivate {

/*
 * Write boundary for a MIDI 1.0 byte stream. A concrete backend owns device
 * lifecycle, scheduling, and physical or packet transport framing.
 *
 * DIN/UART backends write MIDI bytes directly. USB-MIDI backends must encode
 * bytes into USB-MIDI event packets; incoming USB event packets must be
 * decoded before calling Midi1Receiver::Feed(). This interface does not
 * conflate byte-stream protocol with transport framing.
 */
class Midi1ByteTransport {
public:
	virtual ~Midi1ByteTransport() {}
	virtual status_t WriteBytes(const uint8* bytes, size_t size,
		size_t* written) = 0;
};

/* Thin write-side adapter. Caller serializes concurrent writes. */
class Midi1TransportWriter {
public:
	explicit Midi1TransportWriter(Midi1ByteTransport* transport);
	status_t SendChannelMessage(uint8 status, uint8 data1, uint8 data2,
		size_t* written);

private:
	Midi1ByteTransport* fTransport;
};

} // namespace BPrivate
#endif

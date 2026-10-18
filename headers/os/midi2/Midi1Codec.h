/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MIDI2_MIDI1_CODEC_H
#define _MIDI2_MIDI1_CODEC_H

#include <SupportDefs.h>
#include <stddef.h>

namespace BPrivate {

/*
 * A decoded MIDI 1.0 message. size includes the status byte; unused data
 * fields are zero. Channel Voice messages are 2 or 3 bytes. System messages
 * dispatched by Midi1Receiver are one byte (Real-Time/Tune Request) or the
 * complete System Common message.
 */
struct midi1_message {
	uint8 status;
	uint8 data1;
	uint8 data2;
	uint8 size;
};

/*
 * Incremental MIDI 1.0 byte-stream parser.
 *
 * The listener is not owned and must outlive this receiver. Callbacks are
 * synchronous with Feed(); SysExData() receives a pointer valid only for the
 * duration of that callback. The listener must copy the bytes if it needs to
 * retain them. Feed() reports invalid arguments through its status_t return
 * value and malformed MIDI input through Listener::ParseError().
 *
 * Real-Time bytes may occur between any two bytes and do not disturb pending
 * messages or running status. System Common cancels running status. Reserved
 * status bytes are rejected. SysEx payload is streamed without buffering the
 * entire message.
 */
class Midi1Receiver {
public:
	class Listener {
	public:
		virtual ~Listener() {}
		virtual void MessageReceived(const midi1_message& message) = 0;
		virtual void SysExStarted() {}
		virtual void SysExData(const uint8* data, size_t size) = 0;
		virtual void SysExEnded() {}
		virtual void ParseError(status_t error) {}
	};

	explicit Midi1Receiver(Listener* listener);
	void Reset();
	status_t Feed(const uint8* bytes, size_t size);

private:
	void _Dispatch(uint8 status, uint8 data1, uint8 data2, uint8 length);
	void _HandleByte(uint8 byte);
	void _HandleStatus(uint8 status);
	Listener* fListener;
	uint8 fRunningStatus, fPendingStatus, fData[2], fDataCount, fRequiredData;
	bool fInSysEx;
};

/*
 * Stateless helpers for MIDI 1.0 message lengths and Channel Voice encoding.
 * EncodeChannelMessage() writes only a complete message: on insufficient
 * capacity it returns B_BUFFER_OVERFLOW and leaves written equal to zero.
 * Invalid status/data arguments return B_BAD_VALUE.
 */
class Midi1Codec {
public:
	static status_t EncodeChannelMessage(uint8 status, uint8 data1,
		uint8 data2, uint8* output, size_t capacity, size_t* written);
	static uint8 ChannelMessageLength(uint8 status);
	static uint8 SystemMessageLength(uint8 status);
};

} // namespace BPrivate
#endif

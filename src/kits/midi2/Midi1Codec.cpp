/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <midi2/Midi1Codec.h>
#include <Errors.h>
#include <string.h>

namespace BPrivate {
Midi1Receiver::Midi1Receiver(Listener* listener)
	: fListener(listener), fRunningStatus(0), fPendingStatus(0),
	  fDataCount(0), fRequiredData(0), fInSysEx(false)
{
	memset(fData, 0, sizeof(fData));
}

void Midi1Receiver::Reset()
{
	fRunningStatus = fPendingStatus = fDataCount = fRequiredData = 0;
	fInSysEx = false;
}

uint8 Midi1Codec::ChannelMessageLength(uint8 status)
{
	if (status < 0x80 || status > 0xef) return 0;
	uint8 kind = status & 0xf0;
	return kind == 0xc0 || kind == 0xd0 ? 2 : 3;
}

uint8 Midi1Codec::SystemMessageLength(uint8 status)
{
	switch (status) {
		case 0xf1: case 0xf3: return 2;
		case 0xf2: return 3;
		case 0xf6: case 0xf8: case 0xfa: case 0xfb: case 0xfc:
		case 0xfe: case 0xff: return 1;
		default: return 0;
	}
}

void Midi1Receiver::_Dispatch(uint8 status, uint8 d1, uint8 d2, uint8 length)
{
	if (fListener == NULL) return;
	midi1_message message = {status, d1, d2, length, false};
	fListener->MessageReceived(message);
}

void Midi1Receiver::_HandleStatus(uint8 status)
{
	if (status >= 0x80 && status <= 0xef) {
		fRunningStatus = fPendingStatus = status;
		fDataCount = 0;
		fRequiredData = Midi1Codec::ChannelMessageLength(status) - 1;
		return;
	}

	// System Common cancels running status; Real-Time never reaches this path.
	fRunningStatus = 0;
	fPendingStatus = status;
	fDataCount = 0;
	uint8 length = Midi1Codec::SystemMessageLength(status);
	fRequiredData = length > 0 ? length - 1 : 0;
	switch (status) {
		case 0xf0:
			fInSysEx = true;
			fPendingStatus = 0;
			fRequiredData = 0;
			if (fListener != NULL) fListener->SysExStarted();
			break;
		case 0xf7:
			if (fInSysEx) {
				fInSysEx = false;
				if (fListener != NULL) fListener->SysExEnded();
			} else if (fListener != NULL)
				fListener->ParseError(B_BAD_DATA);
			fPendingStatus = 0;
			break;
		case 0xf4: case 0xf5: case 0xf9: case 0xfd:
			if (fListener != NULL) fListener->ParseError(B_BAD_DATA);
			fPendingStatus = fRequiredData = 0;
			break;
		default:
			if (length == 1) {
				_Dispatch(status, 0, 0, 1);
				fPendingStatus = 0;
			}
			break;
	}
}

void Midi1Receiver::_HandleByte(uint8 byte)
{
	// Real-Time messages may interrupt any message and preserve parser state.
	if (byte >= 0xf8) {
		if (Midi1Codec::SystemMessageLength(byte) != 0)
			_Dispatch(byte, 0, 0, 1);
		else if (fListener != NULL)
			fListener->ParseError(B_BAD_DATA);
		return;
	}

	if (byte & 0x80) {
		if (fInSysEx) {
			if (byte == 0xf7) {
				fInSysEx = false;
				if (fListener != NULL) fListener->SysExEnded();
				return;
			}
			fInSysEx = false;
			if (fListener != NULL) {
				fListener->ParseError(B_BAD_DATA);
				fListener->SysExEnded();
			}
		}
		_HandleStatus(byte);
		return;
	}

	if (fInSysEx) {
		if (fListener != NULL) fListener->SysExData(&byte, 1);
		return;
	}

	if (fPendingStatus == 0) {
		if (fRunningStatus == 0) {
			if (fListener != NULL) fListener->ParseError(B_BAD_DATA);
			return;
		}
		fPendingStatus = fRunningStatus;
		fRequiredData = Midi1Codec::ChannelMessageLength(fPendingStatus) - 1;
		fDataCount = 0;
	}
	if (fRequiredData == 0 || fDataCount >= sizeof(fData)) {
		if (fListener != NULL) fListener->ParseError(B_BAD_DATA);
		fPendingStatus = fDataCount = fRequiredData = 0;
		return;
	}

	fData[fDataCount++] = byte;
	if (fDataCount == fRequiredData) {
		uint8 status = fPendingStatus;
		uint8 length = Midi1Codec::ChannelMessageLength(status);
		if (length == 0) length = Midi1Codec::SystemMessageLength(status);
		_Dispatch(status, fData[0], fDataCount > 1 ? fData[1] : 0, length);
		fPendingStatus = fRunningStatus;
		fDataCount = 0;
		if (fRunningStatus == 0) fPendingStatus = fRequiredData = 0;
	}
}

status_t Midi1Receiver::Feed(const uint8* bytes, size_t size)
{
	if (bytes == NULL && size != 0) return B_BAD_VALUE;
	for (size_t i = 0; i < size; i++) _HandleByte(bytes[i]);
	return B_OK;
}

status_t Midi1Codec::EncodeChannelMessage(uint8 status, uint8 d1, uint8 d2,
	uint8* output, size_t capacity, size_t* written)
{
	if (written == NULL) return B_BAD_VALUE;
	*written = 0;
	uint8 length = ChannelMessageLength(status);
	if (length == 0 || output == NULL || capacity < length
		|| d1 > 0x7f || (length == 3 && d2 > 0x7f))
		return B_BAD_VALUE;
	output[0] = status;
	output[1] = d1;
	if (length == 3) output[2] = d2;
	*written = length;
	return B_OK;
}
}

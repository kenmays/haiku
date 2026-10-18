/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <midi2/Midi1Transport.h>
#include <Errors.h>

namespace BPrivate {

Midi1TransportWriter::Midi1TransportWriter(Midi1ByteTransport* transport)
	: fTransport(transport)
{
}

status_t
Midi1TransportWriter::SendChannelMessage(uint8 status, uint8 data1,
	uint8 data2, size_t* written)
{
	if (written == NULL)
		return B_BAD_VALUE;
	*written = 0;
	if (fTransport == NULL)
		return B_NO_INIT;

	uint8 bytes[3];
	size_t length = 0;
	status_t error = Midi1Codec::EncodeChannelMessage(status, data1, data2,
		bytes, sizeof(bytes), &length);
	if (error != B_OK)
		return error;

	size_t count = 0;
	error = fTransport->WriteBytes(bytes, length, &count);
	if (count > length)
		return B_BAD_DATA;
	*written = count;
	if (error != B_OK)
		return error;
	return count == length ? B_OK : B_IO_ERROR;
}

} // namespace BPrivate

MIDI 1.0 protocol and transport API
====================================

Normative references
--------------------

The protocol review uses the MIDI Association's MIDI 1.0 Detailed
Specification (version 4.2.1 / 96-1-4) and the MIDI 1.0 Addenda:

* https://midi.org/midi-1-0-detailed-specification
* https://midi.org/midi-1-0-addenda
* https://midi.org/specs

Protocol parser
---------------

Include <midi2/Midi1Codec.h>. Midi1Receiver incrementally parses a byte stream
and emits Listener callbacks. It supports channel voice messages, channel
running status, System Common messages, Real-Time messages, and streaming
SysEx callbacks.

Channel Voice message lengths include the status byte: Program Change and
Channel Pressure are two bytes; the other Channel Voice message families are
three bytes. System Common cancels channel running status. F1 and F3 have one
data byte; F2 has two data bytes; F6 is a single-byte message. Real-Time
messages may be interleaved between bytes without changing the in-progress
message or running status. SysEx begins with F0 and ends with F7.

Transport boundary
------------------

Include <midi2/Midi1Transport.h> to implement a Midi1ByteTransport backend and
use Midi1TransportWriter for complete Channel Voice messages or SendRawBytes()
for System Common, Real-Time, and chunked SysEx byte sequences. The transport
implementation owns device discovery/lifecycle, write scheduling, and
transport-specific framing. Midi1Receiver::Feed() accepts reconstructed MIDI
bytes from the receive side.

A DIN/UART backend carries MIDI bytes over the serial link and is responsible
for the electrical interface and serial configuration. USB-MIDI 1.0 uses
four-byte event packets carrying a cable number, Code Index Number (CIN), and
up to three MIDI bytes. Include <midi2/Midi1USBTransport.h> and use
Midi1USBPacketCodec to encode a complete MIDI message into event packets or
decode individual event packets back into MIDI bytes. USB-MIDI does not use
MIDI running status on the USB packet stream. SysEx encoding checks the full
packet-buffer requirement before writing, so a short output buffer returns
B_BUFFER_OVERFLOW without emitting a partial SysEx stream. Packet decoding
validates the complete event before copying MIDI bytes to caller output, and
standalone EOX is represented using CIN 5. The channel-message encoder reports
insufficient output capacity separately from invalid message arguments. The
codec does not perform USB device discovery, endpoint I/O, transfer scheduling,
or cable lifecycle management; those belong to a USB backend. Bluetooth MIDI
has its own packetization and timing rules and needs a distinct backend.

Haiku MIDI Kit header compatibility
-----------------------------------

Haiku already provides the legacy Midi Kit API in headers such as <Midi.h>,
<MidiPort.h>, and <MidiDefs.h>, as well as the endpoint/producer/consumer API
under <midi2/>. These interfaces are not interchangeable with the new byte
codec: the existing kit provides higher-level event callbacks and timestamps,
while Midi1Receiver exposes decoded wire messages and streamed SysEx bytes.
A production integration needs an explicit adapter for event conversion,
timestamp units, scheduling, and endpoint lifecycle. This change does not
replace the existing public classes or claim source/binary compatibility
between the legacy kit and the new private codec.

The new Midi1Codec, Midi1Transport, and Midi1USBTransport declarations are in
namespace BPrivate. They are internal implementation interfaces, not a stable
public application API. Promoting them to public API requires a separate
review of exported symbols, installed-header conventions, ABI/reserved fields,
threading contracts, error semantics, and integration with BMidiEndpoint and
the MIDI roster.

The existing MIDI constants include the historical B_CABLE_MESSAGE symbol
with value 0xF5. F5 is reserved in the MIDI 1.0 wire protocol; the legacy
symbol must not be interpreted as a valid MIDI 1.0 message by the new codec.
Reserved wire statuses F4, F5, F9, and FD are rejected by the parser.

Regression tests
----------------

The midi1_codec_test target covers all Channel Voice message-length
families, split input, running status, System Common transitions, defined
System Real-Time statuses, Real-Time interleaving, SysEx callbacks, malformed
input, channel encoder argument and capacity errors, partial transport writes,
and USB-MIDI event packet round trips. These tests have been added but have
not yet been compiled or executed in a Haiku build environment.

Scope and validation status
---------------------------

The codec and writer are protocol/adapter components. This branch does not yet
provide physical DIN, USB-MIDI, or Bluetooth device drivers, nor does it claim
end-to-end transport compliance. The code has not been built or run against a
conformance suite. Before claiming complete compliance, add automated tests
for every status family, System Common lengths, running status transitions,
Real-Time interleaving, malformed/reserved statuses, split input chunks,
SysEx termination, partial writes, and each concrete transport backend.

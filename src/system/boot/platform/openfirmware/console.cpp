/*
 * Copyright 2003-2005, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2010 Andreas Färber <andreas.faerber@web.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "console.h"

#include <string.h>

#include <SupportDefs.h>

#include <boot/stage2.h>
#include <platform/openfirmware/openfirmware.h>
#include <util/kernel_cpp.h>

#include "Handle.h"


class Console : public ConsoleNode {
	public:
		Console();

		void SetHandles(intptr_t readHandle, intptr_t writeHandle, bool takeOwnership = true);

		virtual ssize_t ReadAt(void *cookie, off_t pos, void *buffer,
			size_t bufferSize);
		virtual ssize_t WriteAt(void *cookie, off_t pos, const void *buffer,
			size_t bufferSize);

		virtual void	ClearScreen();
		virtual int32	Width();
		virtual int32	Height();
		virtual void	SetCursor(int32 x, int32 y);
		virtual void	SetCursorVisible(bool visible);
		virtual void	SetColors(int32 foreground, int32 background);

		void PutBackChar(char c);
		void PutBackChars(const char *buffer, int count);
		char GetChar();

	private:
		Handle fReadHandle, fWriteHandle;

		enum { BUFFER_SIZE = 32 };
		char	fReadBuffer[BUFFER_SIZE];
		int		fStart;
		int		fCount;
};


extern ConsoleNode* gConsoleNode;
static Console sConsole;
FILE *stdin, *stdout, *stderr;


Console::Console()
	:
	ConsoleNode()
{
}


void
Console::SetHandles(intptr_t readHandle, intptr_t writeHandle, bool takeOwnership)
{
	fReadHandle.SetHandle(readHandle, takeOwnership);
	fWriteHandle.SetHandle(writeHandle, takeOwnership);
}


ssize_t
Console::ReadAt(void */*cookie*/, off_t /*pos*/, void *_buffer,
	size_t bufferSize)
{
	char *buffer = (char*)_buffer;

	// copy buffered bytes first
	int bytesTotal = 0;
	while (bufferSize > 0 && fCount > 0) {
		*buffer++ = GetChar();
		bytesTotal++;
		bufferSize--;
	}

	// read from console
	if (bufferSize > 0) {
		ssize_t bytesRead = fReadHandle.ReadAt(NULL, -1, buffer, bufferSize);
		if (bytesRead < 0)
			return bytesRead;
		bytesTotal += bytesRead;
	}

	return bytesTotal;
}


ssize_t
Console::WriteAt(void */*cookie*/, off_t /*pos*/, const void *buffer,
	size_t bufferSize)
{
	const char *string = (const char *)buffer;

	// If the frame buffer is enabled, don't write to the chosen stdout.
	// On Apple's OpenFirmware this would overwrite parts of the frame buffer.
	if (gKernelArgs.frame_buffer.enabled)
		return bufferSize;

	// be nice to our audience and replace single "\n" with "\r\n"

	while (bufferSize > 0) {
		bool newLine = false;
		size_t length = 0;

		for (; length < bufferSize; length++) {
			if (string[length] == '\r' && length + 1 < bufferSize) {
				length += 2;
			} else if (string[length] == '\n') {
				newLine = true;
				break;
			}
		}

		if (length > 0) {
			fWriteHandle.WriteAt(NULL, -1, string, length);
			string += length;
			bufferSize -= length;
		}

		if (newLine) {
			// this code replaces a single '\n' with '\r\n', so it
			// bumps the string/bufferSize only a single character
			fWriteHandle.WriteAt(NULL, -1, "\r\n", 2);
			string++;
			bufferSize--;
		}
	}

	return string - (char *)buffer;
}


void
Console::PutBackChar(char c)
{
	if (fCount >= BUFFER_SIZE)
		return;

	int pos = (fStart + fCount) % BUFFER_SIZE;
	fReadBuffer[pos] = c;
	fCount++;
}


void
Console::PutBackChars(const char *buffer, int count)
{
	for (int i = 0; i < count; i++)
		PutBackChar(buffer[i]);
}


char
Console::GetChar()
{
	if (fCount == 0)
		return 0;

	fCount--;
	char c = fReadBuffer[fStart];
	fStart = (fStart + 1) % BUFFER_SIZE;
	return c;
}


//	#pragma mark -


status_t
console_init(void)
{
	unsigned int input, output;
	if (of_getprop(gChosen, "stdin", &input, sizeof(input)) != sizeof(input))
		return B_ERROR;
	if (of_getprop(gChosen, "stdout", &output, sizeof(output))
			!= sizeof(output)) {
		return B_ERROR;
	}

	sConsole.SetHandles(input, output);
	gConsoleNode = &sConsole;

	// now that we're initialized, enable stdio functionality
	stdin = (FILE *)gConsoleNode;
	stdout = stderr = (FILE *)gConsoleNode;

	return B_OK;
}


// #pragma mark -


void
Console::ClearScreen()
{
#ifdef __sparc__
	// Send both a clear screen (for serial terminal) and a vertical form
	// feed for on-screen console
	Write("\014\033[2J", 5);
#else
	of_interpret("erase-screen", 0, 0);
#endif
}


int32
Console::Width()
{
	intptr_t columnCount;
#ifdef __sparc__
	if (of_interpret("screen-#columns", 0, 1, &columnCount) == OF_FAILED)
#else
	if (of_interpret("#columns", 0, 1, &columnCount) == OF_FAILED)
#endif
		return 0;
	return columnCount;
}


int32
Console::Height()
{
	intptr_t lineCount;
#ifdef __sparc__
	if (of_interpret("screen-#rows", 0, 1, &lineCount) == OF_FAILED)
#else
	if (of_interpret("#lines", 0, 1, &lineCount) == OF_FAILED)
#endif
		return 0;
	return lineCount;
}


void
Console::SetCursor(int32 x, int32 y)
{
#ifdef __sparc__
	char buffer[11];
	int len = snprintf(buffer, sizeof(buffer),
		"\033[%" B_PRId32 ";%" B_PRId32 "H", y, x);
	Write(buffer, len);
#else
	// Note: We toggle the cursor temporarily to prevent a cursor artifact at
	// at the old location.
	of_interpret("toggle-cursor"
		" to line#"
		" to column#"
		" toggle-cursor",
		2, 0, y, x);
#endif
}


void
Console::SetCursorVisible(bool visible)
{
}


#ifndef __sparc__
static int
translate_color(int32 color)
{
	/*
			r	g	b
		0:	0	0	0		// black
		1:	0	0	aa		// dark blue
		2:	0	aa	0		// dark green
		3:	0	aa	aa		// cyan
		4:	aa	0	0		// dark red
		5:	aa	0	aa		// purple
		6:	aa	55	0		// brown
		7:	aa	aa	aa		// light gray
		8:	55	55	55		// dark gray
		9:	55	55	ff		// light blue
		a:	55	ff	55		// light green
		b:	55	ff	ff		// light cyan
		c:	ff	55	55		// light red
		d:	ff	55	ff		// magenta
		e:	ff	ff	55		// yellow
		f:	ff	ff	ff		// white
	*/
	if (color >= 0 && color < 16)
		return color;
	return 0;
}
#endif


void
Console::SetColors(int32 foreground, int32 background)
{
#ifdef __sparc__
	// Sadly it seems we can only get inverse video, nothing else seems to work
	if (background != 0)
		Write("\033[7m", 4);
	else
		Write("\033[0m", 4);
#else
	// Note: Toggling the cursor doesn't seem to help. We still get cursor
	// artifacts.
	of_interpret("toggle-cursor"
		" to foreground-color"
		" to background-color"
		" toggle-cursor",
		2, 0, translate_color(foreground), translate_color(background));
#endif
}


static int
translate_key(char escapeCode)
{
	switch (escapeCode) {
		case 65:
			return TEXT_CONSOLE_KEY_UP;
		case 66:
			return TEXT_CONSOLE_KEY_DOWN;
		case 67:
			return TEXT_CONSOLE_KEY_RIGHT;
		case 68:
			return TEXT_CONSOLE_KEY_LEFT;
// TODO: Translate the codes for the following keys. Unfortunately my OF just
// returns a '\0' character. :-/
// 			TEXT_CONSOLE_KEY_PAGE_UP,
// 			TEXT_CONSOLE_KEY_PAGE_DOWN,
// 			TEXT_CONSOLE_KEY_HOME,
// 			TEXT_CONSOLE_KEY_END,

		default:
			return 0;
	}
}


int
console_wait_for_key(void)
{
	// wait for a key
	char buffer[3];
	ssize_t bytesRead;
	do {
		bytesRead = sConsole.ReadAt(NULL, 0, buffer, 3);
		if (bytesRead < 0)
			return 0;
	} while (bytesRead == 0);

	// translate the ESC sequences for cursor keys
	if (bytesRead == 3 && buffer[0] == 27 && buffer[1] == 91) {
		int key = translate_key(buffer[2]);
		if (key != 0)
			return key;
	}

	// put back unread chars
	if (bytesRead > 1)
		sConsole.PutBackChars(buffer + 1, bytesRead - 1);

	return buffer[0];
}


int
console_check_for_key(void)
{
	char buffer[3];
	ssize_t bytesRead = sConsole.ReadAt(NULL, 0, buffer, 3);
	if (bytesRead <= 0)
		return 0;

	// translate the ESC sequences for cursor keys
	if (bytesRead == 3 && buffer[0] == 27 && buffer[1] == 91) {
		int key = translate_key(buffer[2]);
		if (key != 0)
			return key;
	}

	// put back unread chars
	if (bytesRead > 1)
		sConsole.PutBackChars(buffer + 1, bytesRead - 1);

	return buffer[0];
}

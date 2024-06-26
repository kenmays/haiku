/*
 * Copyright 2013-2014, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2021-2024, Andrew Lindesay <apl@lindesay.co.nz>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "TextDocument.h"

#include <algorithm>
#include <stdio.h>
#include <vector>


TextDocument::TextDocument()
	:
	fParagraphs(),
	fEmptyLastParagraph(),
	fDefaultCharacterStyle()
{
}


TextDocument::TextDocument(CharacterStyle characterStyle,
	ParagraphStyle paragraphStyle)
	:
	fParagraphs(),
	fEmptyLastParagraph(paragraphStyle),
	fDefaultCharacterStyle(characterStyle)
{
}


TextDocument::TextDocument(const TextDocument& other)
	:
	fParagraphs(other.fParagraphs),
	fEmptyLastParagraph(other.fEmptyLastParagraph),
	fDefaultCharacterStyle(other.fDefaultCharacterStyle)
{
}


TextDocument&
TextDocument::operator=(const TextDocument& other)
{
	fParagraphs = other.fParagraphs;
	fEmptyLastParagraph = other.fEmptyLastParagraph;
	fDefaultCharacterStyle = other.fDefaultCharacterStyle;

	return *this;
}


bool
TextDocument::operator==(const TextDocument& other) const
{
	if (this == &other)
		return true;

	return fEmptyLastParagraph == other.fEmptyLastParagraph
		&& fDefaultCharacterStyle == other.fDefaultCharacterStyle
		&& fParagraphs == other.fParagraphs;
}


bool
TextDocument::operator!=(const TextDocument& other) const
{
	return !(*this == other);
}


// #pragma mark -


status_t
TextDocument::Insert(int32 textOffset, const BString& text)
{
	return Replace(textOffset, 0, text);
}


status_t
TextDocument::Insert(int32 textOffset, const BString& text,
	CharacterStyle style)
{
	return Replace(textOffset, 0, text, style);
}


status_t
TextDocument::Insert(int32 textOffset, const BString& text,
	CharacterStyle characterStyle, ParagraphStyle paragraphStyle)
{
	return Replace(textOffset, 0, text, characterStyle, paragraphStyle);
}


// #pragma mark -


status_t
TextDocument::Remove(int32 textOffset, int32 length)
{
	return Replace(textOffset, length, BString());
}


// #pragma mark -


status_t
TextDocument::Replace(int32 textOffset, int32 length, const BString& text)
{
	return Replace(textOffset, length, text, CharacterStyleAt(textOffset));
}


status_t
TextDocument::Replace(int32 textOffset, int32 length, const BString& text,
	CharacterStyle style)
{
	return Replace(textOffset, length, text, style,
		ParagraphStyleAt(textOffset));
}


status_t
TextDocument::Replace(int32 textOffset, int32 length, const BString& text,
	CharacterStyle characterStyle, ParagraphStyle paragraphStyle)
{
	TextDocumentRef document = NormalizeText(text, characterStyle,
		paragraphStyle);

	if (!document.IsSet())
		return B_NO_MEMORY;

	if (document->Length() != text.CountChars())
		return B_NO_MEMORY;

	return Replace(textOffset, length, document);
}


status_t
TextDocument::Replace(int32 textOffset, int32 length, TextDocumentRef document)
{
	int32 firstParagraph = 0;
	int32 paragraphCount = 0;

	// TODO: Call _NotifyTextChanging() before any change happened

	status_t ret = _Remove(textOffset, length, firstParagraph, paragraphCount);
	if (ret != B_OK)
		return ret;

	ret = _Insert(textOffset, document, firstParagraph, paragraphCount);

	_NotifyTextChanged(TextChangedEvent(firstParagraph, paragraphCount));

	return ret;
}


// #pragma mark -


const CharacterStyle&
TextDocument::CharacterStyleAt(int32 textOffset) const
{
	int32 paragraphOffset;
	const Paragraph& paragraph = ParagraphAt(textOffset, paragraphOffset);

	textOffset -= paragraphOffset;
	int32 index;
	int32 count = paragraph.CountTextSpans();

	for (index = 0; index < count; index++) {
		const TextSpan& span = paragraph.TextSpanAtIndex(index);
		if (textOffset - span.CountChars() < 0)
			return span.Style();
		textOffset -= span.CountChars();
	}

	return fDefaultCharacterStyle;
}


const BMessage*
TextDocument::ClickMessageAt(int32 textOffset) const
{
	int32 paragraphOffset;
	const Paragraph& paragraph = ParagraphAt(textOffset, paragraphOffset);

	textOffset -= paragraphOffset;
	int32 index;
	int32 count = paragraph.CountTextSpans();

	for (index = 0; index < count; index++) {
		const TextSpan& span = paragraph.TextSpanAtIndex(index);
		if (textOffset - span.CountChars() < 0)
			return span.ClickMessage();
		textOffset -= span.CountChars();
	}

	return NULL;
}


BCursor
TextDocument::CursorAt(int32 textOffset) const
{
	int32 paragraphOffset;
	const Paragraph& paragraph = ParagraphAt(textOffset, paragraphOffset);

	textOffset -= paragraphOffset;
	int32 index;
	int32 count = paragraph.CountTextSpans();

	for (index = 0; index < count; index++) {
		const TextSpan& span = paragraph.TextSpanAtIndex(index);
		if (textOffset - span.CountChars() < 0)
			return span.Cursor();
		textOffset -= span.CountChars();
	}

	return BCursor((BMessage*)NULL);
}


const ParagraphStyle&
TextDocument::ParagraphStyleAt(int32 textOffset) const
{
	int32 paragraphOffset;
	return ParagraphAt(textOffset, paragraphOffset).Style();
}


// #pragma mark -


int32
TextDocument::CountParagraphs() const
{
	return fParagraphs.size();
}


const Paragraph&
TextDocument::ParagraphAtIndex(int32 index) const
{
	return fParagraphs[index];
}


int32
TextDocument::ParagraphIndexFor(int32 textOffset, int32& paragraphOffset) const
{
	// TODO: Could binary search the Paragraphs if they were wrapped in classes
	// that knew there text offset in the document.
	int32 textLength = 0;
	paragraphOffset = 0;
	int32 count = fParagraphs.size();
	for (int32 i = 0; i < count; i++) {
		const Paragraph& paragraph = fParagraphs[i];
		int32 paragraphLength = paragraph.Length();
		textLength += paragraphLength;
		if (textLength > textOffset
			|| (i == count - 1 && textLength == textOffset)) {
			return i;
		}
		paragraphOffset += paragraphLength;
	}
	return -1;
}


const Paragraph&
TextDocument::ParagraphAt(int32 textOffset, int32& paragraphOffset) const
{
	int32 index = ParagraphIndexFor(textOffset, paragraphOffset);
	if (index >= 0)
		return fParagraphs[index];

	return fEmptyLastParagraph;
}


const Paragraph&
TextDocument::ParagraphAt(int32 index) const
{
	if (index >= 0 && index < static_cast<int32>(fParagraphs.size()))
		return fParagraphs[index];
	return fEmptyLastParagraph;
}


bool
TextDocument::Append(const Paragraph& paragraph)
{
	try {
		fParagraphs.push_back(paragraph);
	}
	catch (std::bad_alloc& ba) {
		fprintf(stderr, "bad_alloc when adding a paragraph to a text "
			"document\n");
		return false;
	}
	return true;
}


int32
TextDocument::Length() const
{
	// TODO: Could be O(1) if the Paragraphs were wrapped in classes that
	// knew their text offset in the document.
	int32 textLength = 0;
	int32 count = fParagraphs.size();
	for (int32 i = 0; i < count; i++) {
		const Paragraph& paragraph = fParagraphs[i];
		textLength += paragraph.Length();
	}
	return textLength;
}


BString
TextDocument::Text() const
{
	return Text(0, Length());
}


BString
TextDocument::Text(int32 start, int32 length) const
{
	if (start < 0)
		start = 0;

	BString text;

	int32 count = fParagraphs.size();
	for (int32 i = 0; i < count; i++) {
		const Paragraph& paragraph = fParagraphs[i];
		int32 paragraphLength = paragraph.Length();
		if (paragraphLength == 0)
			continue;
		if (start > paragraphLength) {
			// Skip paragraph if its before start
			start -= paragraphLength;
			continue;
		}

		// Remaining paragraph length after start
		paragraphLength -= start;
		int32 copyLength = std::min(paragraphLength, length);

		text << paragraph.Text(start, copyLength);

		length -= copyLength;
		if (length == 0)
			break;

		// Next paragraph is copied from its beginning
		start = 0;
	}

	return text;
}


TextDocumentRef
TextDocument::SubDocument(int32 start, int32 length) const
{
	TextDocumentRef result(new(std::nothrow) TextDocument(
		fDefaultCharacterStyle, fEmptyLastParagraph.Style()), true);

	if (!result.IsSet())
		return result;

	if (start < 0)
		start = 0;

	int32 count = fParagraphs.size();
	for (int32 i = 0; i < count; i++) {
		const Paragraph& paragraph = fParagraphs[i];
		int32 paragraphLength = paragraph.Length();
		if (paragraphLength == 0)
			continue;
		if (start > paragraphLength) {
			// Skip paragraph if its before start
			start -= paragraphLength;
			continue;
		}

		// Remaining paragraph length after start
		paragraphLength -= start;
		int32 copyLength = std::min(paragraphLength, length);

		result->Append(paragraph.SubParagraph(start, copyLength));

		length -= copyLength;
		if (length == 0)
			break;

		// Next paragraph is copied from its beginning
		start = 0;
	}

	return result;
}


// #pragma mark -


void
TextDocument::PrintToStream() const
{
	int32 paragraphCount = fParagraphs.size();
	if (paragraphCount == 0) {
		printf("<document/>\n");
		return;
	}
	printf("<document>\n");
	for (int32 i = 0; i < paragraphCount; i++) {
		fParagraphs[i].PrintToStream();
	}
	printf("</document>\n");
}


/*static*/ TextDocumentRef
TextDocument::NormalizeText(const BString& text,
	CharacterStyle characterStyle, ParagraphStyle paragraphStyle)
{
	TextDocumentRef document(new(std::nothrow) TextDocument(characterStyle,
			paragraphStyle), true);
	if (!document.IsSet())
		throw B_NO_MEMORY;

	Paragraph paragraph(paragraphStyle);

	// Append TextSpans, splitting 'text' into Paragraphs at line breaks.
	int32 length = text.CountChars();
	int32 chunkStart = 0;
	while (chunkStart < length) {
		int32 chunkEnd = text.FindFirst('\n', chunkStart);

		if (chunkEnd == B_ERROR)
			chunkEnd = length;
		else
			chunkEnd++; // the paragraph includes the `\n`

		BString chunk;
		text.CopyCharsInto(chunk, chunkStart, chunkEnd - chunkStart);
		TextSpan span(chunk, characterStyle);

		if (!paragraph.Append(span))
			throw B_NO_MEMORY;
		if (paragraph.Length() > 0 && !document->Append(paragraph))
			throw B_NO_MEMORY;

		paragraph = Paragraph(paragraphStyle);
		chunkStart = chunkEnd;
	}

	return document;
}


// #pragma mark -


bool
TextDocument::AddListener(TextListenerRef listener)
{
	try {
		fTextListeners.push_back(listener);
	}
	catch (std::bad_alloc& ba) {
		fprintf(stderr, "bad_alloc when adding a listener to a text "
			"document\n");
		return false;
	}
	return true;
}


bool
TextDocument::RemoveListener(TextListenerRef listener)
{
	fTextListeners.erase(std::remove(fTextListeners.begin(), fTextListeners.end(),
		listener), fTextListeners.end());
	return true;
}


bool
TextDocument::AddUndoListener(UndoableEditListenerRef listener)
{
	try {
		fUndoListeners.push_back(listener);
	}
	catch (std::bad_alloc& ba) {
		fprintf(stderr, "bad_alloc when adding an undo listener to a text "
			"document\n");
		return false;
	}
	return true;
}


bool
TextDocument::RemoveUndoListener(UndoableEditListenerRef listener)
{
	fUndoListeners.erase(std::remove(fUndoListeners.begin(), fUndoListeners.end(),
		listener), fUndoListeners.end());
	return true;
}


// #pragma mark - private


status_t
TextDocument::_Insert(int32 textOffset, TextDocumentRef document,
	int32& index, int32& paragraphCount)
{
	int32 paragraphOffset;
	index = ParagraphIndexFor(textOffset, paragraphOffset);
	if (index < 0)
		return B_BAD_VALUE;

	if (document->Length() == 0)
		return B_OK;

	textOffset -= paragraphOffset;

	bool hasLineBreaks;
	if (document->CountParagraphs() > 1) {
		hasLineBreaks = true;
	} else {
		const Paragraph& paragraph = document->ParagraphAt(0);
		hasLineBreaks = paragraph.EndsWith("\n");
	}

	if (hasLineBreaks) {
		// Split paragraph at textOffset
		Paragraph paragraph1(ParagraphAt(index).Style());
		Paragraph paragraph2(document->ParagraphAt(
			document->CountParagraphs() - 1).Style());
		{
			const Paragraph& paragraphAtIndex = ParagraphAt(index);
			int32 spanCount = paragraphAtIndex.CountTextSpans();
			for (int32 i = 0; i < spanCount; i++) {
				const TextSpan& span = paragraphAtIndex.TextSpanAtIndex(i);
				int32 spanLength = span.CountChars();
				if (textOffset >= spanLength) {
					if (!paragraph1.Append(span))
						return B_NO_MEMORY;
					textOffset -= spanLength;
				} else if (textOffset > 0) {
					if (!paragraph1.Append(
							span.SubSpan(0, textOffset))
						|| !paragraph2.Append(
							span.SubSpan(textOffset,
								spanLength - textOffset))) {
						return B_NO_MEMORY;
					}
					textOffset = 0;
				} else {
					if (!paragraph2.Append(span))
						return B_NO_MEMORY;
				}
			}
		}

		fParagraphs.erase(fParagraphs.begin() + index);

		// Append first paragraph in other document to first part of
		// paragraph at insert position
		{
			const Paragraph& otherParagraph = document->ParagraphAt(0);
			int32 spanCount = otherParagraph.CountTextSpans();
			for (int32 i = 0; i < spanCount; i++) {
				const TextSpan& span = otherParagraph.TextSpanAtIndex(i);
				// TODO: Import/map CharacterStyles
				if (!paragraph1.Append(span))
					return B_NO_MEMORY;
			}
		}

		// Insert the first paragraph-part again to the document
		try {
			fParagraphs.insert(fParagraphs.begin() + index, paragraph1);
		}
		catch (std::bad_alloc& ba) {
			return B_NO_MEMORY;
		}
		paragraphCount++;

		// Insert the other document's paragraph save for the last one
		for (int32 i = 1; i < document->CountParagraphs() - 1; i++) {
			const Paragraph& otherParagraph = document->ParagraphAt(i);
			// TODO: Import/map CharacterStyles and ParagraphStyle
			index++;
			try {
				fParagraphs.insert(fParagraphs.begin() + index, otherParagraph);
			}
			catch (std::bad_alloc& ba) {
				return B_NO_MEMORY;
			}
			paragraphCount++;
		}

		int32 lastIndex = document->CountParagraphs() - 1;
		if (lastIndex > 0) {
			const Paragraph& otherParagraph = document->ParagraphAt(lastIndex);
			if (otherParagraph.EndsWith("\n")) {
				// TODO: Import/map CharacterStyles and ParagraphStyle
				index++;
				try {
					fParagraphs.insert(fParagraphs.begin() + index, otherParagraph);
				}
				catch (std::bad_alloc& ba) {
					return B_NO_MEMORY;
				}
			} else {
				int32 spanCount = otherParagraph.CountTextSpans();
				for (int32 i = 0; i < spanCount; i++) {
					const TextSpan& span = otherParagraph.TextSpanAtIndex(i);
					// TODO: Import/map CharacterStyles
					if (!paragraph2.Prepend(span))
						return B_NO_MEMORY;
				}
			}
		}

		// Insert back the second paragraph-part
		if (paragraph2.IsEmpty()) {
			// Make sure Paragraph has at least one TextSpan, even
			// if its empty. This handles the case of inserting a
			// line-break at the end of the document. It than needs to
			// have a new, empty paragraph at the end.
			const int32 indexLastSpan = paragraph1.CountTextSpans() - 1;
			const TextSpan& span = paragraph1.TextSpanAtIndex(indexLastSpan);
			if (!paragraph2.Append(TextSpan("", span.Style())))
				return B_NO_MEMORY;
		}

		index++;
		try {
			fParagraphs.insert(fParagraphs.begin() + index, paragraph2);
		}
		catch (std::bad_alloc& ba) {
			return B_NO_MEMORY;
		}

		paragraphCount++;
	} else {
		Paragraph paragraph(ParagraphAt(index));
		const Paragraph& otherParagraph = document->ParagraphAt(0);

		int32 spanCount = otherParagraph.CountTextSpans();
		for (int32 i = 0; i < spanCount; i++) {
			const TextSpan& span = otherParagraph.TextSpanAtIndex(i);
			paragraph.Insert(textOffset, span);
			textOffset += span.CountChars();
		}

		fParagraphs[index] = paragraph;
		paragraphCount++;
	}

	return B_OK;
}


status_t
TextDocument::_Remove(int32 textOffset, int32 length, int32& index,
	int32& paragraphCount)
{
	if (length == 0)
		return B_OK;

	int32 paragraphOffset;
	index = ParagraphIndexFor(textOffset, paragraphOffset);
	if (index < 0)
		return B_BAD_VALUE;

	textOffset -= paragraphOffset;
	paragraphCount++;

	// The paragraph at the text offset remains, even if the offset is at
	// the beginning of that paragraph. The idea is that the selection start
	// stays visually in the same place. Therefore, the paragraph at that
	// offset has to keep the paragraph style from that paragraph.

	Paragraph resultParagraph(ParagraphAt(index));
	int32 paragraphLength = resultParagraph.Length();
	if (textOffset == 0 && length > paragraphLength) {
		length -= paragraphLength;
		paragraphLength = 0;
		resultParagraph.Clear();
	} else {
		int32 removeLength = std::min(length, paragraphLength - textOffset);
		resultParagraph.Remove(textOffset, removeLength);
		paragraphLength -= removeLength;
		length -= removeLength;
	}

	if (textOffset == paragraphLength && length == 0
		&& index + 1 < static_cast<int32>(fParagraphs.size())) {
		// Line break between paragraphs got removed. Shift the next
		// paragraph's text spans into the resulting one.

		const Paragraph& paragraph = ParagraphAt(index + 1);
		int32 spanCount = paragraph.CountTextSpans();
		for (int32 i = 0; i < spanCount; i++) {
			const TextSpan& span = paragraph.TextSpanAtIndex(i);
			resultParagraph.Append(span);
		}
		fParagraphs.erase(fParagraphs.begin() + (index + 1));
		paragraphCount++;
	}

	textOffset = 0;

	while (length > 0 && index + 1 < static_cast<int32>(fParagraphs.size())) {
		paragraphCount++;
		const Paragraph& paragraph = ParagraphAt(index + 1);
		paragraphLength = paragraph.Length();
		// Remove paragraph in any case. If some of it remains, the last
		// paragraph to remove is reached, and the remaining spans are
		// transfered to the result parahraph.
		if (length >= paragraphLength) {
			length -= paragraphLength;
			fParagraphs.erase(fParagraphs.begin() + index);
		} else {
			// Last paragraph reached
			int32 removedLength = std::min(length, paragraphLength);
			Paragraph newParagraph(paragraph);
			fParagraphs.erase(fParagraphs.begin() + (index + 1));

			if (!newParagraph.Remove(0, removedLength))
				return B_NO_MEMORY;

			// Transfer remaining spans to resultParagraph
			int32 spanCount = newParagraph.CountTextSpans();
			for (int32 i = 0; i < spanCount; i++) {
				const TextSpan& span = newParagraph.TextSpanAtIndex(i);
				resultParagraph.Append(span);
			}

			break;
		}
	}

	fParagraphs[index] = resultParagraph;

	return B_OK;
}


// #pragma mark - notifications


void
TextDocument::_NotifyTextChanging(TextChangingEvent& event) const
{
	// Copy listener list to have a stable list in case listeners
	// are added/removed from within the notification hook.
	std::vector<TextListenerRef> listeners(fTextListeners);

	int32 count = listeners.size();
	for (int32 i = 0; i < count; i++) {
		const TextListenerRef& listener = listeners[i];
		if (!listener.IsSet())
			continue;
		listener->TextChanging(event);
		if (event.IsCanceled())
			break;
	}
}


void
TextDocument::_NotifyTextChanged(const TextChangedEvent& event) const
{
	// Copy listener list to have a stable list in case listeners
	// are added/removed from within the notification hook.
	std::vector<TextListenerRef> listeners(fTextListeners);
	int32 count = listeners.size();
	for (int32 i = 0; i < count; i++) {
		const TextListenerRef& listener = listeners[i];
		if (!listener.IsSet())
			continue;
		listener->TextChanged(event);
	}
}


void
TextDocument::_NotifyUndoableEditHappened(const UndoableEditRef& edit) const
{
	// Copy listener list to have a stable list in case listeners
	// are added/removed from within the notification hook.
	std::vector<UndoableEditListenerRef> listeners(fUndoListeners);
	int32 count = listeners.size();
	for (int32 i = 0; i < count; i++) {
		const UndoableEditListenerRef& listener = listeners[i];
		if (!listener.IsSet())
			continue;
		listener->UndoableEditHappened(this, edit);
	}
}

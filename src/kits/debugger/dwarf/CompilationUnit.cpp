/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2013, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */


#include "CompilationUnit.h"

#include <new>

#include "DebugInfoEntries.h"
#include "TargetAddressRangeList.h"


struct CompilationUnit::File {
	BString		fileName;
	const char*	dirName;


	File(const char* fileName, const char* dirName)
		:
		fileName(fileName),
		dirName(dirName)
	{
	}
};


CompilationUnit::CompilationUnit(off_t headerOffset, off_t contentOffset,
	off_t totalSize, off_t abbreviationOffset, uint8 addressSize,
	bool isBigEndian, bool isDwarf64)
	:
	BaseUnit(headerOffset, contentOffset, totalSize, abbreviationOffset,
		addressSize, isBigEndian, isDwarf64),
	fUnitEntry(NULL),
	fAddressRanges(NULL),
	fDirectories(10),
	fFiles(10),
	fLineNumberProgram(addressSize, isBigEndian)
{
}


CompilationUnit::~CompilationUnit()
{
	SetAddressRanges(NULL);
}


void
CompilationUnit::SetUnitEntry(DIECompileUnitBase* entry)
{
	fUnitEntry = entry;
}


void
CompilationUnit::SetAddressRanges(TargetAddressRangeList* ranges)
{
	if (fAddressRanges != NULL)
		fAddressRanges->ReleaseReference();

	fAddressRanges = ranges;

	if (fAddressRanges != NULL)
		fAddressRanges->AcquireReference();
}


target_addr_t
CompilationUnit::AddressRangeBase() const
{
	return fUnitEntry != NULL ? fUnitEntry->LowPC() : 0;
}


bool
CompilationUnit::AddDirectory(const char* directory)
{
	BString* directoryString = new(std::nothrow) BString(directory);
	if (directoryString == NULL || directoryString->Length() == 0
		|| !fDirectories.AddItem(directoryString)) {
		delete directoryString;
		return false;
	}

	return true;
}


int32
CompilationUnit::CountDirectories() const
{
	return fDirectories.CountItems();
}


const char*
CompilationUnit::DirectoryAt(int32 index) const
{
	BString* directory = fDirectories.ItemAt(index);
	return directory != NULL ? directory->String() : NULL;
}


bool
CompilationUnit::AddFile(const char* fileName, int32 dirIndex)
{
	File* file = new(std::nothrow) File(fileName, DirectoryAt(dirIndex));
	if (file == NULL || file->fileName.Length() == 0 || !fFiles.AddItem(file)) {
		delete file;
		return false;
	}

	return true;
}


int32
CompilationUnit::CountFiles() const
{
	return fFiles.CountItems();
}


const char*
CompilationUnit::FileAt(int32 index, const char** _directory) const
{
	if (File* file = fFiles.ItemAt(index)) {
		if (_directory != NULL)
			*_directory = file->dirName;
		return file->fileName.String();
	}

	return NULL;
}


dwarf_unit_kind
CompilationUnit::Kind() const
{
	return dwarf_unit_kind_compilation;
}

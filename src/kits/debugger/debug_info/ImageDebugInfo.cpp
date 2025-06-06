/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2010-2017, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */

#include "ImageDebugInfo.h"

#include <new>

#include "DebuggerInterface.h"
#include "FunctionDebugInfo.h"
#include "FunctionInstance.h"
#include "SpecificImageDebugInfo.h"
#include "SymbolInfo.h"


ImageDebugInfo::ImageDebugInfo(const ImageInfo& imageInfo)
	:
	fImageInfo(imageInfo),
	fMainFunction(NULL)
{
}


ImageDebugInfo::~ImageDebugInfo()
{
	for (int32 i = 0; FunctionInstance* function = fFunctions.ItemAt(i); i++)
		function->ReleaseReference();

	for (int32 i = 0; SpecificImageDebugInfo* info = fSpecificInfos.ItemAt(i);
			i++) {
		info->ReleaseReference();
	}
}


bool
ImageDebugInfo::AddSpecificInfo(SpecificImageDebugInfo* info)
{
	// NB: on success we take over the caller's reference to the info object
	return fSpecificInfos.AddItem(info);
}


status_t
ImageDebugInfo::FinishInit(DebuggerInterface* interface)
{
	BObjectList<SymbolInfo, true> symbols(50);
	status_t error = interface->GetSymbolInfos(fImageInfo.TeamID(),
		fImageInfo.ImageID(), symbols);
	if (error != B_OK)
		return error;
	symbols.SortItems(&_CompareSymbols);

	// get functions -- get them from most expressive debug info first and add
	// missing functions from less expressive debug infos
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		BObjectList<FunctionDebugInfo> functions;
		error = specificInfo->GetFunctions(symbols, functions);
		if (error != B_OK)
			return error;

		for (int32 k = 0; FunctionDebugInfo* function = functions.ItemAt(k);
				k++) {
			if (FunctionAtAddress(function->Address()) != NULL)
				continue;

			FunctionInstance* instance = new(std::nothrow) FunctionInstance(
				this, function);
			if (instance == NULL
				|| !fFunctions.BinaryInsert(instance, &_CompareFunctions)) {
				delete instance;
				error = B_NO_MEMORY;
				break;
			}

			if (function->IsMain())
				fMainFunction = instance;
		}

		// Remove references returned by the specific debug info -- the
		// FunctionInstance objects have references, now.
		for (int32 k = 0; FunctionDebugInfo* function = functions.ItemAt(k);
				k++) {
			function->ReleaseReference();
		}

		if (error != B_OK)
			return error;
	}

	return B_OK;
}


status_t
ImageDebugInfo::GetType(GlobalTypeCache* cache, const BString& name,
	const TypeLookupConstraints& constraints, Type*& _type)
{
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		status_t error = specificInfo->GetType(cache, name, constraints,
			_type);
		if (error == B_OK || error == B_NO_MEMORY)
			return error;
	}

	return B_ENTRY_NOT_FOUND;
}


bool
ImageDebugInfo::HasType(const BString& name,
	const TypeLookupConstraints& constraints) const
{
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		if (specificInfo->HasType(name, constraints))
			return true;
	}

	return false;
}


AddressSectionType
ImageDebugInfo::GetAddressSectionType(target_addr_t address) const
{
	AddressSectionType type = ADDRESS_SECTION_TYPE_UNKNOWN;
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		type = specificInfo->GetAddressSectionType(address);
		if (type != ADDRESS_SECTION_TYPE_UNKNOWN)
			break;
	}

	return type;
}


int32
ImageDebugInfo::CountFunctions() const
{
	return fFunctions.CountItems();
}


FunctionInstance*
ImageDebugInfo::FunctionAt(int32 index) const
{
	return fFunctions.ItemAt(index);
}


FunctionInstance*
ImageDebugInfo::FunctionAtAddress(target_addr_t address) const
{
	return fFunctions.BinarySearchByKey(address, &_CompareAddressFunction);
}


FunctionInstance*
ImageDebugInfo::FunctionByName(const char* name) const
{
	// TODO: Not really optimal.
	for (int32 i = 0; FunctionInstance* function = fFunctions.ItemAt(i); i++) {
		if (function->Name() == name)
			return function;
	}

	return NULL;
}


status_t
ImageDebugInfo::AddSourceCodeInfo(LocatableFile* file,
	FileSourceCode* sourceCode) const
{
	bool addedAny = false;
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		status_t error = specificInfo->AddSourceCodeInfo(file, sourceCode);
		if (error == B_NO_MEMORY)
			return error;
		addedAny |= error == B_OK;
	}

	return addedAny ? B_OK : B_ENTRY_NOT_FOUND;
}


/*static*/ int
ImageDebugInfo::_CompareFunctions(const FunctionInstance* a,
	const FunctionInstance* b)
{
	return a->Address() < b->Address()
		? -1 : (a->Address() == b->Address() ? 0 : 1);
}


/*static*/ int
ImageDebugInfo::_CompareAddressFunction(const target_addr_t* address,
	const FunctionInstance* function)
{
	if (*address < function->Address())
		return -1;
	return *address < function->Address() + function->Size() ? 0 : 1;
}


/*static*/ int
ImageDebugInfo::_CompareSymbols(const SymbolInfo* a, const SymbolInfo* b)
{
	return a->Address() < b->Address()
		? -1 : (a->Address() == b->Address() ? 0 : 1);
}

/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2010-2018, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */
#ifndef DWARF_IMAGE_DEBUG_INFO_H
#define DWARF_IMAGE_DEBUG_INFO_H


#include <Locker.h>

#include <util/OpenHashTable.h>

#include "AddressSectionTypes.h"
#include "ImageInfo.h"
#include "SpecificImageDebugInfo.h"
#include "Type.h"


class Architecture;
class CompilationUnit;
class DebuggerInterface;
class DIENamespace;
class DIESubprogram;
class DIEType;
class DwarfFunctionDebugInfo;
class DwarfStackFrameDebugInfo;
class DwarfFile;
class ElfSegment;
class FileManager;
class FileSourceCode;
class FunctionID;
class FunctionInstance;
class GlobalTypeCache;
class GlobalTypeLookup;
class LocatableFile;
class SourceCode;
class TeamFunctionSourceInformation;


class DwarfImageDebugInfo : public SpecificImageDebugInfo {
public:
								DwarfImageDebugInfo(const ImageInfo& imageInfo,
									DebuggerInterface* interface,
									Architecture* architecture,
									FileManager* fileManager,
									GlobalTypeLookup* typeLookup,
									GlobalTypeCache* typeCache,
									TeamFunctionSourceInformation* sourceInfo,
									DwarfFile* file);
	virtual						~DwarfImageDebugInfo();

			status_t			Init();

			target_addr_t		RelocationDelta() const
									{ return fRelocationDelta; }

	virtual	status_t			GetFunctions(
									const BObjectList<SymbolInfo, true>& symbols,
									BObjectList<FunctionDebugInfo>& functions);
	virtual	status_t			GetType(GlobalTypeCache* cache,
									const BString& name,
									const TypeLookupConstraints& constraints,
									Type*& _type);
	virtual	bool				HasType(const BString& name,
									const TypeLookupConstraints& constraints)
									const;

	virtual AddressSectionType	GetAddressSectionType(target_addr_t address);

	virtual	status_t			CreateFrame(Image* image,
									FunctionInstance* functionInstance,
									CpuState* cpuState,
									bool getFullFrameInfo,
									ReturnValueInfoList* returnValueInfos,
									StackFrame*& _frame,
									CpuState*& _previousCpuState);
	virtual	status_t			GetStatement(FunctionDebugInfo* function,
									target_addr_t address,
									Statement*& _statement);
	virtual	status_t			GetStatementAtSourceLocation(
									FunctionDebugInfo* function,
									const SourceLocation& sourceLocation,
									Statement*& _statement);

	virtual	status_t			GetSourceLanguage(FunctionDebugInfo* function,
									SourceLanguage*& _language);

	virtual	ssize_t				ReadCode(target_addr_t address, void* buffer,
									size_t size);

	virtual	status_t			AddSourceCodeInfo(LocatableFile* file,
									FileSourceCode* sourceCode);

private:
			struct BasicTargetInterface;
			struct UnwindTargetInterface;
			struct EntryListWrapper;

			struct TypeNameKey;
			struct TypeNameEntry;
			struct TypeNameEntryHashDefinition;

			typedef BOpenHashTable<TypeNameEntryHashDefinition>
				TypeNameTable;

			struct TypeEntryInfo;
			typedef BObjectList<TypeEntryInfo, true> TypeEntryList;

private:
			status_t 			_AddSourceCodeInfo(CompilationUnit* unit,
									FileSourceCode* sourceCode,
									int32 fileIndex);
			int32				_GetSourceFileIndex(CompilationUnit* unit,
									LocatableFile* sourceFile) const;

			status_t			_CreateLocalVariables(CompilationUnit* unit,
									StackFrame* frame, FunctionID* functionID,
									DwarfStackFrameDebugInfo& factory,
									target_addr_t instructionPointer,
									target_addr_t lowPC,
									const EntryListWrapper& variableEntries,
									const EntryListWrapper& blockEntries);

			status_t			_CreateReturnValues(
									ReturnValueInfoList* returnValueInfos,
									Image* image,
									StackFrame* frame,
									DwarfStackFrameDebugInfo& factory);

			bool				_EvaluateBaseTypeConstraints(DIEType* type,
									const TypeLookupConstraints& constraints)
									const;

			status_t			_RecursiveTraverseNamespaceForFunctions(
									DIENamespace* nsEntry,
									CompilationUnit* unit,
									BObjectList<FunctionDebugInfo>& functions);
			status_t			_AddFunction(DIESubprogram* subprogramEntry,
									CompilationUnit* unit,
									BObjectList<FunctionDebugInfo>& functions);

			status_t			_BuildTypeNameTable();
			status_t			_RecursiveAddTypeNames(DIEType* type,
									CompilationUnit* unit);
			status_t			_RecursiveTraverseNamespaceForTypes(
									DIENamespace* nsEntry,
									CompilationUnit* unit);

private:
			BLocker				fLock;
			ImageInfo			fImageInfo;
			DebuggerInterface*	fDebuggerInterface;
			Architecture*		fArchitecture;
			FileManager*		fFileManager;
			GlobalTypeLookup*	fTypeLookup;
			GlobalTypeCache*	fTypeCache;
			TeamFunctionSourceInformation* fSourceInfo;
			TypeNameTable*		fTypeNameTable;
			DwarfFile*			fFile;
			ElfSegment*			fTextSegment;
			target_addr_t		fRelocationDelta;
			target_addr_t		fTextSectionStart;
			target_addr_t		fTextSectionEnd;
			target_addr_t		fPLTSectionStart;
			target_addr_t		fPLTSectionEnd;
};


#endif	// DWARF_IMAGE_DEBUG_INFO_H

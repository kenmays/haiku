/*
 * Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2010-2016, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */

#include "LocalDebuggerInterface.h"

#include <new>

#include <stdio.h>

#include <Locker.h>

#include <AutoLocker.h>
#include <memory_private.h>
#include <OS.h>
#include <system_info.h>
#include <util/DoublyLinkedList.h>
#include <util/KMessage.h>

#include "debug_utils.h"

#include "ArchitectureX86.h"
#include "ArchitectureX8664.h"
#include "AreaInfo.h"
#include "AutoDeleter.h"
#include "CpuState.h"
#include "DebugEvent.h"
#include "ImageInfo.h"
#include "SemaphoreInfo.h"
#include "SymbolInfo.h"
#include "SystemInfo.h"
#include "TeamInfo.h"
#include "ThreadInfo.h"


// number of debug contexts the pool does initially create
static const int kInitialDebugContextCount = 3;

// maximum number of debug contexts in the pool
static const int kMaxDebugContextCount = 10;


// #pragma mark - LocalDebuggerInterface::DebugContext

struct LocalDebuggerInterface::DebugContext : debug_context,
		DoublyLinkedListLinkImpl<DebugContext> {
	DebugContext()
	{
		team = -1;
		nub_port = -1;
		reply_port = -1;
	}

	~DebugContext()
	{
		if (reply_port >= 0)
			destroy_debug_context(this);
	}

	status_t Init(team_id team, port_id nubPort)
	{
		return init_debug_context(this, team, nubPort);
	}

	void Close()
	{
		if (reply_port >= 0) {
			destroy_debug_context(this);
			team = -1;
			nub_port = -1;
			reply_port = -1;
		}
	}
};

// #pragma mark - LocalDebuggerInterface::DebugContextPool

struct LocalDebuggerInterface::DebugContextPool {
	DebugContextPool(team_id team, port_id nubPort)
		:
		fLock("debug context pool"),
		fTeam(team),
		fNubPort(nubPort),
		fBlockSem(-1),
		fContextCount(0),
		fWaiterCount(0),
		fClosed(false)
	{
	}

	~DebugContextPool()
	{
		AutoLocker<BLocker> locker(fLock);

		while (DebugContext* context = fFreeContexts.RemoveHead())
			delete context;

		if (fBlockSem >= 0)
			delete_sem(fBlockSem);
	}

	status_t Init()
	{
		status_t error = fLock.InitCheck();
		if (error != B_OK)
			return error;

		fBlockSem = create_sem(0, "debug context pool block");
		if (fBlockSem < 0)
			return fBlockSem;

		for (int i = 0; i < kInitialDebugContextCount; i++) {
			DebugContext* context;
			error = _CreateDebugContext(context);
			if (error != B_OK)
				return error;

			fFreeContexts.Add(context);
		}

		return B_OK;
	}

	void Close()
	{
		AutoLocker<BLocker> locker(fLock);
		fClosed = true;

		for (DebugContextList::Iterator it = fFreeContexts.GetIterator();
				DebugContext* context = it.Next();) {
			context->Close();
		}

		for (DebugContextList::Iterator it = fUsedContexts.GetIterator();
				DebugContext* context = it.Next();) {
			context->Close();
		}
	}

	DebugContext* GetContext()
	{
		AutoLocker<BLocker> locker(fLock);
		DebugContext* context = fFreeContexts.RemoveHead();

		if (context == NULL) {
			if (fContextCount >= kMaxDebugContextCount
				|| _CreateDebugContext(context) != B_OK) {
				// wait for a free context
				while (context == NULL) {
					fWaiterCount++;
					locker.Unlock();
					while (acquire_sem(fBlockSem) != B_OK);
					locker.Lock();
					context = fFreeContexts.RemoveHead();
				}
			}
		}

		fUsedContexts.Add(context);

		return context;
	}

	void PutContext(DebugContext* context)
	{
		AutoLocker<BLocker> locker(fLock);
		fUsedContexts.Remove(context);
		fFreeContexts.Add(context);

		if (fWaiterCount > 0)
			release_sem(fBlockSem);
	}

private:
	typedef DoublyLinkedList<DebugContext> DebugContextList;

private:
	status_t _CreateDebugContext(DebugContext*& _context)
	{
		DebugContext* context = new(std::nothrow) DebugContext;
		if (context == NULL)
			return B_NO_MEMORY;

		if (!fClosed) {
			status_t error = context->Init(fTeam, fNubPort);
			if (error != B_OK) {
				delete context;
				return error;
			}
		}

		fContextCount++;

		_context = context;
		return B_OK;
	}

private:
	BLocker				fLock;
	team_id				fTeam;
	port_id				fNubPort;
	sem_id				fBlockSem;
	int32				fContextCount;
	int32				fWaiterCount;
	DebugContextList	fFreeContexts;
	DebugContextList	fUsedContexts;
	bool				fClosed;
};


struct LocalDebuggerInterface::DebugContextGetter {
	DebugContextGetter(DebugContextPool* pool)
		:
		fPool(pool),
		fContext(pool->GetContext())
	{
	}

	~DebugContextGetter()
	{
		fPool->PutContext(fContext);
	}

	DebugContext* Context() const
	{
		return fContext;
	}

private:
	DebugContextPool*	fPool;
	DebugContext*		fContext;
};

// #pragma mark - LocalDebuggerInterface

LocalDebuggerInterface::LocalDebuggerInterface(team_id team)
	:
	DebuggerInterface(),
	fTeamID(team),
	fDebuggerPort(-1),
	fNubPort(-1),
	fDebugContextPool(NULL),
	fArchitecture(NULL)
{
}


LocalDebuggerInterface::~LocalDebuggerInterface()
{
	if (fArchitecture != NULL)
		fArchitecture->ReleaseReference();

	Close(false);

	delete fDebugContextPool;
}


status_t
LocalDebuggerInterface::Init()
{
	// create the architecture
#if defined(ARCH_x86)
	fArchitecture = new(std::nothrow) ArchitectureX86(this);
#elif defined(ARCH_x86_64)
	fArchitecture = new(std::nothrow) ArchitectureX8664(this);
#else
	return B_UNSUPPORTED;
#endif

	if (fArchitecture == NULL)
		return B_NO_MEMORY;

	status_t error = fArchitecture->Init();
	if (error != B_OK)
		return error;

	// create debugger port
	char buffer[128];
	snprintf(buffer, sizeof(buffer), "team %" B_PRId32 " debugger", fTeamID);
	fDebuggerPort = create_port(100, buffer);
	if (fDebuggerPort < 0)
		return fDebuggerPort;

	// install as team debugger
	fNubPort = install_team_debugger(fTeamID, fDebuggerPort);
	if (fNubPort < 0)
		return fNubPort;

	error = __start_watching_system(fTeamID, B_WATCH_SYSTEM_THREAD_PROPERTIES,
		fDebuggerPort, 0);
	if (error != B_OK)
		return error;

	// create debug context pool
	fDebugContextPool = new(std::nothrow) DebugContextPool(fTeamID, fNubPort);
	if (fDebugContextPool == NULL)
		return B_NO_MEMORY;

	error = fDebugContextPool->Init();
	if (error != B_OK)
		return error;

	return B_OK;
}


void
LocalDebuggerInterface::Close(bool killTeam)
{
	if (killTeam)
		kill_team(fTeamID);
	else if (fNubPort >= 0)
		remove_team_debugger(fTeamID);

	if (fDebuggerPort >= 0) {
		__stop_watching_system(fTeamID, B_WATCH_SYSTEM_THREAD_PROPERTIES,
			fDebuggerPort, 0);
		delete_port(fDebuggerPort);
	}

	fNubPort = -1;
	fDebuggerPort = -1;
}


bool
LocalDebuggerInterface::Connected() const
{
	return fNubPort >= 0;
}


team_id
LocalDebuggerInterface::TeamID() const
{
	return fTeamID;
}


Architecture*
LocalDebuggerInterface::GetArchitecture() const
{
	return fArchitecture;
}


status_t
LocalDebuggerInterface::GetNextDebugEvent(DebugEvent*& _event)
{
	while (true) {
		char buffer[2048];
		int32 messageCode;
		ssize_t size = read_port(fDebuggerPort, &messageCode, buffer,
			sizeof(buffer));
		if (size < 0) {
			if (size == B_INTERRUPTED)
				continue;

			return size;
		}

		if (messageCode <= B_DEBUGGER_MESSAGE_HANDED_OVER) {
 			debug_debugger_message_data message;
			memcpy(&message, buffer, size);
			if (message.origin.team != fTeamID)
				continue;

			bool ignore = false;
			status_t error = _CreateDebugEvent(messageCode, message, ignore,
				_event);
			if (error != B_OK)
				return error;

			if (ignore) {
				if (message.origin.thread >= 0 && message.origin.nub_port >= 0)
					error = continue_thread(message.origin.nub_port,
						message.origin.thread);
				if (error != B_OK)
					return error;
				continue;
			}

			return B_OK;
		}

		KMessage message;
		size = message.SetTo(buffer);
		if (size != B_OK)
			return size;
		return _GetNextSystemWatchEvent(_event, message);
	}

	return B_OK;
}


status_t
LocalDebuggerInterface::SetTeamDebuggingFlags(uint32 flags)
{
	return set_team_debugging_flags(fNubPort, flags);
}


status_t
LocalDebuggerInterface::ContinueThread(thread_id thread)
{
	return continue_thread(fNubPort, thread);
}


status_t
LocalDebuggerInterface::StopThread(thread_id thread)
{
	return debug_thread(thread);
}


status_t
LocalDebuggerInterface::SingleStepThread(thread_id thread)
{
	debug_nub_continue_thread continueMessage;
	continueMessage.thread = thread;
	continueMessage.handle_event = B_THREAD_DEBUG_HANDLE_EVENT;
	continueMessage.single_step = true;

	return write_port(fNubPort, B_DEBUG_MESSAGE_CONTINUE_THREAD,
		&continueMessage, sizeof(continueMessage));
}


status_t
LocalDebuggerInterface::InstallBreakpoint(target_addr_t address)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_set_breakpoint message;
	message.reply_port = contextGetter.Context()->reply_port;
	message.address = (void*)(addr_t)address;

	debug_nub_set_breakpoint_reply reply;

	status_t error = send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_SET_BREAKPOINT, &message, sizeof(message), &reply,
		sizeof(reply));
	return error == B_OK ? reply.error : error;
}


status_t
LocalDebuggerInterface::UninstallBreakpoint(target_addr_t address)
{
	debug_nub_clear_breakpoint message;
	message.address = (void*)(addr_t)address;

	return write_port(fNubPort, B_DEBUG_MESSAGE_CLEAR_BREAKPOINT,
		&message, sizeof(message));
}


status_t
LocalDebuggerInterface::InstallWatchpoint(target_addr_t address, uint32 type,
	int32 length)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_set_watchpoint message;
	message.reply_port = contextGetter.Context()->reply_port;
	message.address = (void*)(addr_t)address;
	message.type = type;
	message.length = length;

	debug_nub_set_watchpoint_reply reply;

	status_t error = send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_SET_WATCHPOINT, &message, sizeof(message), &reply,
		sizeof(reply));
	return error == B_OK ? reply.error : error;
}


status_t
LocalDebuggerInterface::UninstallWatchpoint(target_addr_t address)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_clear_watchpoint message;
	message.address = (void*)(addr_t)address;

	return write_port(fNubPort, B_DEBUG_MESSAGE_CLEAR_WATCHPOINT,
		&message, sizeof(message));
}


status_t
LocalDebuggerInterface::GetSystemInfo(SystemInfo& info)
{
	system_info sysInfo;
	status_t result = get_system_info(&sysInfo);
	if (result != B_OK)
		return result;

	utsname name;
	result = uname(&name);
	if (result != B_OK)
		return result;

	info.SetTo(fTeamID, sysInfo, name);
	return B_OK;
}


status_t
LocalDebuggerInterface::GetTeamInfo(TeamInfo& info)
{
	team_info teamInfo;
	status_t result = get_team_info(fTeamID, &teamInfo);
	if (result != B_OK)
		return result;

	info.SetTo(fTeamID, teamInfo);
	return B_OK;
}


status_t
LocalDebuggerInterface::GetThreadInfos(BObjectList<ThreadInfo, true>& infos)
{
	thread_info threadInfo;
	int32 cookie = 0;
	while (get_next_thread_info(fTeamID, &cookie, &threadInfo) == B_OK) {
		ThreadInfo* info = new(std::nothrow) ThreadInfo(threadInfo.team,
			threadInfo.thread, threadInfo.name);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
LocalDebuggerInterface::GetImageInfos(BObjectList<ImageInfo, true>& infos)
{
	// get the team's images
	image_info imageInfo;
	int32 cookie = 0;
	while (get_next_image_info(fTeamID, &cookie, &imageInfo) == B_OK) {
		ImageInfo* info = new(std::nothrow) ImageInfo(fTeamID, imageInfo.id,
			imageInfo.name, imageInfo.type, (addr_t)imageInfo.text,
			imageInfo.text_size, (addr_t)imageInfo.data, imageInfo.data_size);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
LocalDebuggerInterface::GetAreaInfos(BObjectList<AreaInfo, true>& infos)
{
	// get the team's areas
	area_info areaInfo;
	ssize_t cookie = 0;
	while (get_next_area_info(fTeamID, &cookie, &areaInfo) == B_OK) {
		AreaInfo* info = new(std::nothrow) AreaInfo(fTeamID, areaInfo.area,
			areaInfo.name, (addr_t)areaInfo.address, areaInfo.size,
			areaInfo.ram_size, areaInfo.lock, areaInfo.protection);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
LocalDebuggerInterface::GetSemaphoreInfos(BObjectList<SemaphoreInfo, true>& infos)
{
	// get the team's semaphores
	sem_info semInfo;
	int32 cookie = 0;
	while (get_next_sem_info(fTeamID, &cookie, &semInfo) == B_OK) {
		SemaphoreInfo* info = new(std::nothrow) SemaphoreInfo(fTeamID,
			semInfo.sem, semInfo.name, semInfo.count, semInfo.latest_holder);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
LocalDebuggerInterface::GetSymbolInfos(team_id team, image_id image,
	BObjectList<SymbolInfo, true>& infos)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	// create a lookup context
	debug_symbol_lookup_context* lookupContext;
	status_t error = debug_create_symbol_lookup_context(contextGetter.Context(),
		image, &lookupContext);
	if (error != B_OK)
		return error;

	// create a symbol iterator
	debug_symbol_iterator* iterator;
	error = debug_create_image_symbol_iterator(
		lookupContext, image, &iterator);
	if (error != B_OK) {
		debug_delete_symbol_lookup_context(lookupContext);
		return error;
	}

	// get the symbols
	char name[1024];
	int32 type;
	void* address;
	size_t size;
	while (debug_next_image_symbol(iterator, name, sizeof(name), &type,
			&address, &size) == B_OK) {
		SymbolInfo* info = new(std::nothrow) SymbolInfo(
			(target_addr_t)(addr_t)address, size, type, name);
		if (info == NULL)
			break;
		if (!infos.AddItem(info)) {
			delete info;
			break;
		}
	}

	// delete the symbol iterator and lookup context
	debug_delete_symbol_iterator(iterator);
	debug_delete_symbol_lookup_context(lookupContext);

	return B_OK;
}


status_t
LocalDebuggerInterface::GetSymbolInfo(team_id team, image_id image, const char* name,
	int32 symbolType, SymbolInfo& info)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	// create a lookup context
	debug_symbol_lookup_context* lookupContext;
	status_t error = debug_create_symbol_lookup_context(contextGetter.Context(),
		image, &lookupContext);
	if (error != B_OK)
		return error;

	// try to get the symbol
	void* foundAddress;
	size_t foundSize;
	int32 foundType;
	error = debug_get_symbol(lookupContext, image, name, symbolType,
		&foundAddress, &foundSize, &foundType);
	if (error == B_OK) {
		info.SetTo((target_addr_t)(addr_t)foundAddress, foundSize, foundType,
			name);
	}

	// delete the lookup context
	debug_delete_symbol_lookup_context(lookupContext);

	return error;
}


status_t
LocalDebuggerInterface::GetThreadInfo(thread_id thread, ThreadInfo& info)
{
	thread_info threadInfo;
	status_t error = get_thread_info(thread, &threadInfo);
	if (error != B_OK)
		return error;

	info.SetTo(threadInfo.team, threadInfo.thread, threadInfo.name);
	return B_OK;
}


status_t
LocalDebuggerInterface::GetCpuState(thread_id thread, CpuState*& _state)
{
	debug_cpu_state debugState;
	status_t error = _GetDebugCpuState(thread, debugState);
	if (error != B_OK)
		return error;
	return fArchitecture->CreateCpuState(&debugState, sizeof(debug_cpu_state),
		_state);
}


status_t
LocalDebuggerInterface::SetCpuState(thread_id thread, const CpuState* state)
{
	debug_cpu_state debugState;
	status_t error = _GetDebugCpuState(thread, debugState);
	if (error != B_OK)
		return error;

	DebugContextGetter contextGetter(fDebugContextPool);

	error = state->UpdateDebugState(&debugState, sizeof(debugState));
	if (error != B_OK)
		return error;

	debug_nub_set_cpu_state message;
	message.thread = thread;

	memcpy(&message.cpu_state, &debugState, sizeof(debugState));

	return send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_SET_CPU_STATE, &message, sizeof(message), NULL,
		0);
}


status_t
LocalDebuggerInterface::GetCpuFeatures(uint32& flags)
{
	return fArchitecture->GetCpuFeatures(flags);
}


status_t
LocalDebuggerInterface::WriteCoreFile(const char* path)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_write_core_file_reply reply;

	debug_nub_write_core_file message;
	message.reply_port = contextGetter.Context()->reply_port;
	strlcpy(message.path, path, sizeof(message.path));

	status_t error = send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_WRITE_CORE_FILE, &message, sizeof(message), &reply,
		sizeof(reply));
	if (error == B_OK)
		error = reply.error;

	return error;
}


status_t
LocalDebuggerInterface::GetMemoryProperties(target_addr_t address,
	uint32& protection, uint32& locking)
{
	return get_memory_properties(fTeamID, (const void *)address,
		&protection, &locking);
}


ssize_t
LocalDebuggerInterface::ReadMemory(target_addr_t address, void* buffer, size_t size)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	return debug_read_memory(contextGetter.Context(),
		(const void*)(addr_t)address, buffer, size);
}


ssize_t
LocalDebuggerInterface::WriteMemory(target_addr_t address, void* buffer,
	size_t size)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	return debug_write_memory(contextGetter.Context(),
		(const void*)address, buffer, size);
}


status_t
LocalDebuggerInterface::_CreateDebugEvent(int32 messageCode,
	const debug_debugger_message_data& message, bool& _ignore,
	DebugEvent*& _event)
{
	DebugEvent* event = NULL;

	switch (messageCode) {
		case B_DEBUGGER_MESSAGE_THREAD_DEBUGGED:
			event = new(std::nothrow) ThreadDebuggedEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_DEBUGGER_CALL:
			event = new(std::nothrow) DebuggerCallEvent(message.origin.team,
				message.origin.thread,
				(target_addr_t)message.debugger_call.message);
			break;
		case B_DEBUGGER_MESSAGE_BREAKPOINT_HIT:
		{
			CpuState* state = NULL;
			status_t error = fArchitecture->CreateCpuState(
				&message.breakpoint_hit.cpu_state,
				sizeof(debug_cpu_state), state);
			if (error != B_OK)
				return error;

			event = new(std::nothrow) BreakpointHitEvent(message.origin.team,
				message.origin.thread, state);
			state->ReleaseReference();
			break;
		}
		case B_DEBUGGER_MESSAGE_WATCHPOINT_HIT:
		{
			CpuState* state = NULL;
			status_t error = fArchitecture->CreateCpuState(
				&message.watchpoint_hit.cpu_state,
				sizeof(debug_cpu_state), state);
			if (error != B_OK)
				return error;

			event = new(std::nothrow) WatchpointHitEvent(message.origin.team,
				message.origin.thread, state);
			state->ReleaseReference();
			break;
		}
		case B_DEBUGGER_MESSAGE_SINGLE_STEP:
		{
			CpuState* state = NULL;
			status_t error = fArchitecture->CreateCpuState(
				&message.single_step.cpu_state,
				sizeof(debug_cpu_state), state);
			if (error != B_OK)
				return error;

			event = new(std::nothrow) SingleStepEvent(message.origin.team,
				message.origin.thread, state);
			state->ReleaseReference();
			break;
		}
		case B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED:
			event = new(std::nothrow) ExceptionOccurredEvent(
				message.origin.team, message.origin.thread,
				message.exception_occurred.exception);
			break;
		case B_DEBUGGER_MESSAGE_TEAM_DELETED:
			if (message.origin.team != fTeamID) {
				_ignore = true;
				return B_OK;
			}
			event = new(std::nothrow) TeamDeletedEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_TEAM_EXEC:
			if (message.origin.team != fTeamID) {
				_ignore = true;
				return B_OK;
			}
			event = new(std::nothrow) TeamExecEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_THREAD_CREATED:
			event = new(std::nothrow) ThreadCreatedEvent(message.origin.team,
				message.origin.thread, message.thread_created.new_thread);
			break;
		case B_DEBUGGER_MESSAGE_THREAD_DELETED:
			event = new(std::nothrow) ThreadDeletedEvent(message.origin.team,
				message.origin.thread);
			break;
		case B_DEBUGGER_MESSAGE_IMAGE_CREATED:
		{
			const image_info& info = message.image_created.info;
			event = new(std::nothrow) ImageCreatedEvent(message.origin.team,
				message.origin.thread,
				ImageInfo(fTeamID, info.id, info.name, info.type,
					(addr_t)info.text, info.text_size, (addr_t)info.data,
					info.data_size));
			break;
		}
		case B_DEBUGGER_MESSAGE_IMAGE_DELETED:
		{
			const image_info& info = message.image_deleted.info;
			event = new(std::nothrow) ImageDeletedEvent(message.origin.team,
				message.origin.thread,
				ImageInfo(fTeamID, info.id, info.name, info.type,
					(addr_t)info.text, info.text_size, (addr_t)info.data,
					info.data_size));
			break;
		}
		case B_DEBUGGER_MESSAGE_POST_SYSCALL:
		{
			event = new(std::nothrow) PostSyscallEvent(message.origin.team,
				message.origin.thread,
				SyscallInfo(message.post_syscall.start_time,
					message.post_syscall.end_time,
					message.post_syscall.return_value,
					message.post_syscall.syscall, message.post_syscall.args));
			break;
		}
		case B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED:
		{
			event = new(std::nothrow) SignalReceivedEvent(message.origin.team,
				message.origin.thread,
				SignalInfo(message.signal_received.signal,
					message.signal_received.handler,
					message.signal_received.deadly));
			break;
		}
		default:
			printf("DebuggerInterface for team %" B_PRId32 ": unknown message "
				"from kernel: %" B_PRId32 "\n", fTeamID, messageCode);
			// fall through...
		case B_DEBUGGER_MESSAGE_TEAM_CREATED:
		case B_DEBUGGER_MESSAGE_PRE_SYSCALL:
		case B_DEBUGGER_MESSAGE_PROFILER_UPDATE:
		case B_DEBUGGER_MESSAGE_HANDED_OVER:
			_ignore = true;
			return B_OK;
	}

	if (event == NULL)
		return B_NO_MEMORY;

	if (message.origin.thread >= 0 && message.origin.nub_port >= 0)
		event->SetThreadStopped(true);

	_ignore = false;
	_event = event;

	return B_OK;
}


status_t
LocalDebuggerInterface::_GetNextSystemWatchEvent(DebugEvent*& _event,
	KMessage& message)
{
	status_t error = B_OK;
	if (message.What() != B_SYSTEM_OBJECT_UPDATE)
		return B_BAD_DATA;

	int32 opcode = 0;
	if (message.FindInt32("opcode", &opcode) != B_OK)
		return B_BAD_DATA;

	DebugEvent* event = NULL;
	switch (opcode)
	{
		case B_THREAD_NAME_CHANGED:
		{
			int32 threadID = -1;
			if (message.FindInt32("thread", &threadID) != B_OK)
				break;

			thread_info info;
			error = get_thread_info(threadID, &info);
			if (error != B_OK)
				break;

			event = new(std::nothrow) ThreadRenamedEvent(fTeamID,
				threadID, threadID, info.name);
			break;
		}

		default:
		{
			error = B_BAD_DATA;
			break;
		}
	}

	if (event != NULL)
		_event = event;

	return error;
}


status_t
LocalDebuggerInterface::_GetDebugCpuState(thread_id thread, debug_cpu_state& _state)
{
	DebugContextGetter contextGetter(fDebugContextPool);

	debug_nub_get_cpu_state message;
	message.reply_port = contextGetter.Context()->reply_port;
	message.thread = thread;

	debug_nub_get_cpu_state_reply reply;

	status_t error = send_debug_message(contextGetter.Context(),
		B_DEBUG_MESSAGE_GET_CPU_STATE, &message, sizeof(message), &reply,
		sizeof(reply));
	if (error != B_OK)
		return error;
	if (reply.error != B_OK)
		return reply.error;

	memcpy(&_state, &reply.cpu_state, sizeof(debug_cpu_state));

	return B_OK;
}

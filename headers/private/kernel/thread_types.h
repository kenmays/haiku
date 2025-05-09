/*
 * Copyright 2004-2016, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Thread definition and structures
 */
#ifndef _KERNEL_THREAD_TYPES_H
#define _KERNEL_THREAD_TYPES_H


#ifndef _ASSEMBLER

#include <pthread.h>

#include <arch/thread_types.h>
#include <condition_variable.h>
#include <heap.h>
#include <ksignal.h>
#include <lock.h>
#include <smp.h>
#include <thread_defs.h>
#include <timer.h>
#include <UserTimer.h>
#include <user_debugger.h>
#include <util/DoublyLinkedList.h>
#include <util/KernelReferenceable.h>
#include <util/list.h>

#include <SupportDefs.h>


enum additional_thread_state {
	THREAD_STATE_FREE_ON_RESCHED = 7, // free the thread structure upon reschedule
//	THREAD_STATE_BIRTH	// thread is being created
};

#define THREAD_MIN_SET_PRIORITY				B_LOWEST_ACTIVE_PRIORITY
#define THREAD_MAX_SET_PRIORITY				B_REAL_TIME_PRIORITY

enum team_state {
	TEAM_STATE_NORMAL,		// normal state
	TEAM_STATE_BIRTH,		// being constructed
	TEAM_STATE_SHUTDOWN,	// still lives, but is going down
	TEAM_STATE_DEATH		// only the Team object still exists, threads are
							// gone
};

#define	TEAM_FLAG_EXEC_DONE	0x01
	// team has executed exec*()
#define	TEAM_FLAG_DUMP_CORE	0x02
	// a core dump is in progress

typedef enum job_control_state {
	JOB_CONTROL_STATE_NONE,
	JOB_CONTROL_STATE_STOPPED,
	JOB_CONTROL_STATE_CONTINUED,
	JOB_CONTROL_STATE_DEAD
} job_control_state;


struct cpu_ent;
struct image;					// defined in image.c
struct io_context;
struct realtime_sem_context;	// defined in realtime_sem.cpp
struct select_info;
struct user_thread;				// defined in libroot/user_thread.h
struct VMAddressSpace;
struct user_mutex_context;		// defined in user_mutex.cpp
struct xsi_sem_context;			// defined in xsi_semaphore.cpp

namespace Scheduler {
	struct ThreadData;
}

namespace BKernel {
	struct Team;
	struct Thread;
	struct ProcessGroup;
}


struct thread_death_entry : DoublyLinkedListLinkImpl<thread_death_entry> {
	thread_id			thread;
	status_t			status;
};

typedef DoublyLinkedList<thread_death_entry> ThreadDeathEntryList;

struct team_loading_info {
	ConditionVariable	condition;
	status_t			result;		// the result of the loading
};

struct team_watcher {
	struct list_link	link;
	void				(*hook)(team_id team, void *data);
	void				*data;
};


#define MAX_DEAD_CHILDREN	32
	// this is a soft limit for the number of child death entries in a team


struct job_control_entry : DoublyLinkedListLinkImpl<job_control_entry> {
	job_control_state	state;		// current team job control state
	thread_id			thread;		// main thread ID == team ID
	uint16				signal;		// signal causing the current state
	bool				has_group_ref;
	uid_t				signaling_user;

	// valid while state != JOB_CONTROL_STATE_DEAD
	BKernel::Team*		team;

	// valid when state == JOB_CONTROL_STATE_DEAD
	pid_t				group_id;
	status_t			status;
	uint16				reason;		// reason for the team's demise, one of the
									// CLD_* values defined in <signal.h>
	bigtime_t			user_time;
	bigtime_t			kernel_time;

	job_control_entry();
	~job_control_entry();

	void InitDeadState();

	job_control_entry& operator=(const job_control_entry& other);
};

typedef DoublyLinkedList<job_control_entry> JobControlEntryList;

struct team_job_control_children {
	JobControlEntryList		entries;
};

struct team_dead_children : team_job_control_children {
	ConditionVariable	condition_variable;
	uint32				count;
	bigtime_t			kernel_time;
	bigtime_t			user_time;
};


struct team_death_entry {
	int32				remaining_threads;
	ConditionVariable	condition;
};


struct free_user_thread {
	struct free_user_thread*	next;
	struct user_thread*			thread;
};


class AssociatedDataOwner;

class AssociatedData : public BReferenceable,
	public DoublyLinkedListLinkImpl<AssociatedData> {
public:
								AssociatedData();
	virtual						~AssociatedData();

			AssociatedDataOwner* Owner() const
									{ return fOwner; }
			void				SetOwner(AssociatedDataOwner* owner)
									{ fOwner = owner; }

	virtual	void				OwnerDeleted(AssociatedDataOwner* owner);

private:
			AssociatedDataOwner* fOwner;
};


class AssociatedDataOwner {
public:
								AssociatedDataOwner();
								~AssociatedDataOwner();

			bool				AddData(AssociatedData* data);
			bool				RemoveData(AssociatedData* data);

			void				PrepareForDeletion();

private:
			typedef DoublyLinkedList<AssociatedData> DataList;

private:

			mutex				fLock;
			DataList			fList;
};


typedef int32 (*thread_entry_func)(thread_func, void *);


namespace BKernel {


struct GroupsArray : KernelReferenceable {
	int		count;
	gid_t	groups[];
};


template<typename IDType>
struct TeamThreadIteratorEntry
	: DoublyLinkedListLinkImpl<TeamThreadIteratorEntry<IDType> > {
	typedef IDType	id_type;
	typedef TeamThreadIteratorEntry<id_type> iterator_type;

	id_type	id;			// -1 for iterator entries, >= 0 for actual elements
	bool	visible;	// the entry is publicly visible
};


struct Thread : TeamThreadIteratorEntry<thread_id>, KernelReferenceable {
	int32			flags;			// summary of events relevant in interrupt
									// handlers (signals pending, user debugging
									// enabled, etc.)
	int64			serial_number;	// immutable after adding thread to hash
	Thread			*hash_next;		// protected by thread hash lock
	DoublyLinkedListLink<Thread> team_link; // protected by team lock and fLock
	char			name[B_OS_NAME_LENGTH];	// protected by fLock
	bool			going_to_suspend;	// protected by scheduler lock
	int32			priority;		// protected by scheduler lock
	int32			io_priority;	// protected by fLock
	int32			state;			// protected by scheduler lock
	struct cpu_ent	*cpu;			// protected by scheduler lock
	struct cpu_ent	*previous_cpu;	// protected by scheduler lock
	CPUSet			cpumask;
	int32			pinned_to_cpu;	// only accessed by this thread or in the
									// scheduler, when thread is not running
	spinlock		scheduler_lock;

	sigset_t		sig_block_mask;	// protected by team->signal_lock,
									// only modified by the thread itself
	sigset_t		sigsuspend_original_unblocked_mask;
		// non-0 after a return from _user_sigsuspend(), containing the inverted
		// original signal mask, reset in handle_signals(); only accessed by
		// this thread
	sigset_t		old_sig_block_mask;
		// the old sig_block_mask to be restored when returning to userland
		// when THREAD_FLAGS_OLD_SIGMASK is set

	ucontext_t*		user_signal_context;	// only accessed by this thread
	addr_t			signal_stack_base;		// only accessed by this thread
	size_t			signal_stack_size;		// only accessed by this thread
	bool			signal_stack_enabled;	// only accessed by this thread

	bool			in_kernel;		// protected by time_lock, only written by
									// this thread
	bool			has_yielded;	// protected by scheduler lock
	Scheduler::ThreadData*	scheduler_data; // protected by scheduler lock

	struct user_thread*	user_thread;	// write-protected by fLock, only
										// modified by the thread itself and
										// thus freely readable by it

	void			(*cancel_function)(int);

	struct {
		uint8		parameters[SYSCALL_RESTART_PARAMETER_SIZE];
	} syscall_restart;

	struct {
		status_t	status;				// current wait status
		uint32		flags;				// interrupable flags
		uint32		type;				// type of the object waited on
		const void*	object;				// pointer to the object waited on
		timer		unblock_timer;		// timer for block with timeout
	} wait;

	struct {
		sem_id		write_sem;	// acquired by writers before writing
		sem_id		read_sem;	// release by writers after writing, acquired
								// by this thread when reading
		thread_id	sender;
		int32		code;
		size_t		size;
		void*		buffer;
	} msg;	// write_sem/read_sem are protected by fLock when accessed by
			// others, the other fields are protected by write_sem/read_sem

	void			(*fault_handler)(void);
	jmp_buf			fault_handler_state;
	int16			page_faults_allowed;
	int16			page_fault_waits_allowed;

	BKernel::Team	*team;	// protected by team lock, thread lock, scheduler
							// lock, team_lock
	rw_spinlock		team_lock;

	struct {
		sem_id		sem;		// immutable after thread creation
		status_t	status;		// accessed only by this thread
		ThreadDeathEntryList waiters; // protected by fLock
	} exit;

	struct select_info *select_infos;	// protected by fLock

	struct thread_debug_info debug_info;

	// stack
	area_id			kernel_stack_area;	// immutable after thread creation
	addr_t			kernel_stack_base;	// immutable after thread creation
	addr_t			kernel_stack_top;	// immutable after thread creation
	area_id			user_stack_area;	// protected by thread lock
	addr_t			user_stack_base;	// protected by thread lock
	size_t			user_stack_size;	// protected by thread lock

	addr_t			user_local_storage;
		// usually allocated at the safe side of the stack
	int				kernel_errno;
		// kernel "errno" differs from its userspace alter ego

	// user_time, kernel_time, and last_time are only written by the thread
	// itself, so they can be read by the thread without lock. Holding the
	// scheduler lock and checking that the thread does not run also guarantees
	// that the times will not change.
	spinlock		time_lock;
	bigtime_t		user_time;			// protected by time_lock
	bigtime_t		kernel_time;		// protected by time_lock
	bigtime_t		last_time;			// protected by time_lock
	bigtime_t		cpu_clock_offset;	// protected by time_lock

	void			(*post_interrupt_callback)(void*);
	void*			post_interrupt_data;

#if KDEBUG_RW_LOCK_DEBUG
	rw_lock*		held_read_locks[64] = {}; // only modified by this thread
#endif

	// architecture dependent section
	struct arch_thread arch_info;

public:
								Thread() {}
									// dummy for the idle threads
								Thread(const char *name, thread_id threadID,
									struct cpu_ent *cpu);
								~Thread();

	static	status_t			Create(const char* name, Thread*& _thread);

	static	Thread*				Get(thread_id id);
	static	Thread*				GetAndLock(thread_id id);
	static	Thread*				GetDebug(thread_id id);
									// in kernel debugger only

	static	bool				IsAlive(thread_id id);

			void*				operator new(size_t size);
			void*				operator new(size_t, void* pointer);
			void				operator delete(void* pointer, size_t size);

			status_t			Init(bool idleThread);

			bool				Lock()
									{ mutex_lock(&fLock); return true; }
			bool				TryLock()
									{ return mutex_trylock(&fLock) == B_OK; }
			void				Unlock()
									{ mutex_unlock(&fLock); }

			void				UnlockAndReleaseReference()
									{ Unlock(); ReleaseReference(); }

			bool				IsAlive() const;

			bool				IsRunning() const
									{ return cpu != NULL; }
									// scheduler lock must be held

			sigset_t			ThreadPendingSignals() const
									{ return fPendingSignals.AllSignals(); }
	inline	sigset_t			AllPendingSignals() const;
			void				AddPendingSignal(int signal)
									{ fPendingSignals.AddSignal(signal); }
			void				AddPendingSignal(Signal* signal)
									{ fPendingSignals.AddSignal(signal); }
			void				RemovePendingSignal(int signal)
									{ fPendingSignals.RemoveSignal(signal); }
			void				RemovePendingSignal(Signal* signal)
									{ fPendingSignals.RemoveSignal(signal); }
			void				RemovePendingSignals(sigset_t mask)
									{ fPendingSignals.RemoveSignals(mask); }
			void				ResetSignalsOnExec();

	inline	int32				HighestPendingSignalPriority(
									sigset_t nonBlocked) const;
	inline	Signal*				DequeuePendingSignal(sigset_t nonBlocked,
									Signal& buffer);

			// user timers -- protected by fLock
			UserTimer*			UserTimerFor(int32 id) const
									{ return fUserTimers.TimerFor(id); }
			status_t			AddUserTimer(UserTimer* timer);
			void				RemoveUserTimer(UserTimer* timer);
			void				DeleteUserTimers(bool userDefinedOnly);

			void				UserTimerActivated(ThreadTimeUserTimer* timer)
									{ fCPUTimeUserTimers.Add(timer); }
			void				UserTimerDeactivated(ThreadTimeUserTimer* timer)
									{ fCPUTimeUserTimers.Remove(timer); }
			void				DeactivateCPUTimeUserTimers();
			bool				HasActiveCPUTimeUserTimers() const
									{ return !fCPUTimeUserTimers.IsEmpty(); }
			ThreadTimeUserTimerList::ConstIterator
									CPUTimeUserTimerIterator() const
									{ return fCPUTimeUserTimers.GetIterator(); }

	inline	bigtime_t			CPUTime(bool ignoreCurrentRun) const;

private:
			mutex				fLock;

			BKernel::PendingSignals	fPendingSignals;
									// protected by team->signal_lock

			UserTimerList		fUserTimers;			// protected by fLock
			ThreadTimeUserTimerList fCPUTimeUserTimers;
									// protected by time_lock
};


struct Team : TeamThreadIteratorEntry<team_id>, KernelReferenceable,
		AssociatedDataOwner {
	DoublyLinkedListLink<Team>	global_list_link;
	Team			*hash_next;		// next in hash
	DoublyLinkedListLink<Team> siblings_link; // protected by parent's fLock
	Team			*parent;		// write-protected by both parent (if any)
									// and this team's fLock
	DoublyLinkedList<Team, DoublyLinkedListMemberGetLink<Team, &Team::siblings_link> > children;
		// protected by this team's fLock;
		// adding/removing a child also requires the child's fLock
	DoublyLinkedListLink<Team> group_link; // protected by the group's lock

	int64			serial_number;	// immutable after adding team to hash

	// process group info -- write-protected by both the group's lock, the
	// team's lock, and the team's parent's lock
	pid_t			group_id;
	pid_t			session_id;
	ProcessGroup	*group;

	int				num_threads;	// number of threads in this team
	int				state;			// current team state, see above
	int32			flags;
	struct io_context *io_context;
	struct user_mutex_context *user_mutex_context;
	struct realtime_sem_context	*realtime_sem_context;
	struct xsi_sem_context *xsi_sem_context;
	struct team_death_entry *death_entry;	// protected by fLock
	ThreadDeathEntryList	dead_threads;

	// protected by the team's fLock
	team_dead_children dead_children;
	team_job_control_children stopped_children;
	team_job_control_children continued_children;

	// protected by the parent team's fLock
	struct job_control_entry* job_control_entry;

	VMAddressSpace	*address_space;
	Thread			*main_thread;	// protected by fLock, immutable
									// after first set
	DoublyLinkedList<Thread, DoublyLinkedListMemberGetLink<Thread, &Thread::team_link> >
		thread_list;	// protected by fLock, signal_lock and gThreadCreationLock
	struct team_loading_info *loading_info;	// protected by fLock
	DoublyLinkedList<image> image_list; // protected by sImageMutex
	struct list		watcher_list;
	struct list		sem_list;		// protected by sSemsSpinlock
	struct list		port_list;		// protected by sPortsLock
	struct arch_team arch_info;

	addr_t			user_data;
	area_id			user_data_area;
	size_t			user_data_size;
	size_t			used_user_data;
	struct free_user_thread* free_user_threads;

	void*			commpage_address;

	struct team_debug_info debug_info;

	bigtime_t		start_time;

	// protected by time_lock
	bigtime_t		dead_threads_kernel_time;
	bigtime_t		dead_threads_user_time;
	bigtime_t		cpu_clock_offset;
	spinlock		time_lock;

	// user group information; protected by fLock
	uid_t			saved_set_uid;
	uid_t			real_uid;
	uid_t			effective_uid;
	gid_t			saved_set_gid;
	gid_t			real_gid;
	gid_t			effective_gid;
	BReference<GroupsArray> supplementary_groups;

	// Exit status information. Set when the first terminal event occurs,
	// immutable afterwards. Protected by fLock.
	struct {
		uint16		reason;			// reason for the team's demise, one of the
									// CLD_* values defined in <signal.h>
		uint16		signal;			// signal killing the team
		uid_t		signaling_user;	// real UID of the signal sender
		status_t	status;			// exit status, if normal team exit
		bool		initialized;	// true when the state has been initialized
	} exit;

	spinlock		signal_lock;

public:
								~Team();

	static	Team*				Create(team_id id, const char* name,
									bool kernel);
	static	Team*				Get(team_id id);
	static	Team*				GetAndLock(team_id id);

			bool				Lock()
									{ mutex_lock(&fLock); return true; }
			bool				TryLock()
									{ return mutex_trylock(&fLock) == B_OK; }
			void				Unlock()
									{ mutex_unlock(&fLock); }

			void				UnlockAndReleaseReference()
									{ Unlock(); ReleaseReference(); }

			void				LockTeamAndParent(bool dontLockParentIfKernel);
			void				UnlockTeamAndParent();
			void				LockTeamAndProcessGroup();
			void				UnlockTeamAndProcessGroup();
			void				LockTeamParentAndProcessGroup();
			void				UnlockTeamParentAndProcessGroup();
			void				LockProcessGroup()
									{ LockTeamAndProcessGroup(); Unlock(); }

			const char*			Name() const	{ return fName; }
			void				SetName(const char* name);

			const char*			Args() const	{ return fArgs; }
			void				SetArgs(const char* args);
			void				SetArgs(const char* path,
									const char* const* otherArgs,
									int otherArgCount);

			BKernel::QueuedSignalsCounter* QueuedSignalsCounter() const
									{ return fQueuedSignalsCounter; }
			sigset_t			PendingSignals() const
									{ return fPendingSignals.AllSignals(); }

			void				AddPendingSignal(int signal)
									{ fPendingSignals.AddSignal(signal); }
			void				AddPendingSignal(Signal* signal)
									{ fPendingSignals.AddSignal(signal); }
			void				RemovePendingSignal(int signal)
									{ fPendingSignals.RemoveSignal(signal); }
			void				RemovePendingSignal(Signal* signal)
									{ fPendingSignals.RemoveSignal(signal); }
			void				RemovePendingSignals(sigset_t mask)
									{ fPendingSignals.RemoveSignals(mask); }
			void				ResetSignalsOnExec();

	inline	int32				HighestPendingSignalPriority(
									sigset_t nonBlocked) const;
	inline	Signal*				DequeuePendingSignal(sigset_t nonBlocked,
									Signal& buffer);

			struct sigaction&	SignalActionFor(int32 signal)
									{ return fSignalActions[signal - 1]; }
			void				InheritSignalActions(Team* parent);

			// user timers -- protected by fLock
			UserTimer*			UserTimerFor(int32 id) const
									{ return fUserTimers.TimerFor(id); }
			status_t			AddUserTimer(UserTimer* timer);
			void				RemoveUserTimer(UserTimer* timer);
			void				DeleteUserTimers(bool userDefinedOnly);

			bool				CheckAddUserDefinedTimer();
			void				UserDefinedTimersRemoved(int32 count);

			void				UserTimerActivated(TeamTimeUserTimer* timer)
									{ fCPUTimeUserTimers.Add(timer); }
			void				UserTimerActivated(TeamUserTimeUserTimer* timer)
									{ fUserTimeUserTimers.Add(timer); }
			void				UserTimerDeactivated(TeamTimeUserTimer* timer)
									{ fCPUTimeUserTimers.Remove(timer); }
			void				UserTimerDeactivated(
									TeamUserTimeUserTimer* timer)
									{ fUserTimeUserTimers.Remove(timer); }
			void				DeactivateCPUTimeUserTimers();
									// both total and user CPU timers
			bool				HasActiveCPUTimeUserTimers() const
									{ return !fCPUTimeUserTimers.IsEmpty(); }
			bool				HasActiveUserTimeUserTimers() const
									{ return !fUserTimeUserTimers.IsEmpty(); }
			TeamTimeUserTimerList::ConstIterator
									CPUTimeUserTimerIterator() const
									{ return fCPUTimeUserTimers.GetIterator(); }
	inline	TeamUserTimeUserTimerList::ConstIterator
									UserTimeUserTimerIterator() const;

			bigtime_t			CPUTime(bool ignoreCurrentRun,
									Thread* lockedThread = NULL) const;
			bigtime_t			UserCPUTime() const;

			ConditionVariable*	CoreDumpCondition() const
									{ return fCoreDumpCondition; }
			void				SetCoreDumpCondition(
									ConditionVariable* condition)
									{ fCoreDumpCondition = condition; }
private:
								Team(team_id id, bool kernel);

private:
			mutex				fLock;
			char				fName[B_OS_NAME_LENGTH];
			char				fArgs[64];
									// contents for the team_info::args field

			BKernel::QueuedSignalsCounter* fQueuedSignalsCounter;
			BKernel::PendingSignals	fPendingSignals;
									// protected by signal_lock
			struct sigaction 	fSignalActions[MAX_SIGNAL_NUMBER];
									// indexed signal - 1, protected by fLock

			UserTimerList		fUserTimers;			// protected by fLock
			TeamTimeUserTimerList fCPUTimeUserTimers;
									// protected by scheduler lock
			TeamUserTimeUserTimerList fUserTimeUserTimers;
			int32				fUserDefinedTimerCount;	// accessed atomically

			ConditionVariable*	fCoreDumpCondition;
									// protected by fLock
};


struct ProcessSession : BReferenceable {
	pid_t				id;
	void*				controlling_tty;
	pid_t				foreground_group;

public:
								ProcessSession(pid_t id);
								~ProcessSession();

			bool				Lock()
									{ mutex_lock(&fLock); return true; }
			bool				TryLock()
									{ return mutex_trylock(&fLock) == B_OK; }
			void				Unlock()
									{ mutex_unlock(&fLock); }

private:
			mutex				fLock;
};


struct ProcessGroup : KernelReferenceable {
	typedef DoublyLinkedList<Team,
		DoublyLinkedListMemberGetLink<Team,
			&Team::group_link> > TeamList;

public:
	struct ProcessGroup *hash_next;
	pid_t				id;
	TeamList			teams;

public:
								ProcessGroup(pid_t id);
								~ProcessGroup();

	static	ProcessGroup*		Get(pid_t id);

			bool				Lock()
									{ mutex_lock(&fLock); return true; }
			bool				TryLock()
									{ return mutex_trylock(&fLock) == B_OK; }
			void				Unlock()
									{ mutex_unlock(&fLock); }

			ProcessSession*		Session() const
									{ return fSession; }
			void				Publish(ProcessSession* session);
			void				PublishLocked(ProcessSession* session);

			bool				IsOrphaned() const;

			void				ScheduleOrphanedCheck();
			void				UnsetOrphanedCheck();

public:
			SinglyLinkedListLink<ProcessGroup> fOrphanedCheckListLink;

private:
			mutex				fLock;
			ProcessSession*		fSession;
			bool				fInOrphanedCheckList;	// protected by
														// sOrphanedCheckLock
};

typedef SinglyLinkedList<ProcessGroup,
	SinglyLinkedListMemberGetLink<ProcessGroup,
		&ProcessGroup::fOrphanedCheckListLink> > ProcessGroupList;


/*!	\brief Allows to iterate through all teams.
*/
struct TeamListIterator {
								TeamListIterator();
								~TeamListIterator();

			Team*				Next();

private:
			TeamThreadIteratorEntry<team_id> fEntry;
};


/*!	\brief Allows to iterate through all threads.
*/
struct ThreadListIterator {
								ThreadListIterator();
								~ThreadListIterator();

			Thread*				Next();

private:
			TeamThreadIteratorEntry<thread_id> fEntry;
};


inline int32
Team::HighestPendingSignalPriority(sigset_t nonBlocked) const
{
	return fPendingSignals.HighestSignalPriority(nonBlocked);
}


inline Signal*
Team::DequeuePendingSignal(sigset_t nonBlocked, Signal& buffer)
{
	return fPendingSignals.DequeueSignal(nonBlocked, buffer);
}


inline TeamUserTimeUserTimerList::ConstIterator
Team::UserTimeUserTimerIterator() const
{
	return fUserTimeUserTimers.GetIterator();
}


inline sigset_t
Thread::AllPendingSignals() const
{
	return fPendingSignals.AllSignals() | team->PendingSignals();
}


inline int32
Thread::HighestPendingSignalPriority(sigset_t nonBlocked) const
{
	return fPendingSignals.HighestSignalPriority(nonBlocked);
}


inline Signal*
Thread::DequeuePendingSignal(sigset_t nonBlocked, Signal& buffer)
{
	return fPendingSignals.DequeueSignal(nonBlocked, buffer);
}


/*!	Returns the thread's current total CPU time (kernel + user + offset).

	The caller must hold \c time_lock.

	\param ignoreCurrentRun If \c true and the thread is currently running,
		don't add the time since the last time \c last_time was updated. Should
		be used in "thread unscheduled" scheduler callbacks, since although the
		thread is still running at that time, its time has already been stopped.
	\return The thread's current total CPU time.
*/
inline bigtime_t
Thread::CPUTime(bool ignoreCurrentRun) const
{
	bigtime_t time = user_time + kernel_time + cpu_clock_offset;

	// If currently running, also add the time since the last check, unless
	// requested otherwise.
	if (!ignoreCurrentRun && last_time != 0)
		time += system_time() - last_time;

	return time;
}


}	// namespace BKernel

using BKernel::Team;
using BKernel::TeamListIterator;
using BKernel::Thread;
using BKernel::ThreadListIterator;
using BKernel::ProcessSession;
using BKernel::ProcessGroup;
using BKernel::ProcessGroupList;


#endif	// !_ASSEMBLER


// bits for the thread::flags field
#define	THREAD_FLAGS_SIGNALS_PENDING		0x0001
	// unblocked signals are pending (computed flag for optimization purposes)
#define	THREAD_FLAGS_DEBUG_THREAD			0x0002
	// forces the thread into the debugger as soon as possible (set by
	// debug_thread())
#define	THREAD_FLAGS_SINGLE_STEP			0x0004
	// indicates that the thread is in single-step mode (in userland)
#define	THREAD_FLAGS_DEBUGGER_INSTALLED		0x0008
	// a debugger is installed for the current team (computed flag for
	// optimization purposes)
#define	THREAD_FLAGS_BREAKPOINTS_DEFINED	0x0010
	// hardware breakpoints are defined for the current team (computed flag for
	// optimization purposes)
#define	THREAD_FLAGS_BREAKPOINTS_INSTALLED	0x0020
	// breakpoints are currently installed for the thread (i.e. the hardware is
	// actually set up to trigger debug events for them)
#define	THREAD_FLAGS_64_BIT_SYSCALL_RETURN	0x0040
	// set by 64 bit return value syscalls
#define	THREAD_FLAGS_RESTART_SYSCALL		0x0080
	// set by handle_signals(), if the current syscall shall be restarted
#define	THREAD_FLAGS_DONT_RESTART_SYSCALL	0x0100
	// explicitly disables automatic syscall restarts (e.g. resume_thread())
#define	THREAD_FLAGS_ALWAYS_RESTART_SYSCALL	0x0200
	// force syscall restart, even if a signal handler without SA_RESTART was
	// invoked (e.g. sigwait())
#define	THREAD_FLAGS_SYSCALL_RESTARTED		0x0400
	// the current syscall has been restarted
#define	THREAD_FLAGS_SYSCALL				0x0800
	// the thread is currently in a syscall; set/reset only for certain
	// functions (e.g. ioctl()) to allow inner functions to discriminate
	// whether e.g. parameters were passed from userland or kernel
#define	THREAD_FLAGS_TRAP_FOR_CORE_DUMP		0x1000
	// core dump in progress; the thread shall not exit the kernel to userland,
	// but shall invoke core_dump_trap_thread() instead.
#ifdef _COMPAT_MODE
#define	THREAD_FLAGS_COMPAT_MODE			0x2000
	// the thread runs a compatibility mode (for instance IA32 on x86_64).
#endif
#define	THREAD_FLAGS_OLD_SIGMASK			0x4000
	// the thread has an old sigmask to be restored

#endif	/* _KERNEL_THREAD_TYPES_H */

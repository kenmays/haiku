/*
 * Copyright 2022, Haiku, Inc. All rights reserved.
 * Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include <pthread.h>

#include <new>

#include <Debug.h>

#include <AutoLocker.h>
#include <syscalls.h>
#include <time_private.h>
#include <user_mutex_defs.h>
#include <user_thread.h>
#include <util/DoublyLinkedList.h>

#include "pthread_private.h"

#define MAX_READER_COUNT	1000000

#define RWLOCK_FLAG_SHARED	0x01


struct Waiter : DoublyLinkedListLinkImpl<Waiter> {
	Waiter(bool writer)
		:
		userThread(get_user_thread()),
		thread(find_thread(NULL)),
		writer(writer),
		queued(false)
	{
	}

	user_thread*	userThread;
	thread_id		thread;
	status_t		status;
	bool			writer;
	bool			queued;
};

typedef DoublyLinkedList<Waiter> WaiterList;


struct SharedRWLock {
	uint32_t	flags;
	int32_t		owner;
	int32_t		sem;

	status_t Init()
	{
		flags = RWLOCK_FLAG_SHARED;
		owner = -1;
		sem = create_sem(MAX_READER_COUNT, "pthread rwlock");

		return sem >= 0 ? B_OK : EAGAIN;
	}

	status_t Destroy()
	{
		if (sem < 0)
			return B_BAD_VALUE;
		return delete_sem(sem) == B_OK ? B_OK : B_BAD_VALUE;
	}

	status_t ReadLock(uint32 flags, bigtime_t timeout)
	{
		status_t status;
		do {
			status = acquire_sem_etc(sem, 1, flags, timeout);
		} while (status == B_INTERRUPTED);
		return status;
	}

	status_t WriteLock(uint32 flags, bigtime_t timeout)
	{
		status_t status;
		do {
			status = acquire_sem_etc(sem, MAX_READER_COUNT, flags, timeout);
		} while (status == B_INTERRUPTED);
		if (status == B_OK)
			owner = find_thread(NULL);
		return status;
	}

	status_t Unlock()
	{
		if (find_thread(NULL) == owner) {
			owner = -1;
			return release_sem_etc(sem, MAX_READER_COUNT, 0);
		} else
			return release_sem(sem);
	}
};


struct LocalRWLock {
	uint32_t	flags;
	int32_t		owner;
	int32_t		mutex;
	int32_t		unused;
	int32_t		reader_count;
	int32_t		writer_count;
		// Note, that reader_count and writer_count are not used the same way.
		// writer_count includes the write lock owner as well as waiting
		// writers. reader_count includes read lock owners only.
	WaiterList	waiters;

	status_t Init()
	{
		flags = 0;
		owner = -1;
		mutex = 0;
		reader_count = 0;
		writer_count = 0;
		new(&waiters) WaiterList;

		return B_OK;
	}

	status_t Destroy()
	{
		Locker locker(this);
		if (reader_count > 0 || waiters.Head() != NULL || writer_count > 0)
			return EBUSY;
		return B_OK;
	}

	bool StructureLock()
	{
		const int32 oldValue = atomic_test_and_set((int32*)&mutex, B_USER_MUTEX_LOCKED, 0);
		if (oldValue != 0) {
			status_t status;
			do {
				status = _kern_mutex_lock((int32*)&mutex, NULL, 0, 0);
			} while (status == B_INTERRUPTED);

			if (status != B_OK)
				return false;
		}
		return true;
	}

	void StructureUnlock()
	{
		// Exit critical region: unlock the mutex
		int32 status = atomic_and((int32*)&mutex,
			~(int32)B_USER_MUTEX_LOCKED);
		if ((status & B_USER_MUTEX_WAITING) != 0)
			_kern_mutex_unblock((int32*)&mutex, 0);
	}

	status_t ReadLock(uint32 flags, bigtime_t timeout)
	{
		Locker locker(this);

		if (writer_count == 0) {
			reader_count++;
			return B_OK;
		}

		return _Wait(false, flags, timeout);
	}

	status_t WriteLock(uint32 flags, bigtime_t timeout)
	{
		Locker locker(this);

		if (reader_count == 0 && writer_count == 0) {
			writer_count++;
			owner = find_thread(NULL);
			return B_OK;
		}

		return _Wait(true, flags, timeout);
	}

	status_t Unlock()
	{
		Locker locker(this);

		if (find_thread(NULL) == owner) {
			writer_count--;
			owner = -1;
		} else
			reader_count--;

		_Unblock();

		return B_OK;
	}

private:
	status_t _Wait(bool writer, uint32 flags, bigtime_t timeout)
	{
		if (timeout == 0)
			return B_TIMED_OUT;

		if (writer_count == 1 && owner == find_thread(NULL))
			return EDEADLK;

		Waiter waiter(writer);
		waiters.Add(&waiter);
		waiter.queued = true;
		waiter.userThread->wait_status = 1;

		if (writer)
			writer_count++;

		status_t status;
		do {
			StructureUnlock();
			status = _kern_block_thread(flags, timeout);
			StructureLock();

			if (!waiter.queued)
				return waiter.status;
		} while (status == B_INTERRUPTED);

		// we're still queued, which means an error (timeout, interrupt)
		// occurred
		waiters.Remove(&waiter);

		if (writer)
			writer_count--;

		_Unblock();

		return status;
	}

	void _Unblock()
	{
		// Check whether there any waiting threads at all and whether anyone
		// has the write lock
		Waiter* waiter = waiters.Head();
		if (waiter == NULL || owner >= 0)
			return;

		// writer at head of queue?
		if (waiter->writer) {
			if (reader_count == 0) {
				waiter->status = B_OK;
				waiter->queued = false;
				waiters.Remove(waiter);
				owner = waiter->thread;

				if (waiter->userThread->wait_status > 0)
					_kern_unblock_thread(waiter->thread, B_OK);
			}
			return;
		}

		// wake up one or more readers -- we unblock more than one reader at
		// a time to save trips to the kernel
		while (!waiters.IsEmpty() && !waiters.Head()->writer) {
			static const int kMaxReaderUnblockCount = 128;
			thread_id readers[kMaxReaderUnblockCount];
			int readerCount = 0;

			while (readerCount < kMaxReaderUnblockCount
					&& (waiter = waiters.Head()) != NULL
					&& !waiter->writer) {
				waiter->status = B_OK;
				waiter->queued = false;
				waiters.Remove(waiter);

				if (waiter->userThread->wait_status > 0) {
					readers[readerCount++] = waiter->thread;
					reader_count++;
				}
			}

			if (readerCount > 0)
				_kern_unblock_threads(readers, readerCount, B_OK);
		}
	}


	struct Locking {
		inline bool Lock(LocalRWLock* lockable)
		{
			return lockable->StructureLock();
		}

		inline void Unlock(LocalRWLock* lockable)
		{
			lockable->StructureUnlock();
		}
	};
	typedef AutoLocker<LocalRWLock, Locking> Locker;
};


static void inline
assert_dummy()
{
	STATIC_ASSERT(sizeof(pthread_rwlock_t) >= sizeof(SharedRWLock));
	STATIC_ASSERT(sizeof(pthread_rwlock_t) >= sizeof(LocalRWLock));
}


// #pragma mark - public lock functions


int
pthread_rwlock_init(pthread_rwlock_t* lock, const pthread_rwlockattr_t* _attr)
{
	pthread_rwlockattr* attr = _attr != NULL ? *_attr : NULL;
	bool shared = attr != NULL && (attr->flags & RWLOCK_FLAG_SHARED) != 0;

	if (shared)
		return ((SharedRWLock*)lock)->Init();
	else
		return ((LocalRWLock*)lock)->Init();
}


int
pthread_rwlock_destroy(pthread_rwlock_t* lock)
{
	if ((lock->flags & RWLOCK_FLAG_SHARED) != 0)
		return ((SharedRWLock*)lock)->Destroy();
	else
		return ((LocalRWLock*)lock)->Destroy();
}


int
pthread_rwlock_rdlock(pthread_rwlock_t* lock)
{
	if ((lock->flags & RWLOCK_FLAG_SHARED) != 0)
		return ((SharedRWLock*)lock)->ReadLock(0, B_INFINITE_TIMEOUT);
	else
		return ((LocalRWLock*)lock)->ReadLock(0, B_INFINITE_TIMEOUT);
}


int
pthread_rwlock_tryrdlock(pthread_rwlock_t* lock)
{
	status_t error;
	if ((lock->flags & RWLOCK_FLAG_SHARED) != 0)
		error = ((SharedRWLock*)lock)->ReadLock(B_ABSOLUTE_REAL_TIME_TIMEOUT, 0);
	else
		error = ((LocalRWLock*)lock)->ReadLock(B_ABSOLUTE_REAL_TIME_TIMEOUT, 0);

	return error == B_TIMED_OUT ? EBUSY : error;
}


int
pthread_rwlock_clockrdlock(pthread_rwlock_t* lock, clockid_t clock_id,
	const struct timespec *abstime)
{
	bigtime_t timeout = 0;
	bool invalidTime = false;
	if (abstime == NULL || !timespec_to_bigtime(*abstime, timeout))
		invalidTime = true;

	uint32 flags = 0;
	if (timeout >= 0) {
		switch (clock_id) {
			case CLOCK_REALTIME:
				flags = B_ABSOLUTE_REAL_TIME_TIMEOUT;
				break;
			case CLOCK_MONOTONIC:
				flags = B_ABSOLUTE_TIMEOUT;
				break;
			default:
				return EINVAL;
		}
	}

	status_t error;
	if ((lock->flags & RWLOCK_FLAG_SHARED) != 0)
		error = ((SharedRWLock*)lock)->ReadLock(flags, timeout);
	else
		error = ((LocalRWLock*)lock)->ReadLock(flags, timeout);

	if (error != B_OK && invalidTime)
		return EINVAL;
	return (error == B_TIMED_OUT) ? EBUSY : error;
}


int
pthread_rwlock_timedrdlock(pthread_rwlock_t* lock,
	const struct timespec *abstime)
{
	return pthread_rwlock_clockrdlock(lock, CLOCK_REALTIME, abstime);
}


int
pthread_rwlock_wrlock(pthread_rwlock_t* lock)
{
	if ((lock->flags & RWLOCK_FLAG_SHARED) != 0)
		return ((SharedRWLock*)lock)->WriteLock(0, B_INFINITE_TIMEOUT);
	else
		return ((LocalRWLock*)lock)->WriteLock(0, B_INFINITE_TIMEOUT);
}


int
pthread_rwlock_trywrlock(pthread_rwlock_t* lock)
{
	status_t error;
	if ((lock->flags & RWLOCK_FLAG_SHARED) != 0)
		error = ((SharedRWLock*)lock)->WriteLock(B_ABSOLUTE_REAL_TIME_TIMEOUT, 0);
	else
		error = ((LocalRWLock*)lock)->WriteLock(B_ABSOLUTE_REAL_TIME_TIMEOUT, 0);

	return error == B_TIMED_OUT ? EBUSY : error;
}


int
pthread_rwlock_clockwrlock(pthread_rwlock_t* lock, clockid_t clock_id,
	const struct timespec *abstime)
{
	bigtime_t timeout = 0;
	bool invalidTime = false;
	if (abstime == NULL || !timespec_to_bigtime(*abstime, timeout))
		invalidTime = true;

	uint32 flags = 0;
	if (timeout >= 0) {
		switch (clock_id) {
			case CLOCK_REALTIME:
				flags = B_ABSOLUTE_REAL_TIME_TIMEOUT;
				break;
			case CLOCK_MONOTONIC:
				flags = B_ABSOLUTE_TIMEOUT;
				break;
			default:
				return EINVAL;
		}
	}

	status_t error;
	if ((lock->flags & RWLOCK_FLAG_SHARED) != 0)
		error = ((SharedRWLock*)lock)->WriteLock(flags, timeout);
	else
		error = ((LocalRWLock*)lock)->WriteLock(flags, timeout);

	if (error != B_OK && invalidTime)
		return EINVAL;
	return (error == B_TIMED_OUT) ? EBUSY : error;
}


int
pthread_rwlock_timedwrlock(pthread_rwlock_t* lock,
	const struct timespec *abstime)
{
	return pthread_rwlock_clockwrlock(lock, CLOCK_REALTIME, abstime);
}


int
pthread_rwlock_unlock(pthread_rwlock_t* lock)
{
	if ((lock->flags & RWLOCK_FLAG_SHARED) != 0)
		return ((SharedRWLock*)lock)->Unlock();
	else
		return ((LocalRWLock*)lock)->Unlock();
}


// #pragma mark - public attribute functions


int
pthread_rwlockattr_init(pthread_rwlockattr_t* _attr)
{
	pthread_rwlockattr* attr = (pthread_rwlockattr*)malloc(
		sizeof(pthread_rwlockattr));
	if (attr == NULL)
		return B_NO_MEMORY;

	attr->flags = 0;
	*_attr = attr;

	return 0;
}


int
pthread_rwlockattr_destroy(pthread_rwlockattr_t* _attr)
{
	pthread_rwlockattr* attr = *_attr;

	free(attr);
	return 0;
}


int
pthread_rwlockattr_getpshared(const pthread_rwlockattr_t* _attr, int* shared)
{
	pthread_rwlockattr* attr = *_attr;

	*shared = (attr->flags & RWLOCK_FLAG_SHARED) != 0
		? PTHREAD_PROCESS_SHARED : PTHREAD_PROCESS_PRIVATE;
	return 0;
}


int
pthread_rwlockattr_setpshared(pthread_rwlockattr_t* _attr, int shared)
{
	pthread_rwlockattr* attr = *_attr;

	if (shared == PTHREAD_PROCESS_SHARED)
		attr->flags |= RWLOCK_FLAG_SHARED;
	else
		attr->flags &= ~RWLOCK_FLAG_SHARED;

	return 0;
}

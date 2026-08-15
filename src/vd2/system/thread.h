// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2024 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_THREAD_H
#define f_VD2_SYSTEM_THREAD_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/atomic.h>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <climits>
#include <QThread>
#include <QMutex>
#include <QRecursiveMutex>
#include <QSemaphore>

typedef void *VDThreadHandle;
typedef uint32 VDThreadID;
typedef uint32 VDThreadId;
typedef uint32 VDProcessId;

VDThreadID VDGetCurrentThreadID();
VDProcessId VDGetCurrentProcessId();
uint32 VDGetLogicalProcessorCount();

void VDSetThreadDebugName(VDThreadID tid, const char *name);
void VDThreadSleep(int milliseconds);

class VDThread {
public:
	enum {
		kPriorityDefault = INT_MIN
	};

	VDThread(const char *pszDebugName = nullptr);
	virtual ~VDThread() noexcept;

	bool ThreadStart();
	void ThreadDetach();
	void ThreadWait();
	void ThreadSetPriority(int priority);

	bool isThreadActive();

	bool isThreadAttached() const {
		return mbAttached;
	}

	VDThreadHandle getThreadHandle() const {
		return mhThread;
	}

	VDThreadID getThreadID() const {
		return mThreadID;
	}

	void *ThreadLocation() const { return nullptr; }

	virtual void ThreadRun() = 0;
	void ThreadFinish();

private:
	static void StaticThreadStart(VDThread *pThis);

	const char *mpszDebugName;
	VDThreadHandle mhThread;
	VDThreadID mThreadID;
	int mThreadPriority;
	bool mbAttached;
	std::thread *mpThread;
};

class VDCriticalSection {
private:
	QRecursiveMutex mMutex;

	VDCriticalSection(const VDCriticalSection&) = delete;
	const VDCriticalSection& operator=(const VDCriticalSection&) = delete;
public:
	class AutoLock {
	private:
		VDCriticalSection& cs;
	public:
		AutoLock(VDCriticalSection& csect) : cs(csect) { cs.Lock(); }
		~AutoLock() { cs.Unlock(); }

		inline operator bool() const { return false; }
	};

	VDCriticalSection() = default;
	~VDCriticalSection() = default;

	void operator++() { Lock(); }
	void operator--() { Unlock(); }

	void Lock() { mMutex.lock(); }
	void Unlock() { mMutex.unlock(); }
};

#define vdsynchronized2(lock) if(VDCriticalSection::AutoLock vd__lock=(lock))VDNEVERHERE;else
#define vdsynchronized1(lock) vdsynchronized2(lock)
#define vdsynchronized(lock) vdsynchronized1(lock)

class VDSignalBase {
protected:
	std::mutex mMutex;
	std::condition_variable mCv;
	bool mSignaled;
	bool mManualReset;

public:
	VDSignalBase(bool manualReset = false);
	virtual ~VDSignalBase() = default;

	void signal();
	bool check();
	void wait();
	int wait(VDSignalBase *second);
	int wait(VDSignalBase *second, VDSignalBase *third);
	static int waitMultiple(const VDSignalBase **signalArray, int count);

	bool tryWait(uint32 timeoutMillisec);

	void *getHandle() { return this; }
	void operator()() { signal(); }
};

class VDSignal : public VDSignalBase {
public:
	VDSignal();
};

class VDSignalPersistent : public VDSignalBase {
public:
	VDSignalPersistent();
	void unsignal();
};

class VDSemaphore {
public:
	VDSemaphore(int initial);
	~VDSemaphore() = default;

	void *GetHandle() const { return (void*)&mSema; }
	void Reset(int count);
	void Wait();
	bool Wait(int timeout);
	bool TryWait();
	void Post();

private:
	mutable QSemaphore mSema;
};

#endif

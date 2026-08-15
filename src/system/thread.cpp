// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2017 Anton Shekhovtsov
// Copyright (C) 2024 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/vdtypes.h>
#include <vd2/system/thread.h>
#include <vd2/system/tls.h>
#include <vd2/system/protscope.h>
#include <vd2/system/bitmath.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>

VDThreadID VDGetCurrentThreadID() {
	return (VDThreadID)(uintptr_t)pthread_self();
}

VDProcessId VDGetCurrentProcessId() {
	return (VDProcessId)getpid();
}

uint32 VDGetLogicalProcessorCount() {
	int count = QThread::idealThreadCount();
	return count > 0 ? (uint32)count : 1;
}

void VDSetThreadDebugName(VDThreadID tid, const char *name) {
#if defined(__linux__)
	if (name) {
		pthread_setname_np((pthread_t)tid, name);
	}
#endif
}

void VDThreadSleep(int milliseconds) {
	if (milliseconds > 0)
		QThread::msleep(milliseconds);
}

VDThread::VDThread(const char *pszDebugName)
	: mpszDebugName(pszDebugName)
	, mhThread(nullptr)
	, mThreadID(0)
	, mThreadPriority(INT_MIN)
	, mbAttached(false)
	, mpThread(nullptr)
{
}

VDThread::~VDThread() noexcept {
	if (isThreadAttached())
		ThreadWait();
}

void VDThread::StaticThreadStart(VDThread *pThis) {
	pThis->mThreadID = VDGetCurrentThreadID();
	if (pThis->mpszDebugName)
		VDSetThreadDebugName(pThis->mThreadID, pThis->mpszDebugName);

	VDInitThreadData(pThis->mpszDebugName);
	pThis->ThreadRun();
	VDDeinitThreadData();
}

bool VDThread::ThreadStart() {
	if (!mbAttached) {
		mpThread = new std::thread(StaticThreadStart, this);
		mhThread = (void*)mpThread->native_handle();
		mbAttached = true;
	}
	return mbAttached;
}

void VDThread::ThreadDetach() {
	if (mbAttached) {
		if (mpThread && mpThread->joinable()) {
			mpThread->detach();
		}
		delete mpThread;
		mpThread = nullptr;
		mhThread = nullptr;
		mThreadID = 0;
		mbAttached = false;
	}
}

void VDThread::ThreadWait() {
	if (mbAttached) {
		if (mpThread && mpThread->joinable()) {
			mpThread->join();
		}
		delete mpThread;
		mpThread = nullptr;
		mhThread = nullptr;
		mThreadID = 0;
		mbAttached = false;
	}
}

void VDThread::ThreadSetPriority(int priority) {
	mThreadPriority = priority;
}

bool VDThread::isThreadActive() {
	return mbAttached;
}

void VDThread::ThreadFinish() {
}

VDSignalBase::VDSignalBase(bool manualReset)
	: mSignaled(false), mManualReset(manualReset)
{
}

VDSignal::VDSignal() : VDSignalBase(false) {}

VDSignalPersistent::VDSignalPersistent() : VDSignalBase(true) {}

void VDSignalPersistent::unsignal() {
	std::lock_guard<std::mutex> lock(mMutex);
	mSignaled = false;
}

void VDSignalBase::signal() {
	std::lock_guard<std::mutex> lock(mMutex);
	mSignaled = true;
	if (mManualReset) {
		mCv.notify_all();
	} else {
		mCv.notify_one();
	}
}

void VDSignalBase::wait() {
	std::unique_lock<std::mutex> lock(mMutex);
	mCv.wait(lock, [this]() { return mSignaled; });
	if (!mManualReset) {
		mSignaled = false;
	}
}

bool VDSignalBase::check() {
	std::lock_guard<std::mutex> lock(mMutex);
	if (mSignaled) {
		if (!mManualReset) mSignaled = false;
		return true;
	}
	return false;
}

int VDSignalBase::wait(VDSignalBase *second) {
	const VDSignalBase *sigs[2] = { this, second };
	return waitMultiple(sigs, 2);
}

int VDSignalBase::wait(VDSignalBase *second, VDSignalBase *third) {
	const VDSignalBase *sigs[3] = { this, second, third };
	return waitMultiple(sigs, 3);
}

int VDSignalBase::waitMultiple(const VDSignalBase **signalArray, int count) {
	if (count <= 0) return -1;
	for (;;) {
		for (int i = 0; i < count; ++i) {
			if (signalArray[i] && const_cast<VDSignalBase*>(signalArray[i])->check()) {
				return i;
			}
		}
		QThread::msleep(1);
	}
}


bool VDSignalBase::tryWait(uint32 timeoutMillisec) {
	std::unique_lock<std::mutex> lock(mMutex);
	if (mCv.wait_for(lock, std::chrono::milliseconds(timeoutMillisec), [this]() { return mSignaled; })) {
		if (!mManualReset) mSignaled = false;
		return true;
	}
	return false;
}

VDSemaphore::VDSemaphore(int initial)
	: mSema(initial)
{
}

void VDSemaphore::Reset(int count) {
	int avail = mSema.available();
	if (avail > 0) mSema.acquire(avail);
	if (count > 0) mSema.release(count);
}

void VDSemaphore::Wait() {
	mSema.acquire(1);
}

bool VDSemaphore::Wait(int timeout) {
	return mSema.tryAcquire(1, timeout);
}

bool VDSemaphore::TryWait() {
	return mSema.tryAcquire(1, 0);
}

void VDSemaphore::Post() {
	mSema.release(1);
}

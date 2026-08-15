// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_VDSCHEDULER_H
#define f_VD2_SYSTEM_VDSCHEDULER_H

#include <vd2/system/vdstl.h>
#include <vd2/system/thread.h>
#include <vd2/system/error.h>

class VDSchedulerNode;
class VDSchedulerSuspendNode;
class VDSignal;
class IVDAsyncErrorCallback;

class VDScheduler {
public:
	VDScheduler();
	~VDScheduler() = default;

	void setSignal(VDSignal *);
	VDSignal *getSignal() { return pWakeupSignal; }
	void setSchedulerNode(VDSchedulerNode *pSchedulerNode);

	IVDAsyncErrorCallback *getErrorCallback() const { return mpErrorCB; }
	void setErrorCallback(IVDAsyncErrorCallback *pCB) { mpErrorCB = pCB; }

	bool isShuttingDown() const { return mbExitThreads; }

	void BeginShutdown();							///< Start signaling scheduling threads to exit.

	bool Run();
	bool IdleWait();								///< Wait because no nodes are ready. Returns false if a thread should exit immediately.
	void Ping();									///< Restart a scheduler thread.  This is required when a scheduler thread leaves.
	void Lock();
	void Unlock();
	void Reschedule(VDSchedulerNode *);				///< Move node to Ready if Waiting.
	void RescheduleFast(VDSchedulerNode *);			///< Same as Reschedule(), but assumes the scheduler is already locked.
	void Add(VDSchedulerNode *pNode);				///< Add node to scheduler.
	void Remove(VDSchedulerNode *pNode);			///< Remove node from scheduler.
	void DumpStatus();

protected:
	void Repost(VDSchedulerNode *, bool);

	VDCriticalSection csScheduler;
	IVDAsyncErrorCallback	*mpErrorCB;
	VDSignal *pWakeupSignal;
	volatile bool	mbExitThreads;
	VDSchedulerNode *pParentSchedulerNode;

	typedef vdlist<VDSchedulerNode> tNodeList;
	tNodeList listWaiting, listReady;

	typedef vdlist<VDSchedulerSuspendNode> tSuspendList;
	tSuspendList listSuspends;
};

class VDSchedulerNode : public vdlist<VDSchedulerNode>::node {
friend class VDScheduler;
public:
	int nPriority;

	VDSchedulerNode() : nPriority(0), mpScheduler(NULL) {}

	virtual bool Service()=0;

	virtual void DumpStatus();

	void Reschedule() { mpScheduler->Reschedule(this); }
	void RemoveFromScheduler() { mpScheduler->Remove(this); }

protected:
	VDScheduler *mpScheduler;
	volatile bool bRunning;
	volatile bool bReschedule;
	volatile bool bReady;
	volatile bool bCondemned;
};

class VDSchedulerSuspendNode : public vdlist<VDSchedulerSuspendNode>::node {
public:
	VDSchedulerSuspendNode(VDSchedulerNode *pNode) : mpNode(pNode) {}

	VDSchedulerNode *mpNode;
	VDSignal mSignal;
};

class VDSchedulerThread : public VDThread {
	VDSchedulerThread(const VDSchedulerThread&);
	VDSchedulerThread& operator=(const VDSchedulerThread&);
public:
	VDSchedulerThread();
	~VDSchedulerThread() = default;;

	bool Start(VDScheduler *pScheduler);

protected:
	void ThreadRun();

	VDScheduler *mpScheduler;
	//uint32 mAffinity;
};

class VDSchedulerThreadPool {
	VDSchedulerThreadPool(const VDSchedulerThreadPool&);
	VDSchedulerThreadPool& operator=(const VDSchedulerThreadPool&);
public:
	VDSchedulerThreadPool();
	~VDSchedulerThreadPool();

	uint32 GetThreadCount() const { return mThreadCount; }

	void SetPriority(int priority);

	bool Start(VDScheduler *pScheduler);
	bool Start(VDScheduler *pScheduler, uint32 threadCount);

protected:
	VDSchedulerThread *mpThreads;
	uint32 mThreadCount;
	int mThreadPriority;
};

#endif

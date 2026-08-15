// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_DUBFRAMEREQUESTQUEUE_H
#define f_VD2_DUBFRAMEREQUESTQUEUE_H

#include <vd2/system/thread.h>
#include <vd2/system/event.h>

struct VDDubFrameRequest {
	sint64	mSrcFrame;
	bool	mbDirect;
};

class VDDubFrameRequestQueue {
	VDDubFrameRequestQueue(const VDDubFrameRequestQueue&);
	VDDubFrameRequestQueue& operator=(const VDDubFrameRequestQueue&);

public:
	VDDubFrameRequestQueue();
	~VDDubFrameRequestQueue();

	const VDSignal& GetNotEmptySignal() const;
	uint32 GetQueueLength();

	void Shutdown();

	void AddRequest(const VDDubFrameRequest& request);
	bool RemoveRequest(VDDubFrameRequest& request);

	VDEvent<VDDubFrameRequestQueue, bool>& OnLowWatermark() {
		return mLowWatermarkEvent;
	}

protected:
	typedef vdfastdeque<VDDubFrameRequest> Queue;
	Queue mQueue;

	VDSignal mNotEmpty;
	VDCriticalSection mMutex;

	VDEvent<VDDubFrameRequestQueue, bool> mLowWatermarkEvent;
};

#endif

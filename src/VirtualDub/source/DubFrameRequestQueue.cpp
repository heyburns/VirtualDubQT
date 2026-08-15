// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <vd2/system/refcount.h>
#include "DubFrameRequestQueue.h"

VDDubFrameRequestQueue::VDDubFrameRequestQueue() {
}

VDDubFrameRequestQueue::~VDDubFrameRequestQueue() {
	Shutdown();
}

const VDSignal& VDDubFrameRequestQueue::GetNotEmptySignal() const {
	return mNotEmpty;
}

uint32 VDDubFrameRequestQueue::GetQueueLength() {
	vdsynchronized(mMutex) {
		return mQueue.size();
	}
}

void VDDubFrameRequestQueue::Shutdown() {
	vdsynchronized(mMutex) {
		mQueue.clear();
	}
}

void VDDubFrameRequestQueue::AddRequest(const VDDubFrameRequest& request) {
	bool shouldSignal = false;

	vdsynchronized(mMutex) {
		shouldSignal = mQueue.empty();

		mQueue.push_back(request);
	}

	if (shouldSignal)
		mNotEmpty.signal();
}

bool VDDubFrameRequestQueue::RemoveRequest(VDDubFrameRequest& request) {
	vdsynchronized(mMutex) {
		if (mQueue.empty())
			return false;

		request = mQueue.front();
		mQueue.pop_front();

		mLowWatermarkEvent.Raise(this, false);
		return true;
	}
}

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAMEQUEUE_H
#define f_VD2_FILTERFRAMEQUEUE_H

#include <vd2/system/vdstl.h>
#include "FilterFrameRequest.h"

class VDFilterFrameRequest;
class VDTextOutputStream;

class VDFilterFrameQueue {
	VDFilterFrameQueue(const VDFilterFrameQueue&);
	VDFilterFrameQueue& operator=(const VDFilterFrameQueue&);
public:
	VDFilterFrameQueue();
	~VDFilterFrameQueue();

	void Shutdown();

	bool GetRequest(sint64 frame, VDFilterFrameRequest **req);
	void CompleteRequests(sint64 frame, VDFilterFrameBuffer *buf);

	void CreateRequest(VDFilterFrameRequest **req);
	bool PeekNextRequest(const uint32 *batchNumberLimit, VDFilterFrameRequest **req);
	bool GetNextRequest(const uint32 *batchNumberLimit, VDFilterFrameRequest **req);

	void Add(VDFilterFrameRequest *req);
	bool Remove(VDFilterFrameRequest *req);

	void DumpStatus(VDTextOutputStream& os);

#ifdef _DEBUG
	void ValidateState();
#else
	inline void ValidateState() {}
#endif

protected:
	typedef vdfastdeque<VDFilterFrameRequest *> Requests;
	Requests mRequests;

	VDFilterFrameRequestAllocator mAllocator;
};

#endif	// f_VD2_FILTERFRAMEQUEUE_H

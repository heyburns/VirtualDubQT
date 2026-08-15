// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2016 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERACCELCONVERTER_H
#define f_VD2_FILTERACCELCONVERTER_H

#include <vd2/system/vectors.h>
#include "FilterInstance.h"
#include "FilterFrameManualSource.h"

class VDFilterAccelEngine;
class VDFilterFrameBufferAccel;

class VDFilterAccelConverter : public VDFilterFrameManualSource {
	VDFilterAccelConverter(const VDFilterAccelConverter&);
	VDFilterAccelConverter& operator=(const VDFilterAccelConverter&);
public:
	VDFilterAccelConverter();
	~VDFilterAccelConverter();

	void Init(VDFilterAccelEngine *engine, IVDFilterFrameSource *source, const VDPixmapLayout& outputLayout, const vdrect32 *srcRect);
	void Start(IVDFilterFrameEngine *frameEngine);
	void Stop();

	bool GetDirectMapping(sint64 outputFrame, sint64& sourceFrame, int& sourceIndex);
	sint64 GetSourceFrame(sint64 outputFrame);
	sint64 GetSymbolicFrame(sint64 outputFrame, IVDFilterFrameSource *source);
	sint64 GetNearestUniqueFrame(sint64 outputFrame);

	RunResult RunRequests(const uint32 *batchNumberLimit, int index);
	RunResult RunProcess(int index);

protected:
	bool InitNewRequest(VDFilterFrameRequest *req, sint64 outputFrame, bool writable, uint32 batchNumber);

	VDFilterAccelEngine	*mpEngine;
	IVDFilterFrameSource *mpSource;
	IVDFilterFrameEngine *mpFrameEngine;
	VDPixmapLayout		mSourceLayout;
	vdrect32			mSourceRect;

	vdrefptr<VDFilterFrameRequest> mpRequest;

	enum ProcessStatus {
		kProcess_Idle,
		kProcess_Pending,
		kProcess_Succeeded,
		kProcess_Failed
	};

	VDAtomicInt mProcessStatus;
	VDFilterFrameBufferAccel *mpLockedDst;
	VDFilterFrameBufferAccel *mpLockedSrc;
};

#endif

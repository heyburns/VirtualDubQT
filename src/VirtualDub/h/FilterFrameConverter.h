// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2016-2017 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAMECONVERTER_H
#define f_VD2_FILTERFRAMECONVERTER_H

#include "FilterInstance.h"
#include "FilterFrameManualSource.h"
#include <vd2/Kasumi/blitter.h>

class VDFilterFrameConverter : public VDFilterFrameManualSource {
	friend class VDFilterFrameConverterNode;

	VDFilterFrameConverter(const VDFilterFrameConverter&);
	VDFilterFrameConverter& operator=(const VDFilterFrameConverter&);
public:
	VDFilterFrameConverter();
	~VDFilterFrameConverter();

	void Init(IVDFilterFrameSource *source, const VDPixmapLayout& outputLayout, const VDPixmapLayout *sourceLayoutOverride, bool normalize16);
	void Start(IVDFilterFrameEngine *frameEngine);
	void Stop();

	bool GetDirectMapping(sint64 outputFrame, sint64& sourceFrame, int& sourceIndex);
	sint64 GetSourceFrame(sint64 outputFrame);
	sint64 GetSymbolicFrame(sint64 outputFrame, IVDFilterFrameSource *source);
	sint64 GetNearestUniqueFrame(sint64 outputFrame);

	int AllocateNodes(int threads);
	RunResult RunRequests(const uint32 *batchNumberLimit, int index);
	RunResult RunProcess(int index);

protected:
	bool InitNewRequest(VDFilterFrameRequest *req, sint64 outputFrame, bool writable, uint32 batchNumber);

	IVDFilterFrameEngine *mpEngine;
	IVDFilterFrameSource *mpSource;
	VDPixmapLayout		mSourceLayout;
	bool mNormalize16;

	VDFilterFrameConverterNode* node;
	int node_count;
	uintptr scope;
};

#endif

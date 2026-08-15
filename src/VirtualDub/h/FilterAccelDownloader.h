// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2016 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERACCELDOWNLOADER_H
#define f_VD2_FILTERACCELDOWNLOADER_H

#include <vd2/system/thread.h>
#include <vd2/Kasumi/blitter.h>
#include "FilterInstance.h"
#include "FilterFrameManualSource.h"
#include "FilterAccelEngine.h"
#include "FilterFrameRequest.h"

class IVDFilterSystemScheduler;

class VDFilterAccelDownloader : public VDFilterFrameManualSource {
	VDFilterAccelDownloader(const VDFilterAccelDownloader&);
	VDFilterAccelDownloader& operator=(const VDFilterAccelDownloader&);
public:
	VDFilterAccelDownloader();
	~VDFilterAccelDownloader();

	void Init(VDFilterAccelEngine *engine, IVDFilterFrameSource *source, const VDPixmapLayout& outputLayout, const VDPixmapLayout *sourceLayoutOverride);
	void Start(IVDFilterFrameEngine *frameEngine);
	void Stop();

	bool GetDirectMapping(sint64 outputFrame, sint64& sourceFrame, int& sourceIndex);
	sint64 GetSourceFrame(sint64 outputFrame);
	sint64 GetSymbolicFrame(sint64 outputFrame, IVDFilterFrameSource *source);
	sint64 GetNearestUniqueFrame(sint64 outputFrame);

	RunResult RunRequests(const uint32 *batchNumberLimit, int index);

protected:
	struct CallbackMsg;

	bool InitNewRequest(VDFilterFrameRequest *req, sint64 outputFrame, bool writable, uint32 batchNumber);

	static void StaticInitCallback(VDFilterAccelEngineDispatchQueue *queue, VDFilterAccelEngineMessage *message);
	static void StaticShutdownCallback(VDFilterAccelEngineDispatchQueue *queue, VDFilterAccelEngineMessage *message);
	static void StaticCleanupCallback(VDFilterAccelEngineDispatchQueue *queue, VDFilterAccelEngineMessage *message);

	VDFilterAccelEngine *mpEngine;
	IVDFilterFrameSource *mpSource;
	IVDFilterFrameEngine *mpFrameEngine;
	VDFilterAccelReadbackBuffer *mpReadbackBuffer;
	vdrefptr<VDFilterFrameRequest> mpRequest;

	VDPixmapLayout		mSourceLayout;

	VDSignal	mCompletedSignal;

	struct DownloadMsg : public VDFilterAccelEngineDownloadMsg {
		VDFilterAccelDownloader *mpDownloader;
	};

	DownloadMsg mDownloadMsg;
};

#endif

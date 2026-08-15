// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2016-2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAMEVIDEOSOURCE_H
#define f_VD2_FILTERFRAMEVIDEOSOURCE_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/blitter.h>
#include "FilterFrameManualSource.h"

class IVDVideoSource;
class IVDStreamSource;

class VDFilterFrameVideoSource : public VDFilterFrameManualSource {
	VDFilterFrameVideoSource(const VDFilterFrameVideoSource&);
	VDFilterFrameVideoSource& operator=(const VDFilterFrameVideoSource&);

public:
	VDFilterFrameVideoSource();
	~VDFilterFrameVideoSource();

	void Init(IVDVideoSource *vs, const VDPixmapLayout& layout);

	RunResult RunRequests(const uint32 *batchNumberLimit, int index);
	bool IsPreroll();

public:	// IVDFilterFrameSource
	virtual sint64 GetNearestUniqueFrame(sint64 outputFrame);

protected:
	IVDVideoSource *mpVS;
	IVDStreamSource *mpSS;
	VDFilterFrameRequest *mpRequest;

	uint32		mDecodePadding;
	VDPosition	mTargetSample;
	bool		mbFirstSample;
	bool		mbPreroll;

	vdfastvector<uint8> mBuffer;
	vdautoptr<IVDPixmapBlitter>	mpBlitter;
};

#endif

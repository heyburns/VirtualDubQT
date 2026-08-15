// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2004 Avery Lee
// Copyright (C) 2016-2020 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//


#ifndef f_TIMELINE_H
#define f_TIMELINE_H

#ifdef _MSC_VER
	#pragma once
#endif

#ifndef f_VD2_SYSTEM_REFCOUNT_H
	#include <vd2/system/refcount.h>
#endif

#ifndef f_FRAMESUBSET_H
	#include "FrameSubset.h"
#endif

class VDFraction;
class IVDVideoSource;

class IVDTimelineTimingSource : public IVDRefCount {
public:
	virtual sint64 GetStart() = 0;
	virtual sint64 GetLength() = 0;
	virtual const VDFraction GetRate() = 0;
	virtual sint64 GetPrevKey(sint64 pos) = 0;
	virtual sint64 GetNextKey(sint64 pos) = 0;
	virtual sint64 GetNearestKey(sint64 pos) = 0;
	virtual bool IsKey(sint64 pos) = 0;
	virtual bool IsNullSample(sint64 pos) = 0;
};

class VDTimeline {
public:
	VDTimeline();
	~VDTimeline();

	FrameSubset&	GetSubset() { return mSubset; }

	void SetTimingSource(IVDTimelineTimingSource *pT) { mpTiming = pT; }
	void SetFromSource();
	bool IsReset() const;

	VDPosition		GetStart()			{ return 0; }
	VDPosition		GetEnd()			{ return mSubset.getTotalFrames(); }
	VDPosition		GetLength()			{ return mSubset.getTotalFrames(); }
	VDPosition		GetNearestKey(VDPosition pos);
	VDPosition		GetNearestKeyNext(sint64 pos);
	VDPosition		GetPrevKey(VDPosition pos);
	VDPosition		GetNextKey(VDPosition pos);
	VDPosition		GetPrevDrop(VDPosition pos);
	VDPosition		GetNextDrop(VDPosition pos);
	VDPosition		GetPrevEdit(VDPosition pos);
	VDPosition		GetNextEdit(VDPosition pos);
	VDPosition		GetPrevMarker(VDPosition pos);
	VDPosition		GetNextMarker(VDPosition pos);

	VDPosition		TimelineToSourceFrame(VDPosition pos);

	void	Rescale(const VDFraction& oldRate, sint64 oldLength, const VDFraction& newRate, sint64 newLength);

	int GetMarkerCount(){ return marker.size(); }
	VDPosition GetMarker(int i);
	void ToggleMarker(VDPosition pos);
	void SetMarkerSrc(VDPosition p);
	void ClearMarker(){ marker.clear(); }
	const vdfastvector<sint64>& GetMarker(){ return marker; }

protected:
	FrameSubset	mSubset;
	vdfastvector<sint64> marker;

	vdrefptr<IVDTimelineTimingSource> mpTiming;
};

void VDCreateTimelineTimingSourceVS(IVDVideoSource *pVS, IVDTimelineTimingSource **ppTS);

#endif

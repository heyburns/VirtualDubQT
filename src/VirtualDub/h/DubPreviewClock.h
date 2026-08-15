// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_DUBPREVIEWCLOCK_H
#define f_VD2_DUBPREVIEWCLOCK_H

#include <vd2/system/event.h>
#include <vd2/system/time.h>

class IVDAsyncBlitter;
class IVDDubPreviewTimer {
public:
	virtual uint32 GetPreviewTime() = 0;
};

class VDDubPreviewClock : public IVDTimerCallback {
	VDDubPreviewClock(VDDubPreviewClock&);
	VDDubPreviewClock& operator=(const VDDubPreviewClock&);
public:
	VDDubPreviewClock();
	~VDDubPreviewClock();

	void Init(IVDDubPreviewTimer *timer, IVDAsyncBlitter *blitter, double frameRate, double frameMultiplicationFactor);
	void Shutdown();

	VDEvent<VDDubPreviewClock, uint32>& OnClockUpdated() {
		return mEventClockUpdated;
	}

protected:
	void TimerCallback();

	uint32 ReadClock() const;

	IVDDubPreviewTimer *mpTimer;
	IVDAsyncBlitter *mpBlitter;
	uint32	mBaseTime;
	double	mTicksToFrames;
	VDCallbackTimer		mFrameTimer;

	VDEvent<VDDubPreviewClock, uint32> mEventClockUpdated;
};

#endif	// f_VD2_DUBPREVIEWCLOCK_H

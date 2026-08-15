// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <vd2/system/time.h>
#include "AsyncBlitter.h"
#include "DubPreviewClock.h"

VDDubPreviewClock::VDDubPreviewClock()
	: mpTimer(NULL)
	, mpBlitter(NULL)
	, mTicksToFrames(1.0)
{
}

VDDubPreviewClock::~VDDubPreviewClock() {
}

void VDDubPreviewClock::Init(IVDDubPreviewTimer *timer, IVDAsyncBlitter *blitter, double frameRate, double frameMultiplicationFactor) {
	mpTimer = timer;
	mpBlitter = blitter;
	mBaseTime = VDGetAccurateTick();
	mTicksToFrames = frameRate / 1000.0 * frameMultiplicationFactor;

	sint32 resolution;
	if (frameRate > 100.0) {
		resolution = VDRoundToInt32(10000000.0 / frameRate);
	} else {
		resolution = VDRoundToInt32(10000000.0 / 4.0 / frameRate);

		if (resolution < 10000)
			resolution = 10000;
	}

	mFrameTimer.Init3(this, resolution, resolution, false);
}

void VDDubPreviewClock::Shutdown() {
	mFrameTimer.Shutdown();
}

void VDDubPreviewClock::TimerCallback() {
	const uint32 clock = ReadClock();

	if (mpBlitter)
		mpBlitter->setPulseClock(clock);

	mEventClockUpdated.Raise(this, clock);
}

uint32 VDDubPreviewClock::ReadClock() const {
	uint32 ticks;
	if (mpTimer)
		ticks = mpTimer->GetPreviewTime();
	else
		ticks = VDGetAccurateTick() - mBaseTime;

	return VDRoundToInt32((double)ticks * mTicksToFrames);
}

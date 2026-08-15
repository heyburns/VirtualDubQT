// VirtualDub - Video processing and capture application
// A/V interface library
//
// Copyright (C) 1998-2007 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_CAP_DSHOW_H
#define f_VD2_RIZA_CAP_DSHOW_H

#ifdef _MSC_VER
	#pragma once
#endif

class VDINTERFACE IVDCaptureDriverDShow : public IVDUnknown {
public:
	enum { kTypeID = 'cpds' };

	virtual bool GetDisableClockForPreview() = 0;
	virtual void SetDisableClockForPreview(bool enabled) = 0;

	virtual bool GetForceAudioRendererClock() = 0;
	virtual void SetForceAudioRendererClock(bool enabled) = 0;

	virtual bool GetIgnoreVideoTimestamps() = 0;
	virtual void SetIgnoreVideoTimestamps(bool enabled) = 0;
};

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 2013 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_CAPUTILS_H
#define f_VD2_CAPUTILS_H

#include <vd2/system/vdtypes.h>

class VDCaptureAutoPriority {
public:
	VDCaptureAutoPriority();
	~VDCaptureAutoPriority();

protected:
	bool		mbPowerOffState;
	bool		mbLowPowerState;
	bool		mbScreenSaverState;

	uint32		mPreviousPriorityClass;
	uint32		mPreviousThreadPriority;
};

int VDCaptureIsCatchableException(uint32 ec);
long VDCaptureHashDriverName(const char *name);

#endif

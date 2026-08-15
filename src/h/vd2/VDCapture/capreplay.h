// VirtualDub - Video processing and capture application
// A/V interface library
//
// Copyright (C) 1998-2005 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_CAPREPLAY_H
#define f_VD2_RIZA_CAPREPLAY_H

#include <vd2/system/vdtypes.h>

class IVDCaptureDriverCallback;

class VDINTERFACE IVDCaptureReplayDriver {
public:
	virtual ~IVDCaptureReplayDriver() {}

	virtual void SetChildCallback(IVDCaptureDriverCallback *pChild) = 0;

	virtual void Init(const wchar_t *filename) = 0;
	virtual bool ReplayNext() = 0;

};

IVDCaptureReplayDriver *VDCreateCaptureReplayDriver();


#endif

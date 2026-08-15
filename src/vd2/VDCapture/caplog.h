// VirtualDub - Video processing and capture application
// A/V interface library
//
// Copyright (C) 1998-2005 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_CAPLOG_H
#define f_VD2_RIZA_CAPLOG_H

#include <vd2/system/vdtypes.h>
#include <vd2/VDCapture/capdriver.h>

struct VDCaptureLogStatistics {
	double	mFrameRate;
	double	mVideoStartTime;
	double	mAudioStartTime;
};

class VDINTERFACE IVDCaptureLogFilter : public IVDCaptureDriverCallback {
public:
	virtual ~IVDCaptureLogFilter() {}

	virtual void SetChildCallback(IVDCaptureDriverCallback *pChild) = 0;

	virtual void WriteLog(const wchar_t *pszName) = 0;
};

IVDCaptureLogFilter *VDCreateCaptureLogFilter();


#endif

// VirtualDub - Video processing and capture application
// A/V interface library
//
// Copyright (C) 1998-2004 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_CAPAUDIOCOMP_H
#define f_VD2_RIZA_CAPAUDIOCOMP_H

#include <vd2/VDCapture/capdriver.h>
#include <vd2/Priss/convert.h>
#include <mmsystem.h>

/////////////////////////////////////////////////////////////////////////////

struct VDCaptureAudioCompStatus {
};

class VDINTERFACE IVDCaptureAudioCompFilter : public IVDCaptureDriverCallback {
public:
	virtual ~IVDCaptureAudioCompFilter() {}

	virtual void SetChildCallback(IVDCaptureDriverCallback *pChild) = 0;
	virtual void SetSourceSplit(bool enable) = 0;

	virtual void Init(const WAVEFORMATEX *srcFormat, const WAVEFORMATEX *dstFormat, const wchar_t* pShortNameHint) = 0;

	virtual void GetStatus(VDCaptureAudioCompStatus&) = 0;
};

IVDCaptureAudioCompFilter *VDCreateCaptureAudioCompFilter();

#endif

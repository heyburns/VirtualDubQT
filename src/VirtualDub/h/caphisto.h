// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_CAPHISTO_H
#define f_CAPHISTO_H

class IVDUIWindow;

class VDINTERFACE IVDCaptureVideoHistogram {
public:
	virtual ~IVDCaptureVideoHistogram() {}

	virtual bool Process(const VDPixmap& px, float out[256], double scale) = 0;
};

class VDINTERFACE IVDUICaptureVideoHistogram : public IVDUnknown {
public:
	enum { kTypeID = 'caph' };

	virtual void SetHistogram(const float data[256]) = 0;		// (thread-safe)
};

IVDCaptureVideoHistogram *VDCreateCaptureVideoHistogram();
IVDUIWindow *VDCreateUICaptureVideoHistogram();

#endif

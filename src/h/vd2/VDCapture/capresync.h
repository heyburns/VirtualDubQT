// VirtualDub - Video processing and capture application
// A/V interface library
//
// Copyright (C) 1998-2004 Avery Lee
// Copyright (C) 2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_CAPRESYNC_H
#define f_VD2_RIZA_CAPRESYNC_H

#include <vd2/VDCapture/capdriver.h>
#include <vd2/Priss/convert.h>

#include <vd2/system/int128.h>

/////////////////////////////////////////////////////////////////////////////

class VDCaptureAudioRateEstimator {
public:
	VDCaptureAudioRateEstimator() { Reset(); }
	void Reset();
	void AddSample(sint64 x, sint64 y);
	int GetCount() const { return mSamples; }
	bool GetSlope(double&) const;
	bool GetXIntercept(double slope, double&) const;
	bool GetYIntercept(double slope, double&) const;

protected:
	sint64		mX;
	sint64		mY;
	vdint128	mX2;
	vdint128	mXY;

	int			mSamples;
};

/////////////////////////////////////////////////////////////////////////////

struct VDCaptureResyncStatus {
	sint32		mVideoTimingAdjust;
	float		mAudioResamplingRate;
	float		mVideoResamplingRate;
	float		mMeasuredLatency;
	float		mCurrentLatency;
};

class VDINTERFACE IVDCaptureResyncFilter : public IVDCaptureDriverCallback {
public:
	enum Mode {
		kModeNone,
		kModeResampleVideo,
		kModeResampleAudio,

		kModeCount
	};

	virtual ~IVDCaptureResyncFilter() {}

	virtual void SetChildCallback(IVDCaptureDriverCallback *pChild) = 0;
	virtual void SetLogCallback(IVDCaptureDriverCallback *pLog) = 0;
	virtual void SetProfiler(IVDCaptureProfiler *pProfiler) = 0;
	virtual void SetVideoRate(double fps) = 0;
	virtual void SetAudioRate(double bytesPerSec) = 0;
	virtual void SetAudioChannels(int chans) = 0;
	virtual void SetAudioFormat(VDAudioSampleType sampleType) = 0;
	virtual void SetResyncMode(Mode mode) = 0;
	virtual void EnableVideoTimingCorrection(bool en) = 0;
	virtual void EnableVideoDrops(bool enable) = 0;
	virtual void EnableVideoInserts(bool enable) = 0;
	virtual void SetVideoInsertLimit(int insertLimit) = 0;
	virtual void SetFixedAudioLatency(int latencyInMilliseconds) = 0;
	virtual void SetLimitedAutoAudioLatency(int sampleLimit) = 0;
	virtual void SetAutoAudioLatency() = 0;
	virtual void EnableAudioClock(bool enable) = 0;

	virtual void GetStatus(VDCaptureResyncStatus&) = 0;
};

IVDCaptureResyncFilter *VDCreateCaptureResyncFilter();




#endif

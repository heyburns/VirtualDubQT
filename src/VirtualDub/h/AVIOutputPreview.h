// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2006 Avery Lee
// Copyright (C) 2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUTPREVIEW_H
#define f_AVIOUTPUTPREVIEW_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <vd2/Riza/audioout.h>
#include "AVIOutput.h"
#include "DubPreviewClock.h"

class IVDAudioOutput;

class AVIAudioPreviewOutputStream : public AVIOutputStream, public IVDDubPreviewTimer {
public:
	AVIAudioPreviewOutputStream();
	~AVIAudioPreviewOutputStream();

	void SetVBRMode(bool enable) { mbVBRMode = enable; }

	void write(uint32 flags, const void *pBuffer, uint32 cbBuffer, uint32 lSamples);
	void partialWriteBegin(uint32 flags, uint32 bytes, uint32 samples);
	void partialWrite(const void *pBuffer, uint32 cbBuffer);
	void partialWriteEnd();
	void finish();
	void finalize();
	void flush();

public:
	void start();
	double GetPosition();		// time in seconds
	long getAvailable();
	bool isFrozen();
	bool isSilent();
	bool isOverflow();
	void stop();

protected:
	uint32 GetPreviewTime();

private:
	void initAudio();
	void UpdatePlaybackTime(uint32 bytePos);

	vdautoptr<IVDAudioOutput> mpAudioOut;
	bool mbInitialized;
	bool mbStarted;
	bool mbVBRMode;

	sint64	mTotalBytes;
	sint64	mTotalSamples;
	uint32	mBufferLevel;

	uint32	mLastPlayTime;
	uint32	mLastCPUTime;
	VDAtomicInt mbFinished;

	VDCriticalSection	mLock;
};

class AVIOutputPreview : public AVIOutput {
private:
public:
	AVIOutputPreview();
	~AVIOutputPreview();

	IVDMediaOutputStream *createVideoStream();
	IVDMediaOutputStream *createAudioStream();

	bool init(const wchar_t *szFile);
	void finalize();
};

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2016-2017 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_DUBPROCESSVIDEODISPLAY_H
#define f_VD2_DUBPROCESSVIDEODISPLAY_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/atomic.h>
#include <vd2/Kasumi/pixmaputils.h>

class VDLoopThrottle;
class VDRenderOutputBuffer;
class VDRenderOutputBufferTracker;
class IVDAsyncBlitter;
class IVDVideoDisplay;
class DubOptions;
class IVDVideoCompressor;
class IVDVideoDecompressor;

class VDDubVideoProcessorDisplay {
	VDDubVideoProcessorDisplay(const VDDubVideoProcessorDisplay&);
	VDDubVideoProcessorDisplay& operator=(const VDDubVideoProcessorDisplay&);
public:
	VDDubVideoProcessorDisplay();
	~VDDubVideoProcessorDisplay();

	void SetThreadInfo(VDLoopThrottle *throttle);
	void SetOptions(const DubOptions *opts);
	void SetInputDisplay(IVDVideoDisplay *pVideoDisplay);
	void SetOutputDisplay(IVDVideoDisplay *pVideoDisplay);
	void SetBlitter(IVDAsyncBlitter *blitter);
	void SetVideoCompressor(IVDVideoCompressor *pCompressor);
	void SetVideoSource(IVDVideoSource *pVideo);

	sint32 GetLatency() const;
	uint32 GetDisplayClock() const;
	void AdvanceFrame();

	bool TryLockInputChannel(sint32 timeout);
	void UnlockInputChannel();

	bool TryRevokeOutputBuffer(VDRenderOutputBuffer **buffer);

	void UnlockAndDisplay(bool forceDisplay, VDRenderOutputBuffer *pBuffer, bool outputValid);

	void ScheduleUpdate();
	void CheckForDecompressorSwitch();
	void UpdateDecompressedVideo(const void *data, uint32 size, bool isKey);

protected:
	static bool AsyncReinitDisplayCallback(int pass, sint64 timelinePos, void *pThisAsVoid, void *, bool aborting);
	static bool StaticAsyncUpdateInputCallback(int pass, sint64 timelinePos, void *pThisAsVoid, void*, bool aborting);
	static bool StaticAsyncUpdateOutputCallback(int pass, sint64 timelinePos, void *pThisAsVoid, void *pBuffer, bool aborting);
	bool AsyncUpdateInputCallback(int pass, VDPosition pos, bool aborting);
	bool AsyncUpdateOutputCallback(int pass, VDPosition pos, VDRenderOutputBuffer *pBuffer, bool aborting);

	const DubOptions	*mpOptions;
	IVDVideoCompressor	*mpVideoCompressor;
	VDLoopThrottle		*mpLoopThrottle;
	bool				mbInputLocked;

	// DISPLAY
	vdrefptr<IVDVideoSource>	mpVideoSource;
	uint32				mFramesToDrop;
	IVDAsyncBlitter		*mpBlitter;
	IVDVideoDisplay		*mpInputDisplay;
	IVDVideoDisplay		*mpOutputDisplay;
	VDAtomicInt			mRefreshFlag;

	// DECOMPRESSION PREVIEW
	vdautoptr<IVDVideoDecompressor>	mpVideoDecompressor;
	bool				mbVideoDecompressorEnabled;
	bool				mbVideoDecompressorPending;
	bool				mbVideoDecompressorErrored;
	VDPixmapBuffer		mVideoDecompBuffer;

public:
	IDubStatusHandler	*mpStatusHandler;
};

#endif	// f_VD2_DUBPROCESSVIDEODISPLAY_H

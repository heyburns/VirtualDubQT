// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2017-2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_DUBPROCESS_H
#define f_VD2_DUBPROCESS_H

#include <vd2/system/thread.h>
#include <vd2/system/profile.h>
#include <vd2/Kasumi/pixmaputils.h>
#include "DubProcessVideo.h"
#include "DubPreviewClock.h"

class IVDMediaOutput;
class IVDMediaOutputStream;
class IVDDubberOutputSystem;
class VDAudioPipeline;
class DubOptions;
class IVDVideoSource;
class IVDVideoDisplay;
class AudioStream;
class VDStreamInterleaver;
class IVDVideoCompressor;
class IVDAsyncBlitter;
class IDubStatusHandler;
struct VDRenderVideoPipeFrameInfo;
class VDRenderOutputBufferTracker;
class VDRenderOutputBuffer;
class VDThreadedVideoCompressor;
class FilterSystem;
class VDFilterFrameRequest;
class IVDFilterFrameClientRequest;
class VDFilterFrameManualSource;
class VDTextOutputStream;

class VDDubProcessThread : public VDThread, public IVDDubVideoProcessorCallback {
public:
	VDDubProcessThread();
	~VDDubProcessThread();

	bool IsCompleted() const { return mbCompleted; }

	IVDFilterSystemScheduler *GetVideoFilterScheduler();

	void PreInit();

	void SetParent(IDubberInternal *pParent);
	void SetAbortSignal(VDAtomicInt *pAbort);
	void SetStatusHandler(IDubStatusHandler *pStatusHandler);
	void SetInputDisplay(IVDVideoDisplay *pVideoDisplay);
	void SetOutputDisplay(IVDVideoDisplay *pVideoDisplay);
	void SetVideoOutput(const VDPixmapLayout& layout, int mode);
	void SetVideoSources(IVDVideoSource *const *pVideoSources, uint32 count);
	void SetVideoFrameSource(VDFilterFrameManualSource *fs);
	void SetAudioSourcePresent(bool present);
	void SetAudioCorrector(AudioStreamL3Corrector *pCorrector);
	void SetAudioStats(AudioStats *pStats);
	void SetVideoCompressor(IVDVideoCompressor *pCompressor, int maxThreads);
	void SetVideoFilterSystem(FilterSystem *fs);
	void SetVideoRequestQueue(VDDubFrameRequestQueue *q);
	void SetIODirect(VDDubIOThread *pIODirect);

	void SetPriority(int priority);

	void Init(const DubOptions& opts, const VDRenderFrameMap *frameMap, DubVideoStreamInfo *pvsi, IVDDubberOutputSystem *pOutputSystem, AVIPipe *pVideoPipe, VDAudioPipeline *pAudioPipe, VDStreamInterleaver *pStreamInterleaver);
	void Shutdown();

	void Abort();
	void UpdateFrames();

	bool GetError(MyError& e) {
		if (mbError) {
			e.TransferFrom(mError);
			return true;
		}
		return false;
	}

	uint32 GetActivityCounter() {
		return mActivityCounter;
	}

	const char *GetCurrentAction() {
		return mpCurrentAction;
	}

	VDSignal *GetBlitterSignal();

	void SetThrottle(float f);
	float GetActivityRatio() const { return mLoopThrottle.GetActivityRatio(); }

	void DumpStatus(VDTextOutputStream& os);

protected:
	void NextSegment();

	bool WriteAudio(int stream, sint32 count);

	void ThreadRun();
	void UpdateAudioDelay();
	void UpdateAudioFormat();
	void UpdateAudioStreamRate();

	void OnVideoStreamEnded();
	void OnFirstFrameWritten();

	const DubOptions		*opt;

	VDStreamInterleaver		*mpInterleaver;
	VDLoopThrottle			mLoopThrottle;
	IDubberInternal			*mpParent;

	// OUTPUT
	IVDMediaOutput			*mpAVIOut;
	IVDMediaOutputStream	*mpAudioOut;			// alias: AVIout->audioOut
	IVDMediaOutputStream	*mpVideoOut;			// alias: AVIout->videoOut
	IVDDubberOutputSystem	*mpOutputSystem;

	// AUDIO SECTION
	VDAudioPipeline			*mpAudioPipe;
	AudioStreamL3Corrector	*mpAudioCorrector;
	AudioStats	*mpAudioStats;
	bool				mbAudioPresent;
	bool				mbAudioEnded;
	uint64				mAudioSamplesWritten;
	vdfastvector<char>	mAudioBuffer;

	// VIDEO SECTION
	AVIPipe					*mpVideoPipe;
	bool				mbVideoEnded;
	bool				mbVideoPushEnded;

	DubVideoStreamInfo	*mpVInfo;
	IVDAsyncBlitter		*mpBlitter;
	IDubStatusHandler	*mpStatusHandler;

	typedef vdfastvector<IVDVideoSource *> VideoSources;
	VideoSources		mVideoSources;

	// PREVIEW
	bool				mbPreview;
	bool				mbFirstPacket;

	// ERROR HANDLING
	MyError				mError;
	bool				mbError;
	bool				mbCompleted;
	VDAtomicInt			*mpAbort;

	const char			*volatile mpCurrentAction;
	VDAtomicInt			mActivityCounter;

	VDDubPreviewClock	mPreviewClock;

	VDDubVideoProcessor	mVideoProcessor;
};

#endif

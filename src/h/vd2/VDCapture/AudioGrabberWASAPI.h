// VirtualDub - Video processing and capture application
// Video capture library
//
// Copyright (C) 1998-2012 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDCAPTURE_AUDIOGRABBERWASAPI_H
#define f_VD2_VDCAPTURE_AUDIOGRABBERWASAPI_H

#pragma once

#include <windows.h>
#include <mmreg.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/atomic.h>
#include <vd2/system/thread.h>
#include <vd2/system/VDRingBuffer.h>

struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;
struct IAudioRenderClient;

class IVDAudioGrabberCallbackWASAPI {
public:
	virtual void ReceiveAudioDataWASAPI() = 0;
};

class VDAudioGrabberWASAPI : public VDThread {
	VDAudioGrabberWASAPI(const VDAudioGrabberWASAPI&);
	VDAudioGrabberWASAPI& operator=(const VDAudioGrabberWASAPI&);
public:
	VDAudioGrabberWASAPI();
	~VDAudioGrabberWASAPI();

	bool GetFormat(vdstructex<WAVEFORMATEX>& format);

	bool Init(IVDAudioGrabberCallbackWASAPI *cb);
	void Shutdown();

	bool Start();
	void Stop();

	uint32 ReadData(void *dst, size_t bytes);

protected:
	void ThreadRun();

	bool InitEndpoint();
	void ShutdownEndpoint();

	bool InitLocal();
	void RunLocal();
	void ShutdownLocal();

	enum State {
		kStateNone,
		kStateInitFailed,
		kStateInited,
		kStateStartFailed,
		kStateRunning
	};

	VDAtomicInt mCurrentState;
	VDAtomicInt mRequestedState;
	VDSignal mStateChangeRequested;
	VDSignal mStateChangeCompleted;

	IVDAudioGrabberCallbackWASAPI *mpCB;

	IMMDevice *mpMMDevice;
	IAudioClient *mpAudioClientCapture;
	IAudioClient *mpAudioClientRender;
	IAudioCaptureClient *mpAudioCaptureClient;
	IAudioRenderClient *mpAudioRenderClient;
	HANDLE mhEventRender;
	uint32 mOutputBufferFrames;
	uint32 mBytesPerFrame;
	uint32 mChannels;
	vdstructex<WAVEFORMATEX> mMixFormat;

	VDRingBuffer<sint16> mTransferBuffer;
};

#endif	// f_VD2_VDCAPTURE_AUDIOGRABBERWASAPI_H

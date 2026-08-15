// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2016-2020 Anton Shekhovtsov
// Copyright (C) 2023-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#define f_PREFS_CPP

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>

#include <vd2/system/w32assist.h>
#include <vd2/Dita/interface.h>
#include <vd2/Dita/services.h>
#include <vd2/system/registry.h>
#include <vd2/system/fileasync.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/system/registrymemory.h>

#include "resource.h"
#include "helpfile.h"

#include "gui.h"
#include "oshelper.h"
#include "dub.h"
#include "dubstatus.h"
#include "prefs.h"

extern HINSTANCE g_hInst;

namespace {
	struct VDPreferences2 {
		int iPreviewPriority = 0;
		int iDubPriority = 0;
		bool fAttachExtension = true;

		int iSceneCutThreshold  = 50 * 16;
		int iSceneFadeThreshold =  4 * 16;

		bool fAVIRestrict1Gb  = false;
		bool fNoCorrectLayer3 = false;

		int				mDisplayAPI = kDisplayDirect3D9;
		bool			mbDisplayAllowDirectXOverlays; // DirectDraw
		bool			mbDisplayEnableHighPrecision;  // D3D9
		bool			mbDisplayEnableFX;             // D3D9
		bool			mbDisplayUseDXWithTS;
		bool			mbDisplayEnableVSync;
		bool			mbDisplayEnableBackgroundFallback;
		bool			mbDisplayEnableDebugInfo;
		enum DisplaySecondaryMode {
			kDisplaySecondaryMode_Disable,
			kDisplaySecondaryMode_ForcePrimary,
			kDisplaySecondaryMode_AutoSwitch,
			kDisplaySecondaryModeCount
		};
		DisplaySecondaryMode mDisplaySecondaryMode;
		VDStringW		mD3DFXFile;                    // D3D9 + FX

		bool			mbUIRememberZoom;

		VDStringW		mTimelineFormat;
		int				mTimeFormat;
		uint32			mTimelinePageSize;
		bool			mbTimelinePageMode;
		bool			mbTimelineWarnReloadTruncation;
		int				mTimelineScaleTrack;
		int				mTimelineScaleInfo;
		int				mTimelineScaleButtons;
		bool			mbAllowDirectYCbCrDecoding;
		bool			mbConfirmRenderAbort;
		bool			mbConfirmExit;
		bool			mbRenderWarnNoAudio;
		bool			mbEnableAVIAlignmentThreshold;
		bool			mbEnableAVIVBRWarning;
		bool			mbEnableAVINonZeroStartWarning;
		bool			mbPreferInternalVideoDecoders;
		bool			mbPreferInternalAudioDecoders;
		bool			mbUseVideoFccHandler;
		bool			mbAVIIgnoreIndex;
		bool			mbAVIRekey;
		bool			mbAVITestRaw;

		uint32			mAVIAlignmentThreshold;
		uint32			mRenderOutputBufferSize;
		uint32			mRenderWaveBufferSize;
		uint32			mRenderVideoBufferCount;
		uint32			mRenderAudioBufferSeconds;
		uint32			mRenderThrottlePercent;
		bool			mbRenderBackgroundPriority;
		bool			mbRenderInhibitSystemSleep;
		bool			mbRenderShowFrames;

		uint32			mAVISuperindexLimit;
		uint32			mAVISubindexLimit;

		VDFraction		mImageSequenceFrameRate;

		int				mVideoCompressionThreads;

		VDStringW		mAudioPlaybackDeviceKey;

		uint32			mEnabledCPUFeatures;

		bool			mbFilterAccelEnabled;
		bool			mbFilterAccelDebugEnabled;		// NOT saved
		uint32			mFilterProcessAhead;
		sint32			mFilterThreads;

		bool			mbBatchStatusWindowEnabled;

		bool			mbAutoRecoverEnabled;
		bool			mbUseUserProfile;
		int				mMRUSize;
		int				mHistoryClearCounter;		// NOT saved

		VDPreferences2()
			: mMRUSize(4)
		{
		}

		bool displayChanged(VDPreferences2& old) {
			if (old.mDisplayAPI != mDisplayAPI) {
				return true;
			}
			if (old.mbDisplayEnableHighPrecision != mbDisplayEnableHighPrecision) {
				return true;
			}
			if (old.mbDisplayEnableFX != mbDisplayEnableFX) {
				return true;
			}
			if (old.mbDisplayUseDXWithTS != mbDisplayUseDXWithTS) {
				return true;
			}
			if (old.mbDisplayAllowDirectXOverlays != mbDisplayAllowDirectXOverlays) {
				return true;
			}
			if (old.mbDisplayEnableVSync != mbDisplayEnableVSync) {
				return true;
			}
			if (old.mbDisplayEnableBackgroundFallback != mbDisplayEnableBackgroundFallback) {
				return true;
			}
			if (old.mDisplaySecondaryMode != mDisplaySecondaryMode) {
				return true;
			}
			if (old.mD3DFXFile != mD3DFXFile) {
				return true;
			}
			return false;
		}

		bool timelineChanged(VDPreferences2& old) {
			if (old.mTimelineFormat!=mTimelineFormat)
				return true;
			if (old.mTimeFormat!=mTimeFormat)
				return true;
			if (old.mTimelineScaleTrack!=mTimelineScaleTrack)
				return true;
			if (old.mTimelineScaleInfo!=mTimelineScaleInfo)
				return true;
			if (old.mTimelineScaleButtons!=mTimelineScaleButtons)
				return true;
			return false;
		}

	} g_prefs2;
}

void VDSavePreferences(VDPreferences2& prefs);

VDRegistryProviderMemory* g_pShadowRegistry;

void VDPreferencesUpdated();

////////////////////////////////////////////////////////////////

class VDDialogPreferencesGeneral : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesGeneral(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			//SetValue(100, mPrefs.iPreviewDepth);
			SetValue(101, mPrefs.mbUIRememberZoom);
			SetValue(102, mPrefs.fAttachExtension);
			SetValue(103, mPrefs.mbConfirmExit);

			pBase->ExecuteAllLinks();
			return true;
		case kEventSync:
		case kEventDetach:
			//mPrefs.iPreviewDepth		= (char)GetValue(100);
			mPrefs.mbUIRememberZoom	= GetValue(101) != 0;
			mPrefs.fAttachExtension	= (char)GetValue(102);
			mPrefs.mbConfirmExit = GetValue(103) != 0;
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesDisplay : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesDisplay(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			switch (mPrefs.mDisplayAPI) {
			case kDisplayGDI:        SetValue(300, 0); break;
			case kDisplayDirectDraw: SetValue(300, 1); break;
			default:
			case kDisplayDirect3D9:  SetValue(300, 2); break;
			case kDisplayDirect3D11: SetValue(300, 3); break;
			case kDisplayOpenGL:     SetValue(300, 4); break;
			}

			SetValue(109, mPrefs.mbDisplayEnableHighPrecision);
			SetValue(105, mPrefs.mbDisplayEnableFX);
			SetValue(102, mPrefs.mbDisplayUseDXWithTS);
			SetValue(107, mPrefs.mbDisplayAllowDirectXOverlays);
			SetValue(106, mPrefs.mbDisplayEnableVSync);
			SetValue(110, mPrefs.mbDisplayEnableBackgroundFallback);
			SetValue(108, mPrefs.mbDisplayEnableDebugInfo);

			SetValue(111, (VDPreferences2::kDisplaySecondaryModeCount - 1) - mPrefs.mDisplaySecondaryMode);
			SetCaption(230, mPrefs.mD3DFXFile.c_str());

			pBase->ExecuteAllLinks();
			return true;
		case kEventSync:
		case kEventDetach:
			switch (GetValue(300)) {
			case 0: mPrefs.mDisplayAPI = kDisplayGDI;        break;
			case 1: mPrefs.mDisplayAPI = kDisplayDirectDraw; break;
			default:
			case 2: mPrefs.mDisplayAPI = kDisplayDirect3D9;  break;
			case 3: mPrefs.mDisplayAPI = kDisplayDirect3D11; break;
			case 4: mPrefs.mDisplayAPI = kDisplayOpenGL;     break;
			}

			mPrefs.mbDisplayEnableHighPrecision      = GetValue(109) != 0;
			mPrefs.mbDisplayEnableFX                 = GetValue(105) != 0;
			mPrefs.mbDisplayUseDXWithTS              = GetValue(102) != 0;
			mPrefs.mbDisplayAllowDirectXOverlays     = GetValue(107) != 0;
			mPrefs.mbDisplayEnableVSync              = GetValue(106) != 0;
			mPrefs.mbDisplayEnableBackgroundFallback = GetValue(110) != 0;
			mPrefs.mbDisplayEnableDebugInfo          = GetValue(108) != 0;

			mPrefs.mDisplaySecondaryMode = (VDPreferences2::DisplaySecondaryMode)((VDPreferences2::kDisplaySecondaryModeCount - 1) - GetValue(111));
			mPrefs.mD3DFXFile = GetCaption(230);
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesCPU : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesCPU(VDPreferences2& p) : mPrefs(p) {}

	void loadOptions() {
		long exts = mPrefs.mEnabledCPUFeatures;
		if (!(mPrefs.mEnabledCPUFeatures & OPTF_FORCE))
			exts = CPUCheckForExtensions();

		SetValue(100, 0 != (mPrefs.mEnabledCPUFeatures & OPTF_FORCE));
		SetValue(200, 0 != (exts & OPTF_FPU));
		SetValue(201, 0 != (exts & OPTF_MMX));
		SetValue(202, 0 != (exts & OPTF_INTEGER_SSE));
		SetValue(203, 0 != (exts & OPTF_SSE));
		SetValue(204, 0 != (exts & OPTF_SSE2));
		SetValue(205, 0 != (exts & OPTF_3DNOW));
		SetValue(206, 0 != (exts & OPTF_3DNOW_EXT));
		SetValue(207, 0 != (exts & OPTF_SSE3));
		SetValue(208, 0 != (exts & OPTF_SSSE3));
		SetValue(209, 0 != (exts & OPTF_SSE4_1));
		SetValue(210, 0 != (exts & OPTF_AVX));
		SetValue(211, 0 != (exts & CPUF_SUPPORTS_SSE42));
		SetValue(212, 0 != (exts & CPUF_SUPPORTS_AVX2));
		SetValue(213, 0 != (exts & CPUF_SUPPORTS_AVX512F));
	}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventSelect:
			if (id==100) {
				if (!GetValue(100)) {
					mPrefs.mEnabledCPUFeatures &= ~OPTF_FORCE;
					loadOptions();
				}
			}
			return true;
		case kEventAttach:
			mpBase = pBase;
			loadOptions();
			pBase->ExecuteAllLinks();
			return true;
		case kEventSync:
		case kEventDetach:
			mPrefs.mEnabledCPUFeatures = 0;
			if (GetValue(100)) mPrefs.mEnabledCPUFeatures |= OPTF_FORCE;
			if (GetValue(200)) mPrefs.mEnabledCPUFeatures |= OPTF_FPU;
			if (GetValue(201)) mPrefs.mEnabledCPUFeatures |= OPTF_MMX;
			if (GetValue(202)) mPrefs.mEnabledCPUFeatures |= OPTF_INTEGER_SSE;
			if (GetValue(203)) mPrefs.mEnabledCPUFeatures |= OPTF_SSE;
			if (GetValue(204)) mPrefs.mEnabledCPUFeatures |= OPTF_SSE2;
			if (GetValue(205)) mPrefs.mEnabledCPUFeatures |= OPTF_3DNOW;
			if (GetValue(206)) mPrefs.mEnabledCPUFeatures |= OPTF_3DNOW_EXT;
			if (GetValue(207)) mPrefs.mEnabledCPUFeatures |= OPTF_SSE3;
			if (GetValue(208)) mPrefs.mEnabledCPUFeatures |= OPTF_SSSE3;
			if (GetValue(209)) mPrefs.mEnabledCPUFeatures |= OPTF_SSE4_1;
			if (GetValue(210)) mPrefs.mEnabledCPUFeatures |= OPTF_AVX;
			if (GetValue(211)) mPrefs.mEnabledCPUFeatures |= CPUF_SUPPORTS_SSE42;
			if (GetValue(212)) mPrefs.mEnabledCPUFeatures |= CPUF_SUPPORTS_AVX2;
			if (GetValue(213)) mPrefs.mEnabledCPUFeatures |= CPUF_SUPPORTS_AVX512F;
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesScene : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesScene(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			{
				mpBase = pBase;

				IVDUIWindow *pWin = mpBase->GetControl(100);
				IVDUITrackbar *pTrackbar = vdpoly_cast<IVDUITrackbar *>(pWin);

				if (pTrackbar) {
					pTrackbar->SetRange(0, 255);
					pWin->SetValue(mPrefs.iSceneCutThreshold ? 256 - ((mPrefs.iSceneCutThreshold+8)>>4) : 0);
				}

				pWin = mpBase->GetControl(200);
				pTrackbar = vdpoly_cast<IVDUITrackbar *>(pWin);

				if (pTrackbar) {
					pTrackbar->SetRange(0, 255);
					pWin->SetValue(mPrefs.iSceneFadeThreshold);
				}

				SyncLabels();
				pBase->ExecuteAllLinks();
			}
			return true;
		case kEventSync:
		case kEventDetach:
			{
				int v = GetValue(100);
				mPrefs.iSceneCutThreshold = v ? (256-v)<<4 : 0;
				mPrefs.iSceneFadeThreshold = GetValue(200);
			}
			return true;

		case kEventSelect:
			SyncLabels();
			return true;
		}
		return false;
	}

	void SyncLabels() {
		int v = GetValue(100);

		if (!v)
			SetCaption(101, L"Off");
		else
			SetCaption(101, VDswprintf(L"%u", 1, &v).c_str());

		v = GetValue(200);
		if (!v)
			SetCaption(201, L"Off");
		else
			SetCaption(201, VDswprintf(L"%u", 1, &v).c_str());
	}
};

class VDDialogPreferencesAVI : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesAVI(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			SetValue(111, 0 != (mPrefs.mbAVITestRaw));
			SetValue(100, 0 != (mPrefs.fAVIRestrict1Gb));
			SetValue(101, 0 != (mPrefs.fNoCorrectLayer3));
			SetValue(102, mPrefs.mbAllowDirectYCbCrDecoding);
			SetValue(103, mPrefs.mbEnableAVIAlignmentThreshold);
			{
				unsigned v = mPrefs.mAVIAlignmentThreshold;
				SetCaption(200, VDswprintf(L"%u", 1, &v).c_str());
				v = mPrefs.mAVISuperindexLimit;
				SetCaption(201, VDswprintf(L"%u", 1, &v).c_str());
				v = mPrefs.mAVISubindexLimit;
				SetCaption(202, VDswprintf(L"%u", 1, &v).c_str());
			}
			SetValue(104, mPrefs.mbPreferInternalVideoDecoders);
			SetValue(105, mPrefs.mbPreferInternalAudioDecoders);
			SetValue(106, mPrefs.mbEnableAVIVBRWarning);
			SetValue(108, mPrefs.mbEnableAVINonZeroStartWarning);
			SetValue(107, mPrefs.mbUseVideoFccHandler);
			SetValue(109, mPrefs.mbAVIIgnoreIndex);
			SetValue(110, mPrefs.mbAVIRekey);
			pBase->ExecuteAllLinks();
			return true;
		case kEventDetach:
		case kEventSync:
			mPrefs.mbAVITestRaw = 0 != GetValue(111);
			mPrefs.fAVIRestrict1Gb = 0 != GetValue(100);
			mPrefs.fNoCorrectLayer3 = 0 != GetValue(101);
			mPrefs.mbAllowDirectYCbCrDecoding = 0!=GetValue(102);
			if (mPrefs.mbEnableAVIAlignmentThreshold = (0 != GetValue(103)))
				mPrefs.mAVIAlignmentThreshold = (uint32)wcstoul(GetCaption(200).c_str(), 0, 10);
			mPrefs.mbPreferInternalVideoDecoders = 0!=GetValue(104);
			mPrefs.mbPreferInternalAudioDecoders = 0!=GetValue(105);
			mPrefs.mAVISuperindexLimit = (uint32)wcstoul(GetCaption(201).c_str(), 0, 10);
			if (mPrefs.mAVISuperindexLimit < 1)
				mPrefs.mAVISuperindexLimit = 1;
			mPrefs.mAVISubindexLimit = (uint32)wcstoul(GetCaption(202).c_str(), 0, 10);
			if (mPrefs.mAVISubindexLimit < 1)
				mPrefs.mAVISubindexLimit = 1;
			mPrefs.mbEnableAVIVBRWarning = 0!=GetValue(106);
			mPrefs.mbEnableAVINonZeroStartWarning = 0 != GetValue(108);
			mPrefs.mbUseVideoFccHandler = 0 != GetValue(107);
			mPrefs.mbAVIIgnoreIndex = 0 != GetValue(109);
			mPrefs.mbAVIRekey = 0 != GetValue(110);
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesTimeline : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesTimeline(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();
			SetCaption(200, mPrefs.mTimelineFormat.c_str());
			SetValue(201, mPrefs.mTimeFormat);
			SetValue(100, mPrefs.mbTimelineWarnReloadTruncation);
			{
				unsigned v = mPrefs.mTimelinePageSize;
				SetCaption(101, VDswprintf(L"%u", 1, &v).c_str());
			}
			SetValue(102, mPrefs.mbTimelinePageMode);
			SetCaption(104, VDStringW().sprintf(L"%u", mPrefs.mTimelineScaleTrack).c_str());
			SetCaption(105, VDStringW().sprintf(L"%u", mPrefs.mTimelineScaleInfo).c_str());
			SetCaption(106, VDStringW().sprintf(L"%u", mPrefs.mTimelineScaleButtons).c_str());
			return true;
		case kEventDetach:
		case kEventSync:
			mPrefs.mTimelineFormat = GetCaption(200);
			mPrefs.mTimeFormat = GetValue(201);
			mPrefs.mbTimelineWarnReloadTruncation = 0 != GetValue(100);
			mPrefs.mTimelinePageSize = (uint32)wcstoul(GetCaption(101).c_str(), 0, 10);
			mPrefs.mbTimelinePageMode = GetValue(102) != 0;
			mPrefs.mTimelineScaleTrack = (uint32)wcstoul(GetCaption(104).c_str(), 0, 10);
			mPrefs.mTimelineScaleInfo = (uint32)wcstoul(GetCaption(105).c_str(), 0, 10);
			mPrefs.mTimelineScaleButtons = (uint32)wcstoul(GetCaption(106).c_str(), 0, 10);
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesDub : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesDub(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();
			SetValue(100, mPrefs.mbConfirmRenderAbort);
			SetValue(101, mPrefs.mbRenderWarnNoAudio);
			SetValue(102, mPrefs.mbRenderInhibitSystemSleep);
			SetValue(103, mPrefs.mbRenderShowFrames);
			return true;
		case kEventDetach:
		case kEventSync:
			mPrefs.mbConfirmRenderAbort = 0 != GetValue(100);
			mPrefs.mbRenderWarnNoAudio = 0 != GetValue(101);
			mPrefs.mbRenderInhibitSystemSleep = 0 != GetValue(102);
			mPrefs.mbRenderShowFrames = 0 != GetValue(103);
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesImages : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesImages(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();
			{
				char buf[128];
				sprintf(buf, "%.4f", mPrefs.mImageSequenceFrameRate.asDouble());

				VDFraction fr2(mPrefs.mImageSequenceFrameRate);
				VDVERIFY(fr2.Parse(buf));

				if (fr2 != mPrefs.mImageSequenceFrameRate)
					sprintf(buf, "%u/%u (~%.7f)", mPrefs.mImageSequenceFrameRate.getHi(), mPrefs.mImageSequenceFrameRate.getLo(), mPrefs.mImageSequenceFrameRate.asDouble());

				SetCaption(100, VDTextAToW(buf).c_str());
			}
			return true;
		case kEventDetach:
		case kEventSync:
			{
				const VDStringA s(VDTextWToA(GetCaption(100)));
				VDFraction fr;
				unsigned hi, lo;
				bool failed = false;
				if (2==sscanf(s.c_str(), " %u / %u", &hi, &lo)) {
					if (!lo)
						failed = true;
					else
						fr = VDFraction(hi, lo);
				} else if (!fr.Parse(s.c_str()) || fr.asDouble() >= 1000000.0) {
					failed = true;
				}

				if (fr.getHi() == 0)
					failed = true;

				if (!failed) {
					mPrefs.mImageSequenceFrameRate = fr;
				}
			}
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesThreading : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesThreading(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();

			{
				unsigned i = mPrefs.mVideoCompressionThreads;
				SetCaption(100, VDswprintf(L"%u", 1, &i).c_str());
			}

			if (mPrefs.mFilterThreads < 0)
				SetValue(101, 0);
			else
				SetValue(101, mPrefs.mFilterThreads + 1);

			SetValue(102, mPrefs.mFilterProcessAhead);

			SetValue(103, mPrefs.iPreviewPriority);
			SetValue(104, mPrefs.iDubPriority);

			pWin = mpBase->GetControl(106);
			{
				IVDUITrackbar *pTrackbar = vdpoly_cast<IVDUITrackbar *>(pWin);

				if (pTrackbar) {
					pTrackbar->SetRange(1, 10);
					pWin->SetValue((mPrefs.mRenderThrottlePercent + 5) / 10);
				}
			}

			SetValue(105, mPrefs.mbRenderBackgroundPriority);

			return true;
		case kEventDetach:
		case kEventSync:
			mPrefs.mVideoCompressionThreads = std::min<uint32>(wcstoul(GetCaption(100).c_str(), 0, 10), 32);
			mPrefs.mFilterThreads = GetValue(101) - 1;
			mPrefs.mFilterProcessAhead = VDClampToUint32(GetValue(102));
			mPrefs.iPreviewPriority	= (char)GetValue(103);
			mPrefs.iDubPriority		= (char)GetValue(104);
			mPrefs.mbRenderBackgroundPriority = GetValue(105) != 0;
			mPrefs.mRenderThrottlePercent			= GetValue(106) * 10;
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesPlayback : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesPlayback(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();

			{
				UINT numDevices = waveOutGetNumDevs();
				IVDUIWindow *win = pBase->GetControl(100);
				IVDUIList *list = vdpoly_cast<IVDUIList *>(win);

				mPlaybackDeviceKeys.clear();
				mPlaybackDeviceKeys.resize(numDevices + 1);

				if (list) {
					list->AddItem(L"Default system playback device");

					for(UINT i=0; i<numDevices; ++i) {
						WAVEOUTCAPSW caps = {0};

						if (MMSYSERR_NOERROR == waveOutGetDevCapsW(i, &caps, sizeof(caps))) {
							const VDStringW key(caps.szPname);

							mPlaybackDeviceKeys[i + 1] = key;

							list->AddItem(key.c_str(), i + 1);
						}
					}

					auto it(std::find(mPlaybackDeviceKeys.cbegin(), mPlaybackDeviceKeys.cend(), mPrefs.mAudioPlaybackDeviceKey));
					if (it != mPlaybackDeviceKeys.cend()) {
						win->SetValue(it - mPlaybackDeviceKeys.cbegin());
					} else {
						win->SetValue(0);
					}
				}
			}

			return true;
		case kEventDetach:
		case kEventSync:
			{
				IVDUIWindow *win = pBase->GetControl(100);
				int index = 0;

				if (win) {
					IVDUIList *list = vdpoly_cast<IVDUIList *>(win);
					if (list) {
						int i = win->GetValue();

						if (i >= 0)
							index = list->GetItemData(i);
					}
				}

				if ((uint32)index < mPlaybackDeviceKeys.size())
					mPrefs.mAudioPlaybackDeviceKey = mPlaybackDeviceKeys[index];
				else
					mPrefs.mAudioPlaybackDeviceKey.clear();
			}
			return true;
		}
		return false;
	}

protected:
	typedef std::vector<VDStringW> PlaybackDeviceKeys;
	PlaybackDeviceKeys mPlaybackDeviceKeys;
};

class VDDialogPreferencesAccel : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesAccel(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();
			SetValue(100, mPrefs.mbFilterAccelEnabled);
			return true;
		case kEventDetach:
		case kEventSync:
			mPrefs.mbFilterAccelEnabled = GetValue(100) != 0;
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesBatch : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesBatch(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();
			SetValue(100, mPrefs.mbBatchStatusWindowEnabled);
			return true;
		case kEventDetach:
		case kEventSync:
			mPrefs.mbBatchStatusWindowEnabled = GetValue(100) != 0;
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesAutoRecover : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesAutoRecover(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();
			SetValue(100, mPrefs.mbAutoRecoverEnabled);
			return true;
		case kEventDetach:
		case kEventSync:
			mPrefs.mbAutoRecoverEnabled = GetValue(100) != 0;
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesStartup : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesStartup(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();
			SetValue(100, mPrefs.mbUseUserProfile);
			return true;
		case kEventDetach:
		case kEventSync:
			mPrefs.mbUseUserProfile = GetValue(100) != 0;
			return true;
		}
		return false;
	}
};

class VDDialogPreferencesHistory : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferencesHistory(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		switch(type) {
		case kEventAttach:
			mpBase = pBase;
			pBase->ExecuteAllLinks();
			SetCaption(100, VDStringW().sprintf(L"%u", mPrefs.mMRUSize).c_str());
			return true;
		case kEventDetach:
		case kEventSync:
			{
				int v = 0;
				swscanf(GetCaption(100).c_str(), L"%d", &v);
				mPrefs.mMRUSize = std::clamp(v, 0, 25);
			}
			return true;
		case kEventSelect:
			if (id == 101) {
				VDClearFilespecSystemData();
				mPrefs.mHistoryClearCounter = ++g_prefs2.mHistoryClearCounter;
				return true;
			}
			break;
		}
		return false;
	}
};

class VDDialogPreferences : public VDDialogBase {
public:
	VDPreferences2& mPrefs;
	VDDialogPreferences(VDPreferences2& p) : mPrefs(p) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		static int g_prefsPage = 0;

		if (type == kEventAttach) {
			mpBase = pBase;
			SetValue(100, g_prefsPage);
			IVDUIWindow *pSave = mpBase->GetControl(12);
			pSave->SetEnabled(!g_pShadowRegistry);
			pBase->ExecuteAllLinks();
		} else if (id == 101 && type == kEventSelect) {
			IVDUIBase *pSubDialog = vdpoly_cast<IVDUIBase *>(pBase->GetControl(101)->GetStartingChild());

			if (pSubDialog) {
				g_prefsPage = item;
				switch(item) {
				case 0:  pSubDialog->SetCallback(new VDDialogPreferencesGeneral(mPrefs), true); break;
				case 1:  pSubDialog->SetCallback(new VDDialogPreferencesDisplay(mPrefs), true); break;
				case 2:  pSubDialog->SetCallback(new VDDialogPreferencesScene(mPrefs), true); break;
				case 3:  pSubDialog->SetCallback(new VDDialogPreferencesCPU(mPrefs), true); break;
				case 4:  pSubDialog->SetCallback(new VDDialogPreferencesAVI(mPrefs), true); break;
				case 5:  pSubDialog->SetCallback(new VDDialogPreferencesTimeline(mPrefs), true); break;
				case 6:  pSubDialog->SetCallback(new VDDialogPreferencesDub(mPrefs), true); break;
				case 7:  pSubDialog->SetCallback(new VDDialogPreferencesImages(mPrefs), true); break;
				case 8:  pSubDialog->SetCallback(new VDDialogPreferencesThreading(mPrefs), true); break;
				case 9:  pSubDialog->SetCallback(new VDDialogPreferencesPlayback(mPrefs), true); break;
				//deprecated: pSubDialog->SetCallback(new VDDialogPreferencesAccel(mPrefs), true); break;
				case 10: pSubDialog->SetCallback(new VDDialogPreferencesBatch(mPrefs), true); break;
				case 11: pSubDialog->SetCallback(new VDDialogPreferencesAutoRecover(mPrefs), true); break;
				case 12: pSubDialog->SetCallback(new VDDialogPreferencesStartup(mPrefs), true); break;
				case 13: pSubDialog->SetCallback(new VDDialogPreferencesHistory(mPrefs), true); break;
				}
			}
		} else if (type == kEventSelect) {
			if (id == 10) {
				pBase->EndModal(true);
				return true;
			} else if (id == 11) {
				pBase->EndModal(false);
				return true;
			} else if (id == 12) {
				IVDUIBase *pSubDialog = vdpoly_cast<IVDUIBase *>(pBase->GetControl(101)->GetStartingChild());

				if (pSubDialog)
					pSubDialog->DispatchEvent(vdpoly_cast<IVDUIWindow *>(mpBase), 0, IVDUICallback::kEventSync, 0);

				VDSavePreferences(mPrefs);
			}
		}
		return false;
	}
};

int VDShowPreferencesDialog(VDGUIHandle h) {
	vdrefptr<IVDUIWindow> peer(VDUICreatePeer(h));

	vdrefptr<IVDUIWindow> pWin(VDCreateDialogFromResource(1000, peer));
	VDPreferences2 temp(g_prefs2);
	VDDialogPreferences prefDlg(temp);

	IVDUIBase *pBase = vdpoly_cast<IVDUIBase *>(pWin);

	pBase->SetCallback(&prefDlg, false);
	int result = pBase->DoModal();

	peer->Shutdown();
	pWin->Shutdown();

	int part_mask = 0;

	if (result) {
		if (g_prefs2.displayChanged(temp))
			part_mask |= PREFERENCES_DISPLAY;

		if (g_prefs2.mEnabledCPUFeatures!=temp.mEnabledCPUFeatures)
			part_mask |= PREFERENCES_OPTF;

		if (g_prefs2.timelineChanged(temp))
			part_mask |= PREFERENCES_TIMELINE;

		g_prefs2 = temp;
		VDPreferencesUpdated();
	}

	return part_mask;
}

void LoadPreferences() {
	IVDRegistryProvider *reg = g_pShadowRegistry;

	VDRegistryAppKey baseKey(reg);

	g_prefs2.iPreviewPriority = baseKey.getInt("PreviewPriority", 0);
	g_prefs2.iDubPriority     = baseKey.getInt("DubPriority", 0);
	g_prefs2.fAttachExtension = baseKey.getBool("AttachExtension", true);

	g_prefs2.iSceneCutThreshold  = baseKey.getInt("SceneCutThreshold", 50 * 16);
	g_prefs2.iSceneFadeThreshold = baseKey.getInt("SceneFadeThreshold", 4 * 16);

	g_prefs2.fAVIRestrict1Gb  = baseKey.getBool("AVIRestrict1Gb", false);
	g_prefs2.fNoCorrectLayer3 = baseKey.getBool("NoCorrectLayer3", false);

	VDRegistryAppKey key(reg, "Preferences");

	g_prefs2.mDisplayAPI = key.getInt("Display: API", kDisplayDirect3D9);
	switch (g_prefs2.mDisplayAPI) {
	case kDisplayGDI:
	case kDisplayDirectDraw:
	case kDisplayDirect3D9:
	case kDisplayDirect3D11:
	case kDisplayOpenGL:
		// correct value, do nothing
		break;
	default:
		// incorrect value, set default value
		g_prefs2.mDisplayAPI = kDisplayDirect3D9;
	}

	g_prefs2.mbDisplayAllowDirectXOverlays     = key.getBool("Display: Allow DirectX overlays", false);
	g_prefs2.mbDisplayEnableHighPrecision      = key.getBool("Display: Enable high precision", false);
	g_prefs2.mbDisplayEnableFX                 = key.getBool("Display: Use effect file", false);
	g_prefs2.mbDisplayUseDXWithTS              = key.getBool("Display: Use DirectX with Terminal", false);
	g_prefs2.mbDisplayEnableVSync              = key.getBool("Display: Enable VSync", false);;
	g_prefs2.mbDisplayEnableBackgroundFallback = key.getBool("Display: Enable background fallback", true);
	g_prefs2.mbDisplayEnableDebugInfo          = key.getBool("Display: Enable debug info", false);

	g_prefs2.mDisplaySecondaryMode = (VDPreferences2::DisplaySecondaryMode)key.getEnumInt("Display: Secondary monitor mode", VDPreferences2::kDisplaySecondaryModeCount);

	if (!key.getString("Direct3D FX file", g_prefs2.mD3DFXFile)) {
		g_prefs2.mD3DFXFile = L"display.fx";
	}

	bool init_zoom = false;
	{
		VDRegistryAppKey key("Persistence");
		if (key.getInt("Input pane size", 0)) init_zoom = true;
		if (key.getInt("Output pane size", 0)) init_zoom = true;
	}
	g_prefs2.mbUIRememberZoom = key.getBool("UI: Remember autosize and zoom", init_zoom);

	if (!key.getString("Timeline: Format2", g_prefs2.mTimelineFormat))
		g_prefs2.mTimelineFormat = L"Frame %f (%t) [%c]";
	g_prefs2.mTimeFormat = key.getInt("Timeline: time format", pref_time_hmst);

	g_prefs2.mTimelinePageSize = key.getInt("Timeline: Page size", 50);
	g_prefs2.mbTimelinePageMode = key.getBool("Timeline: Page mode", false);
	g_prefs2.mbTimelineWarnReloadTruncation = key.getBool("Timeline: Warn on truncation when reloading", true);
	g_prefs2.mTimelineScaleTrack = key.getInt("Timeline: Scale track", 100);
	g_prefs2.mTimelineScaleInfo = key.getInt("Timeline: Scale info", 100);
	g_prefs2.mTimelineScaleButtons = key.getInt("Timeline: Scale buttons", 100);

	g_prefs2.mbAllowDirectYCbCrDecoding = key.getBool("Allow direct YCbCr decoding", true);

	g_prefs2.mbConfirmRenderAbort = key.getBool("Confirm render abort", true);
	g_prefs2.mbConfirmExit = key.getBool("Confirm exit", false);

	g_prefs2.mbAVITestRaw = key.getBool("AVI: Write nonstandard raw", false);
	g_prefs2.mbRenderWarnNoAudio = key.getBool("Render: Warn if no audio", false);
	g_prefs2.mbEnableAVIAlignmentThreshold = key.getBool("AVI: Alignment threshold enable", false);
	g_prefs2.mbEnableAVIVBRWarning = key.getBool("AVI: VBR warning enabled", true);
	g_prefs2.mbEnableAVINonZeroStartWarning = key.getBool("AVI: Non-zero start warning enabled", true);
	g_prefs2.mAVIAlignmentThreshold = key.getInt("AVI: Alignment threshold", 524288);
	g_prefs2.mbPreferInternalVideoDecoders = key.getBool("AVI: Prefer internal decoders", false);
	g_prefs2.mbPreferInternalAudioDecoders = key.getBool("AVI: Prefer internal audio decoders", false);
	g_prefs2.mbUseVideoFccHandler = key.getBool("AVI: Use video stream fccHandler in codec search", false);
	g_prefs2.mbAVIRekey = key.getBool("AVI: Rekey", false);
	g_prefs2.mbAVIIgnoreIndex = key.getBool("AVI: Ignore index", false);

	g_prefs2.mRenderOutputBufferSize = std::max<uint32>(65536, std::min<uint32>(0x10000000, key.getInt("Render: Output buffer size", 2097152)));
	g_prefs2.mRenderWaveBufferSize = std::max<uint32>(65536, std::min<uint32>(0x10000000, key.getInt("Render: Wave buffer size", 65536)));
	g_prefs2.mRenderVideoBufferCount = std::max<uint32>(1, std::min<uint32>(65536, key.getInt("Render: Video buffer count", 32)));
	g_prefs2.mRenderAudioBufferSeconds = std::max<uint32>(1, std::min<uint32>(32, key.getInt("Render: Audio buffer seconds", 2)));
	g_prefs2.mRenderThrottlePercent = std::max<uint32>(10, std::min<uint32>(100, key.getInt("Render: Default throttle percent", 100)));
	g_prefs2.mbRenderInhibitSystemSleep = key.getBool("Render: Inhibit system sleep", true);
	g_prefs2.mbRenderBackgroundPriority = key.getBool("Render: Use background priority", false);
	g_prefs2.mbRenderShowFrames = key.getBool("Render: Show frames", true);
	g_prefs2.mAVISuperindexLimit = key.getInt("AVI: Superindex entry limit", 256);
	g_prefs2.mAVISubindexLimit = key.getInt("AVI: Subindex entry limit", 8192);

	uint32 imageSeqHi = key.getInt("Images: Frame rate numerator", 10);
	uint32 imageSeqLo = key.getInt("Images: Frame rate denominator", 1);

	g_prefs2.mImageSequenceFrameRate.Assign(imageSeqHi, imageSeqLo);

	g_prefs2.mVideoCompressionThreads = key.getInt("Threading: Video compression threads", 0);
	g_prefs2.mFilterThreads = key.getInt("Threading: Video filter threads", -1);

	key.getString("Playback: Default audio device", g_prefs2.mAudioPlaybackDeviceKey);

	//g_prefs2.mbFilterAccelEnabled = key.getBool("Filters: Enable 3D hardware acceleration", false);
	g_prefs2.mbFilterAccelEnabled = false;
	g_prefs2.mFilterProcessAhead = key.getInt("Filters: Process-ahead frame count", 0);

	g_prefs2.mEnabledCPUFeatures = key.getInt("CPU: Enabled extensions", 0);

	g_prefs2.mbBatchStatusWindowEnabled = key.getBool("Batch: Show status window", false);

	g_prefs2.mbAutoRecoverEnabled = key.getBool("AutoRecover: Enabled", false);
	g_prefs2.mbUseUserProfile = key.getBool("Use profile-local path", false);

	g_prefs2.mMRUSize = key.getInt("MRU size", g_prefs2.mMRUSize);
	if (g_prefs2.mMRUSize < 0)
		g_prefs2.mMRUSize = 0;
	else if (g_prefs2.mMRUSize > 25)
		g_prefs2.mMRUSize = 25;

	VDPreferencesUpdated();
}

void VDSavePreferences(VDPreferences2& prefs) {
	IVDRegistryProvider *reg = g_pShadowRegistry;

	VDRegistryAppKey baseKey(reg);

	baseKey.setInt ("PreviewPriority", prefs.iPreviewPriority);
	baseKey.setInt ("DubPriority",     prefs.iDubPriority);
	baseKey.setBool("AttachExtension", prefs.fAttachExtension);

	baseKey.setInt("SceneCutThreshold",  prefs.iSceneCutThreshold);
	baseKey.setInt("SceneFadeThreshold", prefs.iSceneFadeThreshold);

	baseKey.setBool("AVIRestrict1Gb",  prefs.fAVIRestrict1Gb);
	baseKey.setBool("NoCorrectLayer3", prefs.fNoCorrectLayer3);

	VDRegistryAppKey key(reg, "Preferences");

	key.setInt("Display: API", prefs.mDisplayAPI);

	key.setBool("Display: Allow DirectX overlays", prefs.mbDisplayAllowDirectXOverlays);
	key.setBool("Display: Enable high precision", prefs.mbDisplayEnableHighPrecision);
	key.setBool("Display: Use effect file", prefs.mbDisplayEnableFX);
	key.setBool("Display: Use DirectX with Terminal", prefs.mbDisplayUseDXWithTS);
	key.setBool("Display: Enable VSync", prefs.mbDisplayEnableVSync);
	key.setBool("Display: Enable background fallback", prefs.mbDisplayEnableBackgroundFallback);
	key.setBool("Display: Enable debug info", prefs.mbDisplayEnableDebugInfo);

	key.setInt("Display: Secondary monitor mode", prefs.mDisplaySecondaryMode);
	key.setString("Direct3D FX file", prefs.mD3DFXFile.c_str());

	key.setBool("UI: Remember autosize and zoom", prefs.mbUIRememberZoom);

	key.setString("Timeline: Format2", prefs.mTimelineFormat.c_str());
	key.setInt("Timeline: Time format", prefs.mTimeFormat);
	key.setInt("Timeline: Page size", prefs.mTimelinePageSize);
	key.setBool("Timeline: Page mode", prefs.mbTimelinePageMode);
	key.setBool("Timeline: Warn on truncation when reloading", prefs.mbTimelineWarnReloadTruncation);
	key.setInt("Timeline: Scale track", prefs.mTimelineScaleTrack);
	key.setInt("Timeline: Scale info", prefs.mTimelineScaleInfo);
	key.setInt("Timeline: Scale buttons", prefs.mTimelineScaleButtons);

	key.setBool("Allow direct YCbCr decoding", prefs.mbAllowDirectYCbCrDecoding);

	key.setBool("Confirm render abort", prefs.mbConfirmRenderAbort);
	key.setBool("Confirm exit", prefs.mbConfirmExit);

	key.setBool("AVI: Write nonstandard raw", prefs.mbAVITestRaw);
	key.setBool("Render: Warn if no audio", prefs.mbRenderWarnNoAudio);
	key.setBool("AVI: Alignment threshold enable", prefs.mbEnableAVIAlignmentThreshold);
	key.setInt("AVI: Alignment threshold", prefs.mAVIAlignmentThreshold);
	key.setBool("AVI: VBR warning enabled", prefs.mbEnableAVIVBRWarning);
	key.setBool("AVI: Non-zero start warning enabled", prefs.mbEnableAVINonZeroStartWarning);
	key.setBool("AVI: Prefer internal decoders", prefs.mbPreferInternalVideoDecoders);
	key.setBool("AVI: Prefer internal audio decoders", prefs.mbPreferInternalAudioDecoders);
	key.setBool("AVI: Use video stream fccHandler in codec search", prefs.mbUseVideoFccHandler);
	key.setBool("AVI: Rekey", prefs.mbAVIRekey);
	key.setBool("AVI: Ignore index", prefs.mbAVIIgnoreIndex);

	key.setInt("Render: Output buffer size", prefs.mRenderOutputBufferSize);
	key.setInt("Render: Wave buffer size", prefs.mRenderWaveBufferSize);
	key.setInt("Render: Video buffer count", prefs.mRenderVideoBufferCount);
	key.setInt("Render: Audio buffer seconds", prefs.mRenderAudioBufferSeconds);
	key.setInt("Render: Default throttle percent", prefs.mRenderThrottlePercent);
	key.setBool("Render: Inhibit system sleep", prefs.mbRenderInhibitSystemSleep);
	key.setBool("Render: Use background priority", prefs.mbRenderBackgroundPriority);
	key.setBool("Render: Show frames", prefs.mbRenderShowFrames);
	key.setInt("AVI: Superindex entry limit", prefs.mAVISuperindexLimit);
	key.setInt("AVI: Subindex entry limit", prefs.mAVISubindexLimit);

	key.setInt("Images: Frame rate numerator", prefs.mImageSequenceFrameRate.getHi());
	key.setInt("Images: Frame rate denominator", prefs.mImageSequenceFrameRate.getLo());

	key.setInt("Threading: Video compression threads", prefs.mVideoCompressionThreads);
	key.setInt("Threading: Video filter threads", prefs.mFilterThreads);

	key.setString("Playback: Default audio device", prefs.mAudioPlaybackDeviceKey.c_str());

	//key.setBool("Filters: Enable 3D hardware acceleration", prefs.mbFilterAccelEnabled);
	key.setInt("Filters: Process-ahead frame count", prefs.mFilterProcessAhead);

	key.setInt("CPU: Enabled extensions", prefs.mEnabledCPUFeatures);

	key.setBool("Batch: Show status window", prefs.mbBatchStatusWindowEnabled);

	key.setBool("AutoRecover: Enabled", prefs.mbAutoRecoverEnabled);
	key.setBool("Use profile-local path", prefs.mbUseUserProfile);

	key.setInt("MRU size", prefs.mMRUSize);
}

void VDSavePreferences() {
	VDSavePreferences(g_prefs2);
}

void VDSavePreferencesShadow() {
	if (!g_pShadowRegistry) g_pShadowRegistry = new VDRegistryProviderMemory;
	VDSavePreferences();
}

void VDDeletePreferencesShadow() {
	delete g_pShadowRegistry;
	g_pShadowRegistry = 0;
}

void VDSetPreferencesBool(const char *name, bool v) {
	VDSavePreferencesShadow();
	VDRegistryAppKey key(g_pShadowRegistry, "Preferences");
	key.setBool(name,v);
	LoadPreferences();
}

void VDSetPreferencesInt(const char *name, int v) {
	VDSavePreferencesShadow();
	VDRegistryAppKey key(g_pShadowRegistry, "Preferences");
	key.setInt(name,v);
	LoadPreferences();
}

void VDSetPreferencesString(const char *name, const char *s) {
	VDSavePreferencesShadow();
	VDRegistryAppKey key(g_pShadowRegistry, "Preferences");
	key.setString(name,s);
	LoadPreferences();
}

int VDPreferencesGetPreviewPriority() {
	return g_prefs2.iPreviewPriority;
}

int VDPreferencesGetDubPriority() {
	return g_prefs2.iDubPriority;
}

bool VDPreferencesGetAttachExtension() {
	return g_prefs2.fAttachExtension;
}

int VDPreferencesGetSceneCutThreshold() {
	return g_prefs2.iSceneCutThreshold;
}

int VDPreferencesSceneFadeThreshold() {
	return g_prefs2.iSceneFadeThreshold;
}

int VDPreferencesGetDisplayAPI() {
	return g_prefs2.mDisplayAPI;
}

bool VDPreferencesGetDisplayEnableVSync() {
	return g_prefs2.mbDisplayEnableVSync;
}

bool VDPreferencesGetAVIRestrict1Gb() {
	return g_prefs2.fAVIRestrict1Gb;
}

bool VDPreferencesGetNoCorrectLayer3() {
	return g_prefs2.fNoCorrectLayer3;
}

bool VDPreferencesGetRememberZoom() {
	return g_prefs2.mbUIRememberZoom;
}

const VDStringW& VDPreferencesGetTimelineFormat() {
	return g_prefs2.mTimelineFormat;
}

int VDPreferencesGetTimeFormat() {
	return g_prefs2.mTimeFormat;
}

void VDPreferencesSetTimeFormat(int format) {
	g_prefs2.mTimeFormat = format;
}

bool VDPreferencesIsDirectYCbCrInputEnabled() {
	return g_prefs2.mbAllowDirectYCbCrDecoding;
}

bool VDPreferencesIsRenderAbortConfirmEnabled() {
	return g_prefs2.mbConfirmRenderAbort;
}

bool VDPreferencesIsRenderNoAudioWarningEnabled() {
	return g_prefs2.mbRenderWarnNoAudio;
}

uint32 VDPreferencesGetAVIAlignmentThreshold() {
	return g_prefs2.mbEnableAVIAlignmentThreshold ? g_prefs2.mAVIAlignmentThreshold : 0;
}

void VDPreferencesGetAVIIndexingLimits(uint32& superindex, uint32& subindex) {
	superindex = g_prefs2.mAVISuperindexLimit;
	subindex = g_prefs2.mAVISubindexLimit;
}

bool VDPreferencesIsAVIVBRWarningEnabled() {
	return g_prefs2.mbEnableAVIVBRWarning;
}

bool VDPreferencesIsAVINonZeroStartWarningEnabled() {
	return g_prefs2.mbEnableAVINonZeroStartWarning;
}

bool VDPreferencesIsPreferInternalVideoDecodersEnabled() {
	return g_prefs2.mbPreferInternalVideoDecoders;
}

bool VDPreferencesAVIIgnoreIndex() {
	return g_prefs2.mbAVIIgnoreIndex;
}

bool VDPreferencesAVIRekey() {
	return g_prefs2.mbAVIRekey;
}

bool VDPreferencesAVITestRaw() {
	return g_prefs2.mbAVITestRaw;
}

bool VDPreferencesIsPreferInternalAudioDecodersEnabled() {
	return g_prefs2.mbPreferInternalAudioDecoders;
}

bool VDPreferencesIsUseVideoFccHandlerEnabled() {
	return g_prefs2.mbUseVideoFccHandler;
}

const VDStringW& VDPreferencesGetD3DFXFile() {
	return g_prefs2.mD3DFXFile;
}

uint32& VDPreferencesGetRenderOutputBufferSize() {
	return g_prefs2.mRenderOutputBufferSize;
}

uint32& VDPreferencesGetRenderWaveBufferSize() {
	return g_prefs2.mRenderWaveBufferSize;
}

uint32& VDPreferencesGetRenderVideoBufferCount() {
	return g_prefs2.mRenderVideoBufferCount;
}

uint32& VDPreferencesGetRenderAudioBufferSeconds() {
	return g_prefs2.mRenderAudioBufferSeconds;
}

uint32 VDPreferencesGetRenderThrottlePercent() {
	return g_prefs2.mRenderThrottlePercent;
}

const VDFraction& VDPreferencesGetImageSequenceFrameRate() {
	return g_prefs2.mImageSequenceFrameRate;
}

int VDPreferencesGetVideoCompressionThreadCount() {
	return g_prefs2.mVideoCompressionThreads;
}

uint32 VDPreferencesGetEnabledCPUFeatures() {
	return g_prefs2.mEnabledCPUFeatures;
}

const VDStringW& VDPreferencesGetAudioPlaybackDeviceKey() {
	return g_prefs2.mAudioPlaybackDeviceKey;
}

bool VDPreferencesGetFilterAccelEnabled() {
	return g_prefs2.mbFilterAccelEnabled;
}

void VDPreferencesSetFilterAccelVisualDebugEnabled(bool enabled) {
	g_prefs2.mbFilterAccelDebugEnabled = enabled;
}

bool VDPreferencesGetFilterAccelVisualDebugEnabled() {
	return g_prefs2.mbFilterAccelDebugEnabled;
}

sint32 VDPreferencesGetFilterThreadCount() {
	return g_prefs2.mFilterThreads;
}

sint32 VDPreferencesGetVideoFilterProcessAheadCount() {
	return g_prefs2.mFilterProcessAhead;
}

bool VDPreferencesGetBatchShowStatusWindow() {
	return g_prefs2.mbBatchStatusWindowEnabled;
}

bool VDPreferencesGetRenderInhibitSystemSleepEnabled() {
	return g_prefs2.mbRenderInhibitSystemSleep;
}

bool VDPreferencesGetRenderBackgroundPriority() {
	return g_prefs2.mbRenderBackgroundPriority;
}

bool VDPreferencesGetRenderShowFrames() {
	return g_prefs2.mbRenderShowFrames;
}

bool VDPreferencesGetAutoRecoverEnabled() {
	return g_prefs2.mbAutoRecoverEnabled;
}

int VDPreferencesGetMRUSize() {
	return g_prefs2.mMRUSize;
}

int VDPreferencesGetHistoryClearCounter() {
	return g_prefs2.mHistoryClearCounter;
}

bool VDPreferencesGetConfirmExit() {
	return g_prefs2.mbConfirmExit;
}

bool VDPreferencesGetTimelineWarnReloadTruncation() {
	return g_prefs2.mbTimelineWarnReloadTruncation;
}

int VDPreferencesGetTimelinePageSize() {
	return g_prefs2.mTimelinePageSize;
}

bool VDPreferencesGetTimelinePageMode() {
	return g_prefs2.mbTimelinePageMode;
}

int VDPreferencesGetTimelineScaleTrack() {
	return g_prefs2.mTimelineScaleTrack;
}

int VDPreferencesGetTimelineScaleInfo() {
	return g_prefs2.mTimelineScaleInfo;
}

int VDPreferencesGetTimelineScaleButtons() {
	return g_prefs2.mTimelineScaleButtons;
}

void VDPreferencesUpdated()
{
	VDVideoDisplaySetAPI(g_prefs2.mDisplayAPI);

	VDVideoDisplaySetDirectXOverlays(g_prefs2.mbDisplayAllowDirectXOverlays);

	VDVideoDisplaySetHighPrecision(g_prefs2.mbDisplayEnableHighPrecision);
	VDVideoDisplaySetD3DFX(g_prefs2.mbDisplayEnableFX);
	VDVideoDisplaySetD3DFXFileName(g_prefs2.mD3DFXFile.c_str());

	VDVideoDisplaySetDXWithTS(g_prefs2.mbDisplayUseDXWithTS);
	VDVideoDisplaySetBackgroundFallbackEnabled(g_prefs2.mbDisplayEnableBackgroundFallback);
	VDVideoDisplaySetDebugInfoEnabled(g_prefs2.mbDisplayEnableDebugInfo);

	VDVideoDisplaySetSecondaryDXEnabled(g_prefs2.mDisplaySecondaryMode != VDPreferences2::kDisplaySecondaryMode_Disable);
	VDVideoDisplaySetMonitorSwitchingDXEnabled(g_prefs2.mDisplaySecondaryMode == VDPreferences2::kDisplaySecondaryMode_AutoSwitch);
}

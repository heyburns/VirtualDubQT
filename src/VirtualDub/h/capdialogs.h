// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2004 Avery Lee
// Copyright (C) 2017 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_CAPDIALOGS_H
#define f_CAPDIALOGS_H

#include <vd2/system/vdstl.h>

enum {
	kVDCapDevOptSaveCurrentAudioFormat = 1,
	kVDCapDevOptSaveCurrentAudioCompFormat = 2,
	kVDCapDevOptSaveCurrentVideoFormat = 4,
	kVDCapDevOptSaveCurrentVideoCompFormat = 8,
	kVDCapDevOptSaveCurrentFrameRate = 0x10,
	kVDCapDevOptSaveCurrentDisplayMode = 0x20,
	kVDCapDevOptSwitchSourcesTogether = 0x40,
	kVDCapDevOptSlowOverlay = 0x80,
	kVDCapDevOptSlowPreview = 0x100
};

struct VDCaptureSettings {
	uint32	mFramePeriod;
	int		mVideoBufferCount;
	int		mAudioBufferCount;
	int		mAudioBufferSize;
	bool	mbDisplayPrerollDialog;
	bool	mbMaxPower;
	bool	mbEnablePower;
};

class IVDUICaptureVumeter;

bool VDShowCaptureSettingsDialog(VDGUIHandle hwndParent, VDCaptureSettings& parms);
bool VDShowCapturePreferencesDialog(VDGUIHandle h, VDCapturePreferences& prefs);
void VDCaptureLoadPreferences(VDCapturePreferences& prefs);
void VDCaptureSavePreferences(const VDCapturePreferences& prefs);
void VDShowCaptureChannelsDialog(VDGUIHandle h, const vdstructex<VDWaveFormat>& format, VDAudioMaskParam& param, IVDUICaptureVumeter** thunk);

#endif

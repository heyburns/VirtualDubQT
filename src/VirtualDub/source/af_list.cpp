// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include "plugins.h"

extern const VDPluginInfo
	apluginDef_input,
	apluginDef_lowpass,
	apluginDef_highpass,
	apluginDef_butterfly,
	apluginDef_stereosplit,
	apluginDef_stereomerge,
	apluginDef_playback,
	apluginDef_resample,
	apluginDef_output,
	apluginDef_sink,
	apluginDef_pitchshift,
	apluginDef_pitchscale,
	apluginDef_stretch,
	apluginDef_discard,
	apluginDef_centercut,
	apluginDef_centermix,
	apluginDef_gain,
	apluginDef_stereochorus,
	apluginDef_split,
	apluginDef_mix,
	apluginDef_newrate,
	apluginDef_formatconv,
	apluginDef_timestretch;

static const VDPluginInfo *const g_builtin_audio_filters[]={
	&apluginDef_input,
	&apluginDef_lowpass,
	&apluginDef_highpass,
	&apluginDef_butterfly,
	&apluginDef_stereosplit,
	&apluginDef_stereomerge,
	&apluginDef_playback,
	&apluginDef_resample,
	&apluginDef_output,
	&apluginDef_sink,
	&apluginDef_pitchshift,
	&apluginDef_pitchscale,
	&apluginDef_stretch,
	&apluginDef_discard,
	&apluginDef_centercut,
	&apluginDef_centermix,
	&apluginDef_gain,
	&apluginDef_stereochorus,
	&apluginDef_split,
	&apluginDef_mix,
	&apluginDef_newrate,
	&apluginDef_formatconv,
	&apluginDef_timestretch,
	NULL
};

void VDInitBuiltinAudioFilters() {
	VDAddInternalPlugins(g_builtin_audio_filters);
}

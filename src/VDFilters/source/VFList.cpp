// VirtualDub - Video processing and capture application
// Internal filter library
//
// Copyright (C) 1998-2011 Avery Lee
// Copyright (C) 2016-2019 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include <stdafx.h>
#include <vd2/plugin/vdvideofilt.h>
#include <vd2/VDXFrame/VideoFilter.h>
#include <vd2/VDFilters/VFList.h>

extern const VDXFilterDefinition2 g_VDVFAliasFormat;
extern const VDXFilterDefinition g_VDVFBlendLayers;
extern const VDXFilterDefinition g_VDVFBlur;
extern const VDXFilterDefinition g_VDVFBlurHi;
extern const VDXFilterDefinition g_VDVFBoxBlur;
extern const VDXFilterDefinition g_VDVFBrightCont;
extern const VDXFilterDefinition g_VDVFChromaSmoother;
extern const VDXFilterDefinition2 g_VDVFConvertFormat;
extern const VDXFilterDefinition2 g_VDVFConvertGray;
extern const VDXFilterDefinition2 g_VDVFConvertFloat;
extern const VDXFilterDefinition g_VDVFDeinterlace;
extern const VDXFilterDefinition2 g_VDVFDrawText;
extern const VDXFilterDefinition2 g_VDVFDrawTime;
extern const VDXFilterDefinition g_VDVFFieldBob;
extern const VDXFilterDefinition g_VDVFFieldBob2;
extern const VDXFilterDefinition g_VDVFFieldDelay;
extern const VDXFilterDefinition g_VDVFFieldSwap;
extern const VDXFilterDefinition g_VDVFFlipHorizontally;
extern const VDXFilterDefinition g_VDVFFlipVertically;
extern const VDXFilterDefinition g_VDVFGammaCorrect;
extern const VDXFilterDefinition g_VDVFGrayscale;
extern const VDXFilterDefinition g_VDVFHSV;
extern const VDXFilterDefinition g_VDVFInterlace;
extern const VDXFilterDefinition g_VDVFInterpolate;
extern const VDXFilterDefinition g_VDVFInvert;
extern const VDXFilterDefinition g_VDVFIVTC;
extern const VDXFilterDefinition g_VDVFMergeLayers;
extern const VDXFilterDefinition g_VDVFMotionBlur;
extern const VDXFilterDefinition g_VDVFCrop;
extern const VDXFilterDefinition g_VDVFNull;
extern const VDXFilterDefinition g_VDVFPerspective;
extern const VDXFilterDefinition g_VDVFReduce2;
extern const VDXFilterDefinition g_VDVFReduce2HQ;
extern const VDXFilterDefinition g_VDVFRotate;
extern const VDXFilterDefinition g_VDVFRotate2;
extern const VDXFilterDefinition g_VDVFSharpen;
extern const VDXFilterDefinition g_VDVFSmoother;
extern const VDXFilterDefinition g_VDVFTemporalSmoother;
extern const VDXFilterDefinition g_VDVFThreshold;
extern const VDXFilterDefinition g_VDVFTV;
extern const VDXFilterDefinition g_VDVFWarpResize;
extern const VDXFilterDefinition g_VDVFWarpSharp;

extern const VDXFilterDefinition *const g_VDVFList[]={
	&g_VDVFAliasFormat,
	&g_VDVFBlendLayers,
	&g_VDVFBlur,
	&g_VDVFBlurHi,
	&g_VDVFBoxBlur,
	&g_VDVFBrightCont,
	&g_VDVFChromaSmoother,
	&g_VDVFConvertFormat,
	&g_VDVFConvertGray,
	&g_VDVFConvertFloat,
	&g_VDVFDeinterlace,
	&g_VDVFDrawText,
	&g_VDVFDrawTime,
	&g_VDVFFieldBob,
	&g_VDVFFieldBob2,
	&g_VDVFFieldDelay,
	&g_VDVFFieldSwap,
	&g_VDVFFlipHorizontally,
	&g_VDVFFlipVertically,
	&g_VDVFGammaCorrect,
	&g_VDVFGrayscale,
	&g_VDVFHSV,
	&g_VDVFInterlace,
	&g_VDVFInterpolate,
	&g_VDVFInvert,
	&g_VDVFIVTC,
	&g_VDVFMergeLayers,
	&g_VDVFMotionBlur,
	&g_VDVFCrop,
	&g_VDVFNull,
	&g_VDVFPerspective,
	&g_VDVFReduce2,
	&g_VDVFReduce2HQ,
	&g_VDVFRotate,
	&g_VDVFRotate2,
	&g_VDVFSharpen,
	&g_VDVFSmoother,
	&g_VDVFTemporalSmoother,
	&g_VDVFThreshold,
	&g_VDVFTV,
	&g_VDVFWarpResize,
	&g_VDVFWarpSharp,
	NULL
};

const VDXFilterDefinition *const *VDVFGetList() {
	return g_VDVFList;
}


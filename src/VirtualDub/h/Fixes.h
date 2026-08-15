// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2017 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_FIXES_H
#define f_FIXES_H

#include <vd2/plugin/vdinputdriver.h>

typedef VDXAVIStreamHeader AVIStreamHeader_fixed;

/*
struct AVIStreamHeader_fixed{
    uint32		fccType;
    uint32		fccHandler;
    uint32		dwFlags;
    uint16		wPriority;
    uint16		wLanguage;
    uint32		dwInitialFrames;
    uint32		dwScale;
    uint32		dwRate;
    uint32		dwStart;
    uint32		dwLength;
    uint32		dwSuggestedBufferSize;
    uint32		dwQuality;
    uint32		dwSampleSize;
	struct {
		sint16	left;
		sint16	top;
		sint16	right;
		sint16	bottom;
	} rcFrame;
};
*/

#endif

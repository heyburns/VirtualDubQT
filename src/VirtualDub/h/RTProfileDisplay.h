// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
// Copyright (C) 2016 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_RTPROFILEDISPLAY_H
#define f_RTPROFILEDISPLAY_H

#include <windows.h>

#define RTPROFILEDISPLAYCONTROLCLASS (g_szRTProfileDisplayControlName)

extern const wchar_t g_szRTProfileDisplayControlName[];

ATOM RegisterRTProfileDisplayControl();

class VDRTProfiler;

class IVDRTProfileDisplay {
public:
	virtual void SetProfiler(VDRTProfiler *pProfiler) = 0;
};

IVDRTProfileDisplay *VDGetIRTProfileDisplayControl(HWND hwnd);

#endif

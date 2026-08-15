// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
// Copyright (C) 2016 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_LOGWINDOW_H
#define f_LOGWINDOW_H

#include <windows.h>
#include <list>
#include <vd2/system/VDString.h>
#include <vd2/system/log.h>

#define LOGWINDOWCONTROLCLASS (g_szLogWindowControlName)

extern const wchar_t g_szLogWindowControlName[];

ATOM RegisterLogWindowControl();

class IVDLogWindowControl : public IVDLogger {
public:
	virtual void AttachAsLogger(bool bThisThreadOnly) = 0;
	virtual void AddEntry(int severity, const wchar_t *s) = 0;
	virtual void AddEntry(int severity, const VDStringW& s) = 0;
};

IVDLogWindowControl *VDGetILogWindowControl(HWND hwnd);

#endif

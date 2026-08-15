// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2017-2018 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_OPTDLG_H
#define f_OPTDLG_H

#include <windows.h>

struct VDPixmapFormatEx;
struct COMPVARS2;

/////////////////

void ActivateDubDialog(HINSTANCE hInst, LPCWSTR lpResource, HWND hDlg, DLGPROC dlgProc);
INT_PTR CALLBACK AudioConversionDlgProc	( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AudioInterleaveDlgProc	( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK VideoDepthDlgProc			( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK DynamicCompileOptionsDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK VideoDecimationDlgProc	( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK VideoClippingDlgProc		( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK VideoJumpDlgProc			(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam);

// VDDisplayVideoDepthDialog type
enum {
	DepthDialog_input=1,
	DepthDialog_output=2,
	DepthDialog_cap_output=3,
};

bool VDDisplayVideoDepthDialog(VDGUIHandle hParent, VDPixmapFormatEx& opts, int type, int lockFormat, COMPVARS2* compVars, BITMAPINFOHEADER* capSrc=0);

#endif

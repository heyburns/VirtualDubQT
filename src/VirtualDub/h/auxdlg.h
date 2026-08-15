// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AUXDLG_H
#define f_AUXDLG_H

#include <windows.h>

INT_PTR CALLBACK ShowTextDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AboutDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

void Welcome();
void AnnounceExperimental();

#endif

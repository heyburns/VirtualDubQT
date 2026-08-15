// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#include <windows.h>
#include <commctrl.h>

#include "resource.h"

#include "ProgressDialog.h"

#include <vd2/system/error.h>

extern HINSTANCE g_hInst;

ProgressDialog::ProgressDialog(HWND hwndParent, const char *szTitle, const char *szCaption, long _maxval, bool _fAbortEnabled)
	:lpszTitle(szTitle)
	,lpszCaption(szCaption)
	,lpszValueFormat(NULL)
	,maxval(_maxval)
	,curval(0)
	,newval(0)
	,mSparseCount(1)
	,mSparseInterval(1)
	,fAbortEnabled(_fAbortEnabled)
	,fAbort(false)
	,hwndProgressBar(NULL)
	,hwndValue(NULL)
	,hwndDialog(NULL)
	,mhwndParent(hwndParent)
{
	dwLastTime = GetTickCount();

	CreateDialogParamW(g_hInst, MAKEINTRESOURCEW(IDD_PROGRESS), hwndParent, ProgressDlgProc, (LPARAM)this);
}

ProgressDialog::~ProgressDialog() {
	close();
}

void ProgressDialog::setCaption(const char *sz) {
	lpszCaption = sz;

	if (hwndDialog)
		SetDlgItemTextA(hwndDialog, IDC_STATIC_MESSAGE, sz);
}

void ProgressDialog::setValueFormat(const char *sz) {
	lpszValueFormat = sz;
}

void ProgressDialog::setLimit(long lim) {
	curval = 0;		// force a bar update
	maxval = lim;
}

void ProgressDialog::check() {
	MSG msg;

	if (--mSparseCount)
		return;

	DWORD dwTime = GetTickCount();

	mSparseCount = mSparseInterval;

	if (dwTime < dwLastTime + 50) {
		++mSparseInterval;
	} else if (dwTime > dwLastTime + 150) {
		if (mSparseInterval>1)
			--mSparseInterval;
	}

	dwLastTime = dwTime;

	while(PeekMessageW(&msg, mhwndParent ? NULL : hwndDialog, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT && fAbortEnabled) {
			PostQuitMessage(msg.wParam);
			throw MyUserAbortError();
		}

		if (!IsWindow(hwndDialog) || !IsDialogMessage(hwndDialog, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	if (fAbort)
		throw MyUserAbortError();
}

void ProgressDialog::close() {
	if (hwndDialog) {
		if (mhwndParent)
			EnableWindow(mhwndParent, mbPreviouslyEnabled);
		DestroyWindow(hwndDialog);
		hwndDialog = 0;
	}
}

INT_PTR CALLBACK ProgressDialog::ProgressDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	ProgressDialog* thisPtr = (ProgressDialog*)GetWindowLongPtrW(hDlg, DWLP_USER);
	int newval2;

	switch(msg) {
		case WM_INITDIALOG:
			SetWindowLongPtrW(hDlg, DWLP_USER, lParam);

			thisPtr = (ProgressDialog *)lParam;
			thisPtr->hwndProgressBar = GetDlgItem(hDlg, IDC_PROGRESS);
			thisPtr->hwndValue = GetDlgItem(hDlg, IDC_CURRENT_VALUE);
			SendMessageW(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 16384));

			if (!thisPtr->fAbortEnabled)
				EnableWindow(GetDlgItem(hDlg, IDCANCEL), FALSE);

			SetWindowTextA(hDlg, thisPtr->lpszTitle);
			SetDlgItemTextA(hDlg, IDC_STATIC_MESSAGE, thisPtr->lpszCaption);

			thisPtr->hwndDialog = hDlg;

			if (thisPtr->mhwndParent) {
				thisPtr->mbPreviouslyEnabled = !!IsWindowEnabled(thisPtr->mhwndParent);
				EnableWindow(thisPtr->mhwndParent, FALSE);
			}

			SetTimer(hDlg, 1, 500, NULL);

			{
				bool vis = true;

				if (HWND hwndParent = GetParent(hDlg)) {
					while (GetWindowLongW(hwndParent, GWL_STYLE) & WS_CHILD)
						hwndParent = GetParent(hwndParent);

					if (IsIconic(hwndParent))
						vis = false;
				}

				ShowWindow(hDlg, vis ? SW_SHOW : SW_SHOWMINNOACTIVE);
			}
			break;

		case WM_TIMER:
			newval2 = MulDiv(thisPtr->newval, 16384, thisPtr->maxval);

			if (newval2 > thisPtr->curval) {
				if (newval2 > 16384) newval2 = 16384;
				thisPtr->curval = newval2;

				SendMessageW(thisPtr->hwndProgressBar, PBM_SETPOS, (WPARAM)newval2, 0);
			}

			if (thisPtr->lpszValueFormat) {
				char szTemp[128];

				wsprintfA(szTemp, thisPtr->lpszValueFormat, thisPtr->newval, thisPtr->maxval);
				SendMessageA(thisPtr->hwndValue, WM_SETTEXT, 0, (LPARAM)szTemp);
			}
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDCANCEL)
				thisPtr->fAbort = true;
			return TRUE;
	}

	return FALSE;
}

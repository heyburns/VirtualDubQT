// VDXFrame - Helper library for VirtualDub plugins
//
// Copyright (C) 2008 Avery Lee
// Copyright (C) 2017 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <windows.h>
#include <vd2/VDXFrame/VideoFilterDialog.h>

namespace {
	HINSTANCE GetLocalHInstance() {
		return (HINSTANCE)1;
	}
}

VDXVideoFilterDialog::VDXVideoFilterDialog()
	: mhdlg(NULL)
{
}

LRESULT VDXVideoFilterDialog::Show(HINSTANCE hInst, LPCSTR templName, HWND parent) {
	if (!hInst)
		hInst = GetLocalHInstance();

	return DialogBoxParamA(hInst, templName, parent, StaticDlgProc, (LPARAM)this);
}

LRESULT VDXVideoFilterDialog::Show(HINSTANCE hInst, LPCWSTR templName, HWND parent) {
	if (!hInst)
		hInst = GetLocalHInstance();

	return DialogBoxParamW(hInst, templName, parent, StaticDlgProc, (LPARAM)this);
}

HWND VDXVideoFilterDialog::ShowModeless(HINSTANCE hInst, LPCSTR templName, HWND parent) {
	if (!hInst)
		hInst = GetLocalHInstance();

	return CreateDialogParamA(hInst, templName, parent, StaticDlgProc, (LPARAM)this);
}

HWND VDXVideoFilterDialog::ShowModeless(HINSTANCE hInst, LPCWSTR templName, HWND parent) {
	if (!hInst)
		hInst = GetLocalHInstance();

	return CreateDialogParamW(hInst, templName, parent, StaticDlgProc, (LPARAM)this);
}

INT_PTR CALLBACK VDXVideoFilterDialog::StaticDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	VDXVideoFilterDialog *pThis;

	if (msg == WM_INITDIALOG) {
		pThis = (VDXVideoFilterDialog *)lParam;
		SetWindowLongPtrW(hdlg, DWLP_USER, (LONG_PTR)pThis);
		pThis->mhdlg = hdlg;
	} else {
		pThis = (VDXVideoFilterDialog*)GetWindowLongPtrW(hdlg, DWLP_USER);
	}

	return pThis ? pThis->DlgProc(msg, wParam, lParam) : FALSE;
}

INT_PTR VDXVideoFilterDialog::DlgProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	return FALSE;
}

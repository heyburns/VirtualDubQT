// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2023-2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/w32assist.h>
#include <vd2/system/seh.h>
#include <vd2/system/text.h>
#include <vd2/system/vdstdc.h>
#include <vd2/system/vdstl.h>

#ifdef _WIN32

bool VDIsForegroundTaskW32() {
	HWND hwndFore = GetForegroundWindow();

	if (!hwndFore)
		return false;

	DWORD dwProcessId = 0;
	GetWindowThreadProcessId(hwndFore, &dwProcessId);

	return dwProcessId == GetCurrentProcessId();
}

LPVOID VDConvertThreadToFiberW32(LPVOID parm) {
	return ConvertThreadToFiber(parm);
}

void VDSwitchToFiberW32(LPVOID fiber) {
	SwitchToFiber(fiber);
}

int VDGetSizeOfBitmapHeaderW32(const BITMAPINFOHEADER *pHdr) {
	int palents = 0;

	if ((pHdr->biCompression == BI_RGB || pHdr->biCompression == BI_RLE4 || pHdr->biCompression == BI_RLE8) && pHdr->biBitCount <= 8) {
		palents = pHdr->biClrUsed;
		if (!palents)
			palents = 1 << pHdr->biBitCount;
	}
	int size = pHdr->biSize + palents * sizeof(RGBQUAD);

	if (pHdr->biSize < sizeof(BITMAPV4HEADER) && pHdr->biCompression == BI_BITFIELDS)
		size += sizeof(DWORD) * 3;

	return size;
}

void VDSetWindowTextW32(HWND hwnd, const wchar_t *s) {
	SetWindowTextW(hwnd, s);
}

void VDSetWindowTextFW32(HWND hwnd, const wchar_t *format, ...) {
	va_list val;

	va_start(val, format);
	{
		wchar_t buf[512];
		int r = _vsnwprintf_s(buf, std::size(buf) - 1, format, val);

		if (r >= 0) {
			VDSetWindowTextW32(hwnd, buf);
			va_end(val);
			return;
		}
	}

	VDStringW s;
	s.append_vsprintf(format, val);
	VDSetWindowTextW32(hwnd, s.c_str());

	va_end(val);
}

VDStringA VDGetWindowTextAW32(HWND hwnd) {
	char buf[512];

	int len = GetWindowTextLengthA(hwnd);

	if (len > 511) {
		vdblock<char> tmp(len + 1);
		len = GetWindowTextA(hwnd, tmp.data(), tmp.size());

		const char *s = tmp.data();
		VDStringA text(s, s+len);
		return text;
	} else if (len > 0) {
		len = GetWindowTextA(hwnd, buf, 512);

		return VDStringA(buf, buf + len);
	}

	return VDStringA();
}

VDStringW VDGetWindowTextW32(HWND hwnd) {
	union {
		wchar_t w[256];
		char a[512];
	} buf;

	{
		int len = GetWindowTextLengthW(hwnd);

		if (len > 255) {
			vdblock<wchar_t> tmp(len + 1);
			len = GetWindowTextW(hwnd, tmp.data(), tmp.size());

			VDStringW text(tmp.data(), len);
			return text;
		} else if (len > 0) {
			len = GetWindowTextW(hwnd, buf.w, 256);

			VDStringW text(buf.w, len);
			return text;
		}
	}

	return VDStringW();
}

void VDAppendMenuW32(HMENU hmenu, UINT flags, UINT id, const wchar_t *text){
	AppendMenuW(hmenu, flags, id, text);
}

bool VDAppendPopupMenuW32(HMENU hmenu, UINT flags, HMENU hmenuPopup, const wchar_t *text){
	flags |= MF_POPUP;

	return 0 != AppendMenuW(hmenu, flags, (UINT_PTR)hmenuPopup, text);
}

void VDAppendMenuSeparatorW32(HMENU hmenu) {
	int pos = GetMenuItemCount(hmenu);
	if (pos < 0)
		return;

	{
		MENUITEMINFOW mmiW;
		vdfastfixedvector<wchar_t, 256> bufW;

		mmiW.cbSize		= MENUITEMINFO_SIZE_VERSION_400W;
		mmiW.fMask		= MIIM_TYPE;
		mmiW.fType		= MFT_SEPARATOR;

		InsertMenuItemW(hmenu, pos, TRUE, &mmiW);
	}
}

void VDCheckMenuItemByPositionW32(HMENU hmenu, uint32 pos, bool checked) {
	CheckMenuItem(hmenu, pos, checked ? MF_BYPOSITION|MF_CHECKED : MF_BYPOSITION|MF_UNCHECKED);
}

void VDCheckMenuItemByCommandW32(HMENU hmenu, UINT cmd, bool checked) {
	CheckMenuItem(hmenu, cmd, checked ? MF_BYCOMMAND|MF_CHECKED : MF_BYCOMMAND|MF_UNCHECKED);
}

void VDCheckRadioMenuItemByPositionW32(HMENU hmenu, uint32 pos, bool checked) {
	MENUITEMINFOA mii;

	mii.cbSize = sizeof(MENUITEMINFOA);
	mii.fMask = MIIM_FTYPE | MIIM_STATE;
	if (GetMenuItemInfoA(hmenu, pos, TRUE, &mii)) {
		mii.fType |= MFT_RADIOCHECK;
		mii.fState &= ~MFS_CHECKED;
		if (checked)
			mii.fState |= MFS_CHECKED;
		SetMenuItemInfoA(hmenu, pos, TRUE, &mii);
	}
}

void VDCheckRadioMenuItemByCommandW32(HMENU hmenu, UINT cmd, bool checked) {
	MENUITEMINFOA mii;

	mii.cbSize = sizeof(MENUITEMINFOA);
	mii.fMask = MIIM_FTYPE | MIIM_STATE;
	if (GetMenuItemInfoA(hmenu, cmd, FALSE, &mii)) {
		mii.fType |= MFT_RADIOCHECK;
		mii.fState &= ~MFS_CHECKED;
		if (checked)
			mii.fState |= MFS_CHECKED;
		SetMenuItemInfoA(hmenu, cmd, FALSE, &mii);
	}
}

void VDEnableMenuItemByCommandW32(HMENU hmenu, UINT cmd, bool checked) {
	EnableMenuItem(hmenu, cmd, checked ? MF_BYCOMMAND|MF_ENABLED : MF_BYCOMMAND|MF_GRAYED);
}

VDStringW VDGetMenuItemTextByCommandW32(HMENU hmenu, UINT cmd) {
	VDStringW s;

	{
		MENUITEMINFOW mmiW;
		vdfastfixedvector<wchar_t, 256> bufW;

		mmiW.cbSize		= MENUITEMINFO_SIZE_VERSION_400W;
		mmiW.fMask		= MIIM_TYPE;
		mmiW.fType		= MFT_STRING;
		mmiW.dwTypeData	= NULL;
		mmiW.cch		= 0;		// required to avoid crash on NT4

		if (GetMenuItemInfoW(hmenu, cmd, FALSE, &mmiW)) {
			bufW.resize(mmiW.cch + 1, 0);
			++mmiW.cch;
			mmiW.dwTypeData = bufW.data();

			if (GetMenuItemInfoW(hmenu, cmd, FALSE, &mmiW))
				s = bufW.data();
		}
	}

	return s;
}

void VDSetMenuItemTextByCommandW32(HMENU hmenu, UINT cmd, const wchar_t *text) {
	{
		MENUITEMINFOW mmiW;

		mmiW.cbSize		= MENUITEMINFO_SIZE_VERSION_400W;
		mmiW.fMask		= MIIM_TYPE;
		mmiW.fType		= MFT_STRING;
		mmiW.dwTypeData	= (LPWSTR)text;

		SetMenuItemInfoW(hmenu, cmd, FALSE, &mmiW);
	}
}

LRESULT	VDDualCallWindowProcW32(WNDPROC wp, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return (IsWindowUnicode(hwnd) ? CallWindowProcW : CallWindowProcA)(wp, hwnd, msg, wParam, lParam);
}

LRESULT VDDualDefWindowProcW32(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return IsWindowUnicode(hwnd) ? DefWindowProcW(hwnd, msg, wParam, lParam) : DefWindowProcA(hwnd, msg, wParam, lParam);
}

EXECUTION_STATE VDSetThreadExecutionStateW32(EXECUTION_STATE esFlags) {

	EXECUTION_STATE es = SetThreadExecutionState(esFlags);

	return es;
}

bool VDSetFilePointerW32(HANDLE h, sint64 pos, DWORD dwMoveMethod)
{
	LARGE_INTEGER filepos;
	filepos.QuadPart = pos;
	BOOL result = SetFilePointerEx(h, filepos, &filepos, dwMoveMethod);

	return !!result;
}

bool VDGetFileSizeW32(HANDLE h, sint64& size)
{
	LARGE_INTEGER filesize;
	BOOL result = GetFileSizeEx(h, &filesize);

	if (!result) {
		return false;
	}

	size = filesize.QuadPart;
	return true;
}

#if !defined(_MSC_VER)
HMODULE VDGetLocalModuleHandleW32() {
	MEMORY_BASIC_INFORMATION meminfo;
	static HMODULE shmod = (VirtualQuery((HINSTANCE)&VDGetLocalModuleHandleW32, &meminfo, sizeof meminfo), (HMODULE)meminfo.AllocationBase);

	return shmod;
}
#endif

bool VDDrawTextW32(HDC hdc, const wchar_t *s, int nCount, LPRECT lpRect, UINT uFormat) {
	RECT r;
	{
		// If multiline and vcentered (not normally supported...)
		if (!((uFormat ^ DT_VCENTER) & (DT_VCENTER|DT_SINGLELINE))) {
			uFormat &= ~DT_VCENTER;

			r = *lpRect;
			if (!DrawTextW(hdc, s, nCount, &r, uFormat | DT_CALCRECT))
				return false;

			int dx = ((lpRect->right - lpRect->left) - (r.right - r.left)) >> 1;
			int dy = ((lpRect->bottom - lpRect->top) - (r.bottom - r.top)) >> 1;

			r.left += dx;
			r.right += dx;
			r.top += dy;
			r.bottom += dy;
			lpRect = &r;
		}

		return !!DrawTextW(hdc, s, nCount, lpRect, uFormat);
	}
}

bool VDPatchModuleImportTableW32(HMODULE hmod, const char *srcModule, const char *name, void *pCompareValue, void *pNewValue, void *volatile *ppOldValue) {
	return false;
}

bool VDPatchModuleExportTableW32(HMODULE hmod, const char *name, void *pCompareValue, void *pNewValue, void *volatile *ppOldValue) {
	return false;
}

HMODULE VDLoadSystemLibraryW32(const char *name) {
	return NULL;
}

#else

bool VDIsForegroundTaskW32() { return true; }
LPVOID VDConvertThreadToFiberW32(LPVOID parm) { return parm; }
void VDSwitchToFiberW32(LPVOID) {}
int VDGetSizeOfBitmapHeaderW32(const BITMAPINFOHEADER *pHdr) {
	int size = pHdr->biSize;
	if (pHdr->biBitCount <= 8) {
		int palents = pHdr->biClrUsed ? pHdr->biClrUsed : (1 << pHdr->biBitCount);
		size += palents * sizeof(RGBQUAD);
	}
	return size;
}
void VDSetWindowTextW32(HWND, const wchar_t *) {}
void VDSetWindowTextFW32(HWND, const wchar_t *, ...) {}
VDStringA VDGetWindowTextAW32(HWND) { return VDStringA(); }
VDStringW VDGetWindowTextW32(HWND) { return VDStringW(); }
void VDAppendMenuW32(HMENU, UINT, UINT, const wchar_t *) {}
bool VDAppendPopupMenuW32(HMENU, UINT, HMENU, const wchar_t *) { return false; }
void VDAppendMenuSeparatorW32(HMENU) {}
void VDCheckMenuItemByPositionW32(HMENU, uint32, bool) {}
void VDCheckMenuItemByCommandW32(HMENU, UINT, bool) {}
void VDCheckRadioMenuItemByPositionW32(HMENU, uint32, bool) {}
void VDCheckRadioMenuItemByCommandW32(HMENU, UINT, bool) {}
void VDEnableMenuItemByCommandW32(HMENU, UINT, bool) {}
VDStringW VDGetMenuItemTextByCommandW32(HMENU, UINT) { return VDStringW(); }
void VDSetMenuItemTextByCommandW32(HMENU, UINT, const wchar_t *) {}
LRESULT VDDualCallWindowProcW32(WNDPROC, HWND, UINT, WPARAM, LPARAM) { return 0; }
LRESULT VDDualDefWindowProcW32(HWND, UINT, WPARAM, LPARAM) { return 0; }
EXECUTION_STATE VDSetThreadExecutionStateW32(EXECUTION_STATE esFlags) { return esFlags; }
bool VDSetFilePointerW32(HANDLE, sint64, DWORD) { return true; }
bool VDGetFileSizeW32(HANDLE, sint64& size) { size = 0; return true; }
HMODULE VDGetLocalModuleHandleW32() { return NULL; }
bool VDDrawTextW32(HDC, const wchar_t *, int, LPRECT, UINT) { return true; }
bool VDPatchModuleImportTableW32(HMODULE, const char *, const char *, void *, void *, void *volatile *) { return false; }
bool VDPatchModuleExportTableW32(HMODULE, const char *, void *, void *, void *volatile *) { return false; }
HMODULE VDLoadSystemLibraryW32(const char *) { return NULL; }

#endif

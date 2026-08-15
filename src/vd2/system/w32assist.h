// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2023-2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_W32ASSIST_H
#define f_VD2_SYSTEM_W32ASSIST_H

#include <windows.h>

#include <vd2/system/VDString.h>

// useful constants missing from the Platform SDK

enum {
#ifdef _M_AMD64
	MENUITEMINFO_SIZE_VERSION_400A = sizeof(MENUITEMINFOA),
	MENUITEMINFO_SIZE_VERSION_400W = sizeof(MENUITEMINFOW)
#else
	MENUITEMINFO_SIZE_VERSION_400A = (offsetof(MENUITEMINFOA, cch) + sizeof(UINT)),
	MENUITEMINFO_SIZE_VERSION_400W = (offsetof(MENUITEMINFOW, cch) + sizeof(UINT))
#endif
};

// helper functions

bool		VDIsForegroundTaskW32();

LPVOID		VDConvertThreadToFiberW32(LPVOID parm);
void		VDSwitchToFiberW32(LPVOID fiber);

int			VDGetSizeOfBitmapHeaderW32(const BITMAPINFOHEADER *pHdr);
void		VDSetWindowTextW32(HWND hwnd, const wchar_t *s);
void		VDSetWindowTextFW32(HWND hwnd, const wchar_t *format, ...);
VDStringA	VDGetWindowTextAW32(HWND hwnd);
VDStringW	VDGetWindowTextW32(HWND hwnd);
void		VDAppendMenuW32(HMENU hmenu, UINT flags, UINT id, const wchar_t *text);
bool		VDAppendPopupMenuW32(HMENU hmenu, UINT flags, HMENU hmenuPopup, const wchar_t *text);
void		VDAppendMenuSeparatorW32(HMENU hmenu);
void		VDCheckMenuItemByPositionW32(HMENU hmenu, uint32 pos, bool checked);
void		VDCheckMenuItemByCommandW32(HMENU hmenu, UINT cmd, bool checked);
void		VDCheckRadioMenuItemByPositionW32(HMENU hmenu, uint32 pos, bool checked);
void		VDCheckRadioMenuItemByCommandW32(HMENU hmenu, UINT cmd, bool checked);
void		VDEnableMenuItemByCommandW32(HMENU hmenu, UINT cmd, bool checked);
VDStringW	VDGetMenuItemTextByCommandW32(HMENU hmenu, UINT cmd);
void		VDSetMenuItemTextByCommandW32(HMENU hmenu, UINT cmd, const wchar_t *text);

LRESULT		VDDualCallWindowProcW32(WNDPROC wp, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT		VDDualDefWindowProcW32(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

EXECUTION_STATE VDSetThreadExecutionStateW32(EXECUTION_STATE esFlags);

bool		VDSetFilePointerW32(HANDLE h, sint64 pos, DWORD dwMoveMethod);
bool		VDGetFileSizeW32(HANDLE h, sint64& size);

#if !defined(_MSC_VER)
	HMODULE		VDGetLocalModuleHandleW32();
#else
	extern "C" IMAGE_DOS_HEADER __ImageBase;
	inline HMODULE VDGetLocalModuleHandleW32() {
		return (HINSTANCE)&__ImageBase;
	}
#endif

bool		VDDrawTextW32(HDC hdc, const wchar_t *s, int nCount, LPRECT lpRect, UINT uFormat);

bool		VDPatchModuleImportTableW32(HMODULE hmod, const char *srcModule, const char *name, void *pCompareValue, void *pNewValue, void *volatile *ppOldValue);
bool		VDPatchModuleExportTableW32(HMODULE hmod, const char *name, void *pCompareValue, void *pNewValue, void *volatile *ppOldValue);

/// Load a library from the Windows system directory.
HMODULE		VDLoadSystemLibraryW32(const char *name);

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2007 Avery Lee
// Copyright (C) 2015-2018 Anton Shekhovtsov
// Copyright (C) 2023-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <vd2/system/VDString.h>
#include <vd2/system/filesys.h>
#include <vd2/system/zip.h>
#include <vd2/system/Error.h>
#include <vd2/system/w32assist.h>
#include "oshelper.h"
#include "version.h"

extern const wchar_t g_szError[];

VDStringW g_VDDataPath;

///////////////////////////////////////////////////////////////////////////

void Draw3DRect(VDZHDC hDC, int x, int y, int dx, int dy, bool inverted) {
	HPEN hPenOld;

	hPenOld = (HPEN)SelectObject(hDC, GetStockObject(inverted ? WHITE_PEN : BLACK_PEN));
	MoveToEx(hDC, x, y+dy-1, NULL);
	LineTo(hDC, x+dx-1, y+dy-1);
	LineTo(hDC, x+dx-1, y);
	DeleteObject(SelectObject(hDC, GetStockObject(inverted ? BLACK_PEN : WHITE_PEN)));
	MoveToEx(hDC, x, y+dy-1, NULL);
	LineTo(hDC, x, y);
	LineTo(hDC, x+dx-1, y);
	DeleteObject(SelectObject(hDC, hPenOld));
}

///////////////////////////////////////////////////////////////////////////
//
//	help support
//
///////////////////////////////////////////////////////////////////////////

VDStringW VDGetHelpPath() {
	return VDMakePath(VDGetProgramPath().c_str(), L"VirtualDub.chm");
}

void VDShowHelp(HWND hwnd, const wchar_t *filename) {
	try {
		VDStringW helpFile(VDGetHelpPath());

		if (!VDDoesPathExist(helpFile.c_str())) {
			throw MyError(L"Cannot find help file: %s", helpFile.c_str());
		}

		// If we're on Windows NT, check for the ADS and/or network drive.
		{
			VDStringW helpFileADS(helpFile);
			helpFileADS += L":Zone.Identifier";
			if (VDDoesPathExist(helpFileADS.c_str())) {
				int rv = MessageBoxA(hwnd, "VirtualDub has detected that its help file, VirtualDub.chm, has an Internet Explorer download location marker on it. This may prevent the help file from being displayed properly, resulting in \"Action canceled\" errors being displayed. Would you like to remove it?", "VirtualDub warning", MB_YESNO|MB_ICONEXCLAMATION);

				if (rv == IDYES)
					DeleteFileW(helpFileADS.c_str());
			}
		}

		if (filename) {
			helpFile.append(L"::/");
			helpFile.append(filename);
		}

		VDStringW helpCommand(VDStringW(L"\"hh.exe\" \"") + helpFile + L'"');

		PROCESS_INFORMATION pi;
		BOOL retval;

		// CreateProcess will actually modify the string that it gets, soo....
		{
			STARTUPINFOW si = {sizeof(STARTUPINFOW)};
			std::vector<wchar_t> tempbufW(helpCommand.size() + 1, 0);
			helpCommand.copy(&tempbufW[0], tempbufW.size());
			retval = CreateProcessW(NULL, &tempbufW[0], NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &si, &pi);
		}

		if (retval) {
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		} else
			throw MyWin32Error("Cannot launch HTML Help: %%s", GetLastError());
	} catch(const MyError& e) {
		e.post(hwnd, g_szError);
	}
}

bool IsFilenameOnFATVolume(const wchar_t *pszFilename) {
	VDStringW rootPath(VDFileGetRootPath(pszFilename));

	{
		DWORD dwMaxComponentLength;
		DWORD dwFSFlags;
		wchar_t szFilesystem[MAX_PATH];

		if (!GetVolumeInformationW(rootPath.c_str(),
				NULL, 0,		// Volume name buffer
				NULL,			// Serial number buffer
				&dwMaxComponentLength,
				&dwFSFlags,
				szFilesystem,
				MAX_PATH))
			return false;

		return !_wcsnicmp(szFilesystem, L"FAT", 3);
	}
}

namespace {
	HWND APIENTRY VDGetAncestorAutodetect(HWND hwnd, UINT gaFlags);

	typedef HWND (APIENTRY *tpGetAncestor)(HWND, UINT);
	tpGetAncestor g_pVDGetAncestor = VDGetAncestorAutodetect;

	HWND APIENTRY VDGetAncestorAutodetect(HWND hwnd, UINT gaFlags) {
		g_pVDGetAncestor = GetAncestor;

		return GetAncestor(hwnd, gaFlags);
	}
}

HWND VDGetAncestorW32(HWND hwnd, uint32 gaFlags) {
	return g_pVDGetAncestor(hwnd, gaFlags);
}

VDStringW VDLoadStringW32(uint32 uID, bool doSubstitutions) {
	// Credit for this function goes to Raymond Chen, who described how
	// to directly access string resources in his blog.

	VDStringW str;

	HRSRC hrsrc = FindResourceExW(NULL, RT_STRING, MAKEINTRESOURCEW(1 + (uID >> 4)), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
	if (hrsrc) {
		HGLOBAL hglob = LoadResource(NULL, hrsrc);
		if (hglob) {
			LPCWSTR pTable = (LPCWSTR)LockResource(hglob);
			if (pTable) {
				uID &= 15;

				while(uID--)
					pTable += 1 + (UINT)*pTable;

				str.assign(pTable+1, (UINT)*pTable);
			}
		}
	}

	if (doSubstitutions)
		VDSubstituteStrings(str);

	return str;
}

void VDSubstituteStrings(VDStringW& s) {
	VDStringW::size_type posLast = 0;
	VDStringW::size_type pos = s.find('$');
	if (pos == VDStringW::npos)
		return;

	VDStringW t;

	for(;;) {
		t.append(s, posLast, pos - posLast);

		if (pos == VDStringW::npos || pos+1 >= s.size())
			break;

		wchar_t c = s[pos+1];
		switch(c) {
			case L'n':
				t.append(VD_PROGRAM_NAMEW);
				break;

			case L'v':
				t.append(VD_PROGRAM_VERSIONW);
				break;

			case L'h':
				t.append(VD_PROGRAM_REV_HASHW);
				break;

			case L's':
				t.append(VD_PROGRAM_SPECIAL_BUILDW);
				break;

			case L'c':
				t.append(VD_PROGRAM_CONFIGW);
				break;

			case L'p':
				t.append(VD_PROGRAM_PLATFORM_NAMEW);
				break;
		}

		posLast = pos + 2;
		pos = s.find('$', posLast);
	}

	s = t;
}

void VDSetDataPath(const wchar_t *path) {
	g_VDDataPath = path;
}

const wchar_t *VDGetDataPath() {
	return g_VDDataPath.c_str();
}

VDStringW VDGetLocalAppDataPath() {
	int csidl = CSIDL_LOCAL_APPDATA;

	wchar_t pathW[MAX_PATH];

	if (!SHGetSpecialFolderPathW(NULL, pathW, csidl, FALSE)) {
		return VDGetProgramPath();
	}

	return VDStringW(pathW);
}

void VDCopyTextToClipboardA(const char *s) {
	if (!OpenClipboard(NULL))
		return;

	if (EmptyClipboard()) {
		size_t len = strlen(s) + 1;
		HANDLE hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len);
		if (hMem) {
			void *lpvMem = GlobalLock(hMem);
			if (lpvMem) {
				memcpy(lpvMem, s, len);

				GlobalUnlock(lpvMem);
				SetClipboardData(CF_TEXT, hMem);
				CloseClipboard();
				return;
			}
			GlobalFree(hMem);
		}
	}

	CloseClipboard();
}

void VDCopyTextToClipboardW(const wchar_t *s) {
	if (!OpenClipboard(NULL))
		return;

	if (EmptyClipboard()) {
		size_t len = sizeof(wchar_t) * (wcslen(s) + 1);
		HANDLE hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len);
		if (hMem) {
			void *lpvMem = GlobalLock(hMem);
			if (lpvMem) {
				memcpy(lpvMem, s, len);

				GlobalUnlock(lpvMem);
				SetClipboardData(CF_UNICODETEXT, hMem);
				CloseClipboard();
				return;
			}
			GlobalFree(hMem);
		}
	}

	CloseClipboard();
}

void VDCopyTextToClipboard(const wchar_t *s) {
	VDCopyTextToClipboardW(s);
}

uint32 VDCreateAutoSaveSignature() {
	FILETIME ft;
	::GetSystemTimeAsFileTime(&ft);

	return GetCurrentProcessId() ^ ft.dwLowDateTime ^ ft.dwHighDateTime;
}

///////////////////////////////////////////////////////////////////////////

void LaunchURL(const char *pURL) {
	ShellExecuteA(NULL, "open", pURL, NULL, NULL, SW_SHOWNORMAL);
}

///////////////////////////////////////////////////////////////////////////

namespace {
	void EnableShutdownPrivilegesNT() {
		HANDLE h;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &h)) {
			LUID luid;

			if (LookupPrivilegeValueW(NULL, L"SeShutdownPrivilege", &luid)) {
				TOKEN_PRIVILEGES tp;
				tp.PrivilegeCount = 1;
				tp.Privileges[0].Luid = luid;
				tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

				AdjustTokenPrivileges(h, FALSE, &tp, 0, NULL, NULL);
			}

			CloseHandle(h);
		}
	}
}

static void ExitWindowsExDammit(UINT uFlags, DWORD dwReserved) {
	EnableShutdownPrivilegesNT();

	ExitWindowsEx(uFlags, dwReserved);
}

bool VDInitiateSystemShutdownWithUITimeout(VDSystemShutdownMode mode, const wchar_t *reason, uint32 timeout) {
	if (mode != kVDSystemShutdownMode_Shutdown)
		return false;

	HMODULE hmodK32 = GetModuleHandleW(L"advapi32");
	if (!hmodK32)
		return false;

	EnableShutdownPrivilegesNT();

	typedef BOOL (WINAPI *tpInitiateSystemShutdownExW)(
		LPWSTR lpMachineName,
		LPWSTR lpMessage,
		DWORD dwTimeout,
		BOOL bForceAppsClosed,
		BOOL bRebootAfterShutdown,
		DWORD dwReason
	);

	tpInitiateSystemShutdownExW pInitiateSystemShutdownExW = (tpInitiateSystemShutdownExW)GetProcAddress(hmodK32, "InitiateSystemShutdownExW");
	if (!pInitiateSystemShutdownExW)
		return false;

	return 0 != pInitiateSystemShutdownExW(NULL, (LPWSTR)reason, timeout, TRUE, FALSE, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
}

bool VDInitiateSystemShutdown(VDSystemShutdownMode mode) {
	if (mode != kVDSystemShutdownMode_Shutdown) {
		HMODULE hmodPowrProf = LoadLibraryW(L"powrprof");
		bool success = false;

		if (hmodPowrProf) {
			typedef BOOLEAN (APIENTRY *tpSetSuspendState)(BOOL Hibernate, BOOL ForceCritical, BOOL DisableWakeEvent);
			tpSetSuspendState pSetSuspendState = (tpSetSuspendState)GetProcAddress(hmodPowrProf, "SetSuspendState");
			if (pSetSuspendState) {
				if (pSetSuspendState(mode == kVDSystemShutdownMode_Hibernate, FALSE, FALSE)) {
					success = true;
				}
			}

			FreeLibrary(hmodPowrProf);
		}

		return success;
	} else {
		// In theory, this is an illegal combination of flags, but it
		// seems to be necessary to properly power off both Windows 98
		// and Windows XP.  In particular, Windows 98 just logs off if
		// you try EWX_POWEROFF.  Joy.
		ExitWindowsExDammit(EWX_SHUTDOWN|EWX_POWEROFF|EWX_FORCEIFHUNG, 0);
		return true;
	}
}

///////////////////////////////////////////////////////////////////////////

void VDCPUUsageReader::Init() {
	FILETIME ftCreate, ftExit;

	if (GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, (FILETIME *)&kt_last, (FILETIME *)&ut_last)) {

		GetSystemTimes((FILETIME *)&idle_last, (FILETIME *)&skt_last, (FILETIME *)&sut_last);
	}
}

void VDCPUUsageReader::read(int& vd, int& sys)
{
	FILETIME ftCreate, ftExit;
	unsigned __int64 kt, ut, skt, sut, idle;

	GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, (FILETIME*)&kt, (FILETIME*)&ut);
	GetSystemTimes((FILETIME*)&idle, (FILETIME*)&skt, (FILETIME*)&sut);

	unsigned __int64 sd = (skt - skt_last) + (sut - sut_last);

	if (sd == 0) {
		vd = -1;
		sys = -1;
	} else {
		vd = (int)((100 * (kt + ut - kt_last - ut_last) + sd/2) / sd);
		sys = (int)((100 * (sd - (idle - idle_last)) + sd/2) / sd);
	}

	kt_last   = kt;
	ut_last   = ut;
	skt_last  = skt;
	sut_last  = sut;
	idle_last = idle;

	return;
}

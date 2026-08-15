// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <windows.h>
#include <vd2/system/registry.h>
#include "oshelper.h"
#include "helpfile.h"

extern const char g_szCapture[];
static const char g_szCaptureWarn[]="Disabled Warnings";

#define CWF_PINNACLE		(0x00000001L)
#define CWF_ZORAN			(0x00000002L)
#define CWF_BROOKTREE		(0x00000004L)

static long g_capwarnFlags = -1;

static void CaptureWarnInit() {
	if (g_capwarnFlags < 0) {
		VDRegistryAppKey key(g_szCapture);

		g_capwarnFlags = key.getInt(g_szCaptureWarn, 0);
	}
}

static void CaptureWarnDisable(long f) {
	g_capwarnFlags |= f;

	VDRegistryAppKey key(g_szCapture);
	key.setInt(g_szCaptureWarn, g_capwarnFlags);
}

void CaptureWarnCheckDriver(HWND hwnd, const char *s) {
	CaptureWarnInit();

	if (!(g_capwarnFlags & CWF_PINNACLE) && (strstr(s, "Pinnacle") || strstr(s, "miroVIDEO"))) {

		if (IDYES == MessageBoxA(hwnd,
			"You may experience slow GUI performance while this driver "
			"is active, depending on your video card.  Do you want to "
			"know more about this problem?"
			,
			"Miro/Pinnacle Systems driver detected",
			MB_YESNO)) {

			VDShowHelp(hwnd, L"capwarn.html");
		}

		CaptureWarnDisable(CWF_PINNACLE);
	}
}

void CaptureWarnCheckDrivers(HWND hwnd) {
	HANDLE hFind;
	WIN32_FIND_DATAW wfd;
	wchar_t szPath[MAX_PATH], *s;

	if (!(g_capwarnFlags & CWF_ZORAN)) {
		GetWindowsDirectoryW(szPath, std::size(szPath));
		s = szPath;
		while(*s) ++s;
		if (s[-1] != '\\')
			*s++ = '\\';

		wcscpy(s, L"system\\h20capt.dll");

		hFind = FindFirstFileW(szPath, &wfd);

		if (hFind == INVALID_HANDLE_VALUE) {
			wcscpy(s, L"system\\h22capt.dll");

			hFind = FindFirstFileW(szPath, &wfd);
		}

		if (hFind != INVALID_HANDLE_VALUE) {
			FindClose(hFind);

			CaptureWarnInit();

			if (IDYES == MessageBoxA(hwnd,
				"You may experience difficulty getting exact framerates "
				"with your capture card, resulting in dropped frames.  Do "
				"you want to know more about this problem?"
				,
				"Zoran drivers detected",
				MB_YESNO)) {

				VDShowHelp(hwnd, L"capwarn.html");
			}
			CaptureWarnDisable(CWF_ZORAN);
		}
	}

	if (!(g_capwarnFlags & CWF_BROOKTREE)) {
		GetWindowsDirectoryW(szPath, std::size(szPath));
		s = szPath;
		while(*s) ++s;
		if (s[-1] != '\\')
			*s++ = '\\';

		wcscpy(s, L"system\\bt848_32.dll");

		hFind = FindFirstFileW(szPath, &wfd);

		if (hFind == INVALID_HANDLE_VALUE) {
			wcscpy(s, L"system32\\bt848_32.dll");

			hFind = FindFirstFileW(szPath, &wfd);
		}

		if (hFind != INVALID_HANDLE_VALUE) {
			FindClose(hFind);

			CaptureWarnInit();

			if (IDYES == MessageBoxA(hwnd,
				"You may have difficulty capturing above 320x240 with this card.  Do "
				"you want to know more about this problem?"
				,
				"Brooktree Bt848/878 drivers detected",
				MB_YESNO)) {

				VDShowHelp(hwnd, L"capwarn.html");
			}
			CaptureWarnDisable(CWF_BROOKTREE);
		}
	}
}

// VirtualDub - Video processing and capture application
//
// Copyright (C) 2013 Avery Lee
// Copyright (C) 2024-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <windows.h>
#include "caputils.h"

VDCaptureAutoPriority::VDCaptureAutoPriority() {
	BOOL fScreenSaverState;
	BOOL fLowPowerState;
	BOOL fPowerOffState;

	SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE, 0, &fScreenSaverState, FALSE);
	SystemParametersInfoW(SPI_GETLOWPOWERACTIVE, 0, &fLowPowerState, FALSE);
	SystemParametersInfoW(SPI_GETPOWEROFFACTIVE, 0, &fPowerOffState, FALSE);

	mbScreenSaverState = 0!=fScreenSaverState;
	mbLowPowerState = 0!=fLowPowerState;
	mbPowerOffState = 0!=fPowerOffState;

	SystemParametersInfoW(SPI_SETPOWEROFFACTIVE, FALSE, NULL, FALSE);
	SystemParametersInfoW(SPI_SETLOWPOWERACTIVE, FALSE, NULL, FALSE);
	SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, FALSE, NULL, FALSE);

	HANDLE hProcess = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();

	mPreviousPriorityClass = GetPriorityClass(hProcess);
	mPreviousThreadPriority = GetThreadPriority(hThread);

	SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS);
	SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL);
}

VDCaptureAutoPriority::~VDCaptureAutoPriority() {
	HANDLE hProcess = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();

	if (GetThreadPriority(hThread) == THREAD_PRIORITY_ABOVE_NORMAL)
		SetThreadPriority(hThread, mPreviousThreadPriority);

	if (GetPriorityClass(hProcess) == HIGH_PRIORITY_CLASS)
		SetPriorityClass(hProcess, mPreviousPriorityClass);

	SystemParametersInfoW(SPI_SETPOWEROFFACTIVE, mbPowerOffState, NULL, FALSE);
	SystemParametersInfoW(SPI_SETLOWPOWERACTIVE, mbLowPowerState, NULL, FALSE);
	SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, mbScreenSaverState, NULL, FALSE);
}

namespace {
	bool VDIsDebuggerPresent() {
		return 0!= IsDebuggerPresent();
	}
}

long VDCaptureHashDriverName(const char *name) {
	long hash;
	int len=0;
	char c;

	// We don't want to have to deal with hash collisions in the Registry,
	// so instead we use the Prayer method, in conjunction with a careful
	// hash algorithm.  LMSB is the length of the string, clamped at 255;
	// LSB is the first byte of the string.  The upper 2 bytes are the
	// modulo sum of all the bytes in the string.  This way, two drivers
	// would have to start with the same letter and have description
	// strings of the exact same length in order to collide.
	//
	// It's impossible to distinguish two identical capture cards this way,
	// but what moron puts two exact same cards in his system?  Besides,
	// this way if someone yanks a card and alters the driver numbers, we
	// can still find the right config for each driver.

	hash = (long)(unsigned char)name[0];

	while(c=*name++) {
		hash += (long)(unsigned char)c << 16;
		++len;
	}
	if (len>255) len=255;
	hash |= (len<<8);

	// If some idiot driver gives us no name, we have a hash of zero.
	// We do not like zero hashes.

	if (!hash) ++hash;

	return hash;
}

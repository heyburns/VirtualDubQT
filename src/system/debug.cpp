// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2024-2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <stdio.h>
#include <stdarg.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/debug.h>
#include <vd2/system/thread.h>

#ifdef _DEBUG

VDAssertResult VDAssert(const char *exp, const char *file, int line) {
	fprintf(stderr, "%s(%d): Assert failed: %s\n", file, line, exp);
	return kVDAssertBreak;
}

VDAssertResult VDAssertPtr(const char *exp, const char *file, int line) {
	fprintf(stderr, "%s(%d): Assert failed: %s is not a valid pointer\n", file, line, exp);
	return kVDAssertBreak;
}

#endif

void VDDebugPrint(const char* format, ...)
{
	char buf[4096];
	buf[0] = 0;

	va_list val;
	va_start(val, format);
	vsnprintf(buf, sizeof(buf), format, val);
	va_end(val);

	fputs(buf, stderr);
}

void VDDebugPrint(const wchar_t* format, ...)
{
	wchar_t buf[4096];
	buf[0] = 0;

	va_list val;
	va_start(val, format);
	vswprintf(buf, sizeof(buf)/sizeof(wchar_t), format, val);
	va_end(val);

	fputws(buf, stderr);
}

namespace {
	IVDExternalCallTrap *g_pExCallTrap;
}

void VDSetExternalCallTrap(IVDExternalCallTrap *trap) {
	g_pExCallTrap = trap;
}

bool IsMMXState() {
	return false;
}

void ClearMMXState() {
}

void VDClearEvilCPUStates() {
}

void VDPreCheckExternalCodeCall(const char *file, int line) {
	(void)file; (void)line;
}

void VDPostCheckExternalCodeCall(const wchar_t *mpContext, const char *mpFile, int mLine) {
	(void)mpContext; (void)mpFile; (void)mLine;
}

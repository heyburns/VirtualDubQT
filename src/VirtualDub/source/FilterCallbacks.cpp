// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2015 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <vd2/system/cpuaccel.h>
#include "filters.h"
#include "resource.h"
#include "version.h"

extern HINSTANCE g_hInst;

static bool VDFilterCallbackIsFPUEnabled() {
	return !!FPU_enabled;
}

static bool VDFilterCallbackIsMMXEnabled() {
	return !!MMX_enabled;
}

static void VDFilterCallbackThrowExcept(const char *format, ...) {
	va_list val;
	MyError e;

	va_start(val, format);
	e.vsetf(format, val);
	va_end(val);

	throw e;
}

static void VDFilterCallbackThrowExceptMemory() {
	throw MyMemoryError();
}

// This is really disgusting...

struct VDXFilterVTbls {
	void *pvtblVBitmap;
};

static void VDFilterCallbackInitVTables(VDXFilterVTbls *pvtbls) {
	VBitmap tmp;
	pvtbls->pvtblVBitmap = *(void **)&tmp;
}

static long VDFilterCallbackGetCPUFlags() {
	return CPUGetEnabledExtensions();
}

static long VDFilterCallbackGetHostVersionInfo(char *buf, int len) {
	char tbuf[256];

	const unsigned long version_num = REV_NUM;

	LoadStringA(g_hInst, IDS_TITLE_INITIAL, tbuf, std::size(tbuf));
	_snprintf(buf, len, tbuf, version_num,
#ifdef _DEBUG
		"debug"
#else
		"release"
#endif
		);

	return version_num;
}

VDXFilterFunctions g_VDFilterCallbacks={
	FilterAdd,
	FilterRemove,
	VDFilterCallbackIsFPUEnabled,
	VDFilterCallbackIsMMXEnabled,
	VDFilterCallbackInitVTables,
	VDFilterCallbackThrowExceptMemory,
	VDFilterCallbackThrowExcept,
	VDFilterCallbackGetCPUFlags,
	VDFilterCallbackGetHostVersionInfo,
};

FilterModInitFunctions g_FilterModCallbacks={
	FilterModAdd,
};

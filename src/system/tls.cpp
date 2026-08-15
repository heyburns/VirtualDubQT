// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/tls.h>

VDThreadInitHook g_pInitHook;

void VDInitThreadData(const char *pszThreadName) {
	if (g_pInitHook)
		g_pInitHook(true, pszThreadName);
}

void VDDeinitThreadData() {
	if (g_pInitHook)
		g_pInitHook(false, NULL);
}

void VDSetThreadInitHook(VDThreadInitHook pHook) {
	g_pInitHook = pHook;
}

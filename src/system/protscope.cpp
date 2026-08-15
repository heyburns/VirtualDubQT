// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/protscope.h>

VDProtectedAutoScope *VDGetProtectedScopeLinkNull() {
	return NULL;
}

void VDSetProtectedScopeLinkNull(VDProtectedAutoScope *) {
}

tpVDGetProtectedScopeLink g_pVDGetProtectedScopeLink = VDGetProtectedScopeLinkNull;
tpVDSetProtectedScopeLink g_pVDSetProtectedScopeLink = VDSetProtectedScopeLinkNull;

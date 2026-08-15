// VirtualDub - Video processing and capture application
//
// Copyright (C) 2013 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include "plugins.h"

extern const VDXPluginInfo g_inputDrv_plugindef_TestId;

static const VDPluginInfo *const g_inputDrv_list[]={
	&g_inputDrv_plugindef_TestId,
	NULL
};

void VDInitBuiltinInputDrivers() {
	VDAddInternalPlugins(g_inputDrv_list);
}

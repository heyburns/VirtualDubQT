// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#ifdef _DEBUG
	#include <vd2/VDXFrame/VideoFilter.h>

	class VDVideoFilterDebugCrop : public VDXVideoFilter {
	public:
		uint32 GetParams();
		void Run();
	};

	uint32 VDVideoFilterDebugCrop::GetParams() {
		if (fa->dst.mFrameCount > 10)
			fa->dst.mFrameCount -= 10;
		else
			fa->dst.mFrameCount = 0;

		return 0;
	}

	void VDVideoFilterDebugCrop::Run() {
	}

	extern const VDXFilterDefinition filterDef_debugcrop = VDXVideoFilterDefinition<VDVideoFilterDebugCrop>(
		NULL,
		"__debug crop",
		"Removes output frame count debugging purposes.");

	#pragma warning(disable: 4505)	// warning C4505: 'VDXVideoFilter::[thunk]: __thiscall VDXVideoFilter::`vcall'{44,{flat}}' }'' : unreferenced local function has been removed
#endif

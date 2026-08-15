// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#ifdef _DEBUG
	#include <vd2/VDXFrame/VideoFilter.h>

	class VDVideoFilterDebugError : public VDXVideoFilter {
	public:
		uint32 GetParams();
		void Run();
	};

	uint32 VDVideoFilterDebugError::GetParams() {
		return 0;
	}

	void VDVideoFilterDebugError::Run() {
		const VDXFBitmap& dst = *fa->mpOutputFrames[0];

		if (dst.mFrameNumber == 20)
			ff->Except("Sample error");
	}

	extern const VDXFilterDefinition filterDef_debugerror = VDXVideoFilterDefinition<VDVideoFilterDebugError>(
		NULL,
		"__debug error",
		"Introduces errors into the video filter system for debugging purposes.");

	#pragma warning(disable: 4505)	// warning C4505: 'VDXVideoFilter::[thunk]: __thiscall VDXVideoFilter::`vcall'{44,{flat}}' }'' : unreferenced local function has been removed
#endif

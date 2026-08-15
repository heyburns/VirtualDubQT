// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2015 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_FILTDLG_H
#define f_FILTDLG_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>
#include <vd2/system/fraction.h>

class IVDVideoSource;

struct VDVideoFiltersDialogResult {
	bool	mbDialogAccepted;
	bool	mbChangeDetected;
	bool	mbRescaleRequested;

	VDFraction	mOldFrameRate;
	sint64		mOldFrameCount;
	VDFraction	mNewFrameRate;
	sint64		mNewFrameCount;
};

VDVideoFiltersDialogResult VDShowDialogVideoFilters(VDGUIHandle h, IVDVideoSource *pVS, VDPosition initialTime, int edit_instance=-1, HWND* owner_ref=0);
VDVideoFiltersDialogResult VDShowDialogVideoFilters(VDGUIHandle hParent, int w, int h, int format, const VDFraction& rate, sint64 length, VDPosition initialTime);

#endif

// VirtualDub - Video processing and capture application
// A/V interface library
//
// Copyright (C) 1998-2004 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_CAPDRIVERS_H
#define f_VD2_RIZA_CAPDRIVERS_H

class IVDCaptureSystem;

IVDCaptureSystem *VDCreateCaptureSystemVFW();
IVDCaptureSystem *VDCreateCaptureSystemDS();
IVDCaptureSystem *VDCreateCaptureSystemScreen();

#endif

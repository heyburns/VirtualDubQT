// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 2013 Avery Lee
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_HALFFLOAT_H
#define f_VD2_SYSTEM_HALFFLOAT_H

#include <vd2/system/vdtypes.h>

uint16 VDConvertFloatToHalf(const void *f);
void VDConvertHalfToFloat(uint16 h, void *dst);

#endif	// f_VD2_SYSTEM_HALFFLOAT_H

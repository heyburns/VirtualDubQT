// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_BITMATH_H
#define f_VD2_SYSTEM_BITMATH_H

#ifdef _MSC_VER
	#pragma once
#endif

#ifndef f_VD2_SYSTEM_VDTYPES_H
	#include <vd2/system/vdtypes.h>
#endif

int VDCountBits(uint32 v);
int VDFindLowestSetBit(uint32 v);
int VDFindHighestSetBit(uint32 v);
uint32 VDCeilToPow2(uint32 v);

union VDFloatAsInt {
	float f;
	sint32 i;
};

union VDIntAsFloat {
	sint32 i;
	float f;
};

inline sint32 VDGetFloatAsInt(float f) {
	const VDFloatAsInt conv = { f };
	return conv.i;
}

inline float VDGetIntAsFloat(sint32 i) {
	const VDIntAsFloat conv = { i };
	return conv.f;
}

///////////////////////////////////////////////////////////////////////////////

#include <vd2/system/win32/intrin.h>
#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)

inline int VDFindLowestSetBit(uint32 v) {
	unsigned long index;
	return _BitScanForward(&index, v) ? index : 32;
}

inline int VDFindHighestSetBit(uint32 v) {
	unsigned long index;
	return _BitScanReverse(&index, v) ? index : -1;
}

inline int VDFindLowestSetBitFast(uint32 v) {
	unsigned long index;
	_BitScanForward(&index, v);
	return index;
}

inline int VDFindHighestSetBitFast(uint32 v) {
	unsigned long index;
	_BitScanReverse(&index, v);
	return index;
}

#endif

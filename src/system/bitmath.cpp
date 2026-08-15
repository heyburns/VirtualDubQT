// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/bitmath.h>

int VDCountBits(uint32 v) {
	v -= (v >> 1) & 0x55555555;
	v = ((v & 0xcccccccc) >> 2) + (v & 0x33333333);
	v = (v + (v >> 4)) & 0x0f0f0f0f;
	return (v * 0x01010101) >> 24;
}

uint32 VDCeilToPow2(uint32 v) {
	v += v;
	--v;

	while(uint32 x = v & (v - 1))
		v = x;

	return v;
}

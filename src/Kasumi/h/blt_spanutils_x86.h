// VirtualDub - Video processing and capture application
// Graphics support library
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_KASUMI_BLT_SPANUTILS_X86_H
#define f_VD2_KASUMI_BLT_SPANUTILS_X86_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>

namespace nsVDPixmapSpanUtils {
	void horiz_expand2x_coaligned_ISSE(uint8 *dst, const uint8 *src, sint32 w);
	void horiz_expand4x_coaligned_MMX(uint8 *dst, const uint8 *src, sint32 w);
	void vert_expand2x_centered_ISSE(uint8 *dst, const uint8 *const *srcs, sint32 w, uint8 phase);
	void vert_expand4x_centered_ISSE(uint8 *dst, const uint8 *const *srcs, sint32 w, uint8 phase);
}

#endif

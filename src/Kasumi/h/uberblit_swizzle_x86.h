// VirtualDub - Video processing and capture application
// Graphics support library
//
// Copyright (C) 1998-2008 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_KASUMI_UBERBLIT_SWIZZLE_X86_H
#define f_VD2_KASUMI_UBERBLIT_SWIZZLE_X86_H

#include "uberblit_swizzle.h"

class VDPixmapGen_8In16_Even_MMX : public VDPixmapGen_8In16 {
protected:
	void Compute(void *dst0, sint32 y);
};

class VDPixmapGen_8In16_Odd_MMX : public VDPixmapGen_8In16 {
protected:
	void Compute(void *dst0, sint32 y);
};

class VDPixmapGen_8In32_MMX : public VDPixmapGen_8In32 {
protected:
	void Compute(void *dst0, sint32 y);
};

class VDPixmapGen_Swap8In16_MMX : public VDPixmapGen_Swap8In16 {
protected:
	void Compute(void *dst0, sint32 y);
};

///////////////////////////////////////////////////////////////////////////////////////////////////
//
//	16-bit interleavers
//
///////////////////////////////////////////////////////////////////////////////////////////////////

class VDPixmapGen_B8x2_To_B8R8_MMX : public VDPixmapGen_B8x2_To_B8R8 {
protected:
	void Compute(void *dst0, sint32 y);
};

///////////////////////////////////////////////////////////////////////////////////////////////////
//
//	32-bit interleavers
//
///////////////////////////////////////////////////////////////////////////////////////////////////

class VDPixmapGen_B8x3_To_G8B8_G8R8_MMX : public VDPixmapGen_B8x3_To_G8B8_G8R8 {
protected:
	void Compute(void *dst0, sint32 y);
};

class VDPixmapGen_B8x3_To_B8G8_R8G8_MMX : public VDPixmapGen_B8x3_To_B8G8_R8G8 {
protected:
	void Compute(void *dst0, sint32 y);
};

#endif

// VirtualDub - Video processing and capture application
// Graphics support library
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_KASUMI_TEXT_H
#define f_VD2_KASUMI_TEXT_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vectors.h>

class VDPixmapPathRasterizer;

struct VDOutlineFontGlyphInfo {
	uint16	mPointArrayStart;		// start of points (encoded as 8:8)
	uint16	mCommandArrayStart;		// start of commands (encoded as 6:2 RLE).
	sint16	mAWidth;				// advance from start to character cell
	sint16	mBWidth;				// width of character cell
	sint16	mCWidth;				// advance from character cell to end
};

struct VDOutlineFontInfo {
	const uint16 *mpPointArray;
	const uint8 *mpCommandArray;
	const VDOutlineFontGlyphInfo *mpGlyphArray;
	int		mStartGlyph;
	int		mEndGlyph;
	int		mMinX;
	int		mMinY;
	int		mMaxX;
	int		mMaxY;
	int		mEmSquare;
	int		mAscent;
	int		mDescent;
	int		mLineGap;
};

struct VDTextLayoutMetrics {
	vdrect32f	mExtents;
	float		mAdvance;
};

void VDPixmapGetTextExtents(const VDOutlineFontInfo *font, float size, const char *pText, VDTextLayoutMetrics& out_Metrics);
void VDPixmapConvertTextToPath(VDPixmapPathRasterizer& rast, const VDOutlineFontInfo *font, float size, float x, float y, const char *pText, const float transform[2][2] = NULL);

struct VDBitmapFontInfo {
	const uint8 *mpBitsArray;
	const uint16 *mpPosArray;
	uint8		mStartChar;
	uint8		mEndChar;
	int			mCellWidth;
	int			mCellHeight;
	int			mCellAscent;
	int			mCellAdvance;
	int			mLineGap;
};

void VDPixmapDrawText(const VDPixmap& pxdst, const VDBitmapFontInfo *font, int x, int y, uint32 fore, uint32 back, const char *pText);

#endif

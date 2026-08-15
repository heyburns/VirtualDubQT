// VirtualDub - Video processing and capture application
// Graphics support library
//
// Copyright (C) 2013 Avery Lee
// Copyright (C) 2016-2018 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_KASUMI_BLITTER_H
#define f_VD2_KASUMI_BLITTER_H

#include <vd2/system/vectors.h>
#include <vd2/system/vdstring.h>
#include <vd2/kasumi/pixmap.h>

struct VDPixmap;
struct VDPixmapLayout;
class IVDPixmapGen;
class IVDPixmapExtraGen;

class IVDPixmapBlitter {
public:
	VDStringA profiler_comment;

	virtual ~IVDPixmapBlitter() {}
	virtual void Blit(const VDPixmap& dst, const VDPixmap& src) = 0;
	virtual void Blit(const VDPixmap& dst, const vdrect32 *rDst, const VDPixmap& src) = 0;
	void SetComment(const VDPixmap& dst, const VDPixmap& src);
	void SetComment(const VDPixmapLayout& dst, const VDPixmapLayout& src);
};

IVDPixmapBlitter *VDPixmapCreateBlitter(const VDPixmap& dst, const VDPixmap& src, IVDPixmapExtraGen* extraDst=0);
IVDPixmapBlitter *VDPixmapCreateBlitter(const VDPixmapLayout& dst, const VDPixmapLayout& src, IVDPixmapExtraGen* extraDst=0);
IVDPixmapExtraGen* VDPixmapCreateNormalizer(int format, FilterModPixmapInfo& out_info, bool useAlpha=true);

class VDPixmapCachedBlitter {
	VDPixmapCachedBlitter(const VDPixmapCachedBlitter&);
	VDPixmapCachedBlitter& operator=(const VDPixmapCachedBlitter&);
public:
	VDStringA profiler_comment;

	VDPixmapCachedBlitter();
	~VDPixmapCachedBlitter();

	void Update(const VDPixmap& dst, const VDPixmap& src);
	void Blit(const VDPixmap& dst, const VDPixmap& src);
	void Invalidate();

protected:
	sint32 mSrcWidth;
	sint32 mSrcHeight;
	VDPixmapFormatEx mSrcFormat;
	sint32 mDstWidth;
	sint32 mDstHeight;
	VDPixmapFormatEx mDstFormat;
	IVDPixmapBlitter *mpCachedBlitter;
};

#endif

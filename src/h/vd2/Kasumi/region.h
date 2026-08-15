// VirtualDub - Video processing and capture application
// Graphics support library
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2018-2020 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_KASUMI_REGION_H
#define f_VD2_KASUMI_REGION_H

struct VDPixmap;

#include <vd2/system/vectors.h>
#include <vd2/system/vdstl.h>
#include <Windows.h>

class VDPixmapRegion {
public:
	void swap(VDPixmapRegion& x);
	void clear();

public:
	vdfastvector<uint32> mSpans;
	vdrect32	mBounds;
};

class VDPixmapPathRasterizer {
public:
	VDPixmapPathRasterizer();
	VDPixmapPathRasterizer(const VDPixmapPathRasterizer&);	// no-op
	~VDPixmapPathRasterizer();

	VDPixmapPathRasterizer& operator=(const VDPixmapPathRasterizer&);	// no-op

	void Clear();
	void QuadraticBezier(const vdint2 pts[4]);
	void CubicBezier(const vdint2 pts[4]);
	void Line(const vdint2& pt1, const vdint2& pt2);
	void FastLine(int x0, int y0, int x1, int y1);

	void ScanConvert(VDPixmapRegion& region);

protected:
	void ClearEdgeList();
	void FreeEdgeLists();
	void ClearScanBuffer();
	void ReallocateScanBuffer(int ymin, int ymax);

	struct Edge {
		Edge *next;
		int posandflag;
	};

	enum { kEdgeBlockMax = 1024 };

	struct EdgeBlock {
		EdgeBlock *next;
		Edge edges[1024];

		EdgeBlock(EdgeBlock *p) : next(p) {}
	};

	struct Scan {
		Edge *chain;
		uint32 count;
	};

	EdgeBlock *mpEdgeBlocks;
	EdgeBlock *mpFreeEdgeBlocks;
	int mEdgeBlockIdx;
	Scan *mpScanBuffer;
	Scan *mpScanBufferBiased;
	int mScanYMin;
	int mScanYMax;
};

bool VDPixmapFillRegion(const VDPixmap& dst, const VDPixmapRegion& region, int x, int y, uint32 color);
bool VDPixmapFillPixmapAntialiased8x(const VDPixmap& dst, const VDPixmapRegion& region, int x, int y, uint32 color);

void VDPixmapCreateRoundRegion(VDPixmapRegion& dst, float r);
void VDPixmapConvolveRegion(VDPixmapRegion& dst, const VDPixmapRegion& r1, const VDPixmapRegion& r2, VDPixmapRegion *tempCache = NULL);

void VDPixmapConvertGDIPathToPath(VDPixmapPathRasterizer& rast, vdfastvector<POINT>& point, vdfastvector<unsigned char>& flag);

#endif

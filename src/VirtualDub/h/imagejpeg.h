// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2004 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_IMAGEJPEG_H
#define f_IMAGEJPEG_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vector>

class VDINTERFACE IVDJPEGEncoder {
public:
	enum ImageFormat {
		kFormatRGB15,
		kFormatRGB24,
		kFormatRGB32
	};

	enum eChromaMode {
		kYCC444,			// 4:4:4 YCrCb (24 bits/pixel)
		kYCC422,			// 4:2:2 YCrCb (16 bits/pixel) (Motion JPEG)
		kYCC420,			// 4:2:0 YCrCb (12 bits/pixel) (MPEG-1)
	};

	virtual ~IVDJPEGEncoder() {}

	virtual void Init(int quality = 50, bool bOptimizeHuffmanTables = false, eChromaMode cmode = kYCC420) = 0;
	virtual void Encode(vdfastvector<char>& dst, const char *src, ptrdiff_t stride_pixels, int format, int w, int h) = 0;
};

IVDJPEGEncoder *VDCreateJPEGEncoder();

#endif

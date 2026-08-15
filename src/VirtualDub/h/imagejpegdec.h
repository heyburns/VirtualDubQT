// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2004 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_IMAGEJPEGDEC_H
#define f_IMAGEJPEGDEC_H

#include <vd2/system/vdtypes.h>

class IVDJPEGDecoder {
public:
	enum {
		kFormatXRGB1555,
		kFormatRGB888,
		kFormatXRGB8888
	};

	virtual ~IVDJPEGDecoder() {}

	virtual void Begin(const void *src, uint32 srclen) = 0;
	virtual void DecodeHeader(int& w, int& h) = 0;
	virtual void DecodeImage(void *dst, ptrdiff_t dstpitch, int format) = 0;
	virtual void End() = 0;
};

IVDJPEGDecoder *VDCreateJPEGDecoder();

#endif

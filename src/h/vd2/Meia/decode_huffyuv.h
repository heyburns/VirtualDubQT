// VirtualDub - Video processing and capture application
// Video decoding library
//
// Copyright (C) 1998-2008 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_MEIA_DECODE_HUFFYUV_H
#define f_VD2_MEIA_DECODE_HUFFYUV_H

#include <vd2/system/vdtypes.h>

struct VDPixmap;

class VDINTERFACE IVDVideoDecoderHuffyuv {
public:
	virtual ~IVDVideoDecoderHuffyuv() {}
	virtual void		Init(uint32 w, uint32 h, uint32 depth, const uint8 *extradata, uint32 extralen) = 0;
	virtual void		DecompressFrame(const void *src, uint32 len) = 0;
	virtual VDPixmap	GetFrameBuffer() = 0;
};

IVDVideoDecoderHuffyuv *VDCreateVideoDecoderHuffyuv();

#endif

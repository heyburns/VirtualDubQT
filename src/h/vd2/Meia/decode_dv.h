// VirtualDub - Video processing and capture application
// Video decoding library
//
// Copyright (C) 1998-2004 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_MEIA_DECODE_DV_H
#define f_VD2_MEIA_DECODE_DV_H

#include <vd2/system/vdtypes.h>

struct VDPixmap;

class VDINTERFACE IVDVideoDecoderDV {
public:
	virtual ~IVDVideoDecoderDV() {}
	virtual void		DecompressFrame(const void *src, bool isPAL) = 0;
	virtual VDPixmap	GetFrameBuffer() = 0;
};

IVDVideoDecoderDV *VDCreateVideoDecoderDV();

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2006 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_IMAGEIFF_H
#define f_VD2_IMAGEIFF_H

#include <vd2/system/vdtypes.h>

struct VDPixmap;

class VDINTERFACE IVDImageDecoderIFF {
public:
	virtual ~IVDImageDecoderIFF() {}

	virtual const VDPixmap& Decode(const void *src, uint32 srclen) = 0;
};

IVDImageDecoderIFF *VDCreateImageDecoderIFF();

#endif

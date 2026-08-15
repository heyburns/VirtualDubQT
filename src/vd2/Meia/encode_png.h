// VirtualDub - Video processing and capture application
// Video decoding/encoding library
//
// Copyright (C) 1998-2006 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_MEIA_ENCODE_PNG_H
#define f_VD2_MEIA_ENCODE_PNG_H

#include <vd2/system/vdtypes.h>

struct VDPixmap;

class VDINTERFACE IVDImageEncoderPNG {
public:
	virtual ~IVDImageEncoderPNG() {}
	virtual void Encode(const VDPixmap& px, const void *&p, uint32& len, bool quick_compress) = 0;
};

IVDImageEncoderPNG *VDCreateImageEncoderPNG();

#endif

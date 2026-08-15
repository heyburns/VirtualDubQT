// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_MJPEGDECODER_H
#define f_MJPEGDECODER_H

#include "VirtualDub.h"

class IMJPEGDecoder {
public:
	virtual ~IMJPEGDecoder() {};

	virtual void decodeFrameRGB15(uint32 *output, uint8 *input, int len)=0;
	virtual void decodeFrameRGB32(uint32 *output, uint8 *input, int len)=0;
	virtual void decodeFrameUYVY(uint32 *output, uint8 *input, int len)=0;
	virtual void decodeFrameYUY2(uint32 *output, uint8 *input, int len)=0;
};

IMJPEGDecoder *CreateMJPEGDecoder(int w, int h);

#endif

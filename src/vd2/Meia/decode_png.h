// VirtualDub - Video processing and capture application
// Video decoding library
//
// Copyright (C) 1998-2004 Avery Lee
// Copyright (C) 2016 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_MEIA_DECODE_PNG_H
#define f_VD2_MEIA_DECODE_PNG_H

#include <vd2/system/vdtypes.h>

struct VDPixmap;

enum PNGDecodeError {
	kPNGDecodeOK,
	kPNGDecodeNotPNG,
	kPNGDecodeTruncatedChunk,
	kPNGDecodeBadHeader,
	kPNGDecodeUnsupportedCompressionAlgorithm,
	kPNGDecodeUnsupportedInterlacingAlgorithm,
	kPNGDecodeUnsupportedFilterAlgorithm,
	kPNGDecodeBadPalette,
	kPNGDecodeDecompressionFailed,
	kPNGDecodeBadFilterMode,
	kPNGDecodeUnknownRequiredChunk,
	kPNGDecodeOutOfMemory,
	kPNGDecodeChecksumFailed,
	kPNGDecodeUnsupported
};

class VDINTERFACE IVDImageDecoderPNG {
public:
	virtual ~IVDImageDecoderPNG() {}
	virtual PNGDecodeError Decode(const void *src, uint32 size) = 0;
	virtual const VDPixmap& GetFrameBuffer() = 0;
	virtual bool IsAlphaPresent() const = 0;
};

IVDImageDecoderPNG *VDCreateImageDecoderPNG();

bool VDDecodePNGHeader(const void *src, uint32 len, int& w, int& h, bool& hasalpha);

#endif

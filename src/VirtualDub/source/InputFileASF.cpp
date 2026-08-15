// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
// Copyright (C) 2020 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include "InputFile.h"
#include <vd2/system/error.h>

class VDInputDriverASF : public vdrefcounted<IVDInputDriver> {
public:
	const wchar_t *GetSignatureName() { return NULL; }

	uint32 GetFlags() { return kF_Video | kF_Audio; }

	int GetDefaultPriority() {
		return -1;
	}

	const wchar_t *GetFilenamePattern() {
		return NULL;
	}

	bool DetectByFilename(const wchar_t *pszFilename) {
		return false;
	}

	DetectionConfidence DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
		const static unsigned char asf_sig[]={
			0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
			0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c
		};

		if (nHeaderSize < 16) return kDC_Error_MoreData;
		if (nHeaderSize >= 16) {
			if (!memcmp(pHeader, asf_sig, 16))
				return kDC_VeryLow;
		}

		return kDC_None;
	}

	InputFile *CreateInputFile(uint32 flags) {
		throw MyError("ASF files are not supported.");
	}
};

extern IVDInputDriver *VDCreateInputDriverASF() { return new VDInputDriverASF; }

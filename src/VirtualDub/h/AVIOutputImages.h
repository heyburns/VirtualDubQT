// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2015-2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUT_IMAGES_H
#define f_AVIOUTPUT_IMAGES_H

#include <vd2/system/VDString.h>
#include "AVIOutput.h"

class VideoSource;

class AVIOutputImages : public AVIOutput {
protected:
	VDStringW mPrefix;
	VDStringW mSuffix;
	int mDigits;
	int mStart;
	int mFormat;
	int mQuality;

public:
	enum {
		kFormatBMP,
		kFormatTGA,
		kFormatJPEG,
		kFormatPNG,
		kFormatTGAUncompressed,
		kFormatTIFF_LZW,
		kFormatTIFF_RAW,
		kFormatTIFF_ZIP,
		kFormatCount
	};

	AVIOutputImages(const wchar_t *pszPrefix, const wchar_t *pszSuffix, int iDigits, int start, int format, int q);
	~AVIOutputImages();

	IVDMediaOutputStream *createVideoStream();
	IVDMediaOutputStream *createAudioStream();

	bool init(const wchar_t *szFile);
	void finalize();

	static void WriteSingleImage(const wchar_t *name, int format, int q, VDPixmap* px);
};

#endif

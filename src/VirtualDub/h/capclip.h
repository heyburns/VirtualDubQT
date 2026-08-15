// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_CAPCLIP_H
#define f_CAPCLIP_H

#include <windows.h>
#include <vfw.h>

#include "VBitmap.h"

class CaptureFrameSource {
private:
	BITMAPINFOHEADER	bmihDecomp;
	BITMAPINFOHEADER	*bmihSrc;
	HIC					hic;
	VBitmap				vbAnalyze;
	void *				pFrameBuffer;
	bool				fDecompressionOk;

	void _destruct();

public:
	CaptureFrameSource(HWND);
	~CaptureFrameSource();

	bool CheckFrameSize(int w, int h);
	const VBitmap *Decompress(VIDEOHDR *pvhdr);
	const VBitmap *getFrameBuffer() {
		return &vbAnalyze;
	}
};

#endif

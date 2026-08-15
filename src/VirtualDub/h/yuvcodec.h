// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2024 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_YUVDRIVER_H
#define f_YUVDRIVER_H

#include "IVideoDriver.h"
#include "CVideoCompressor.h"

class YUVCodecDriver : public IVideoDriver {
public:
	virtual ~YUVCodecDriver();

	BOOL	Load(HDRVR hDriver);
	void	Free(HDRVR hDriver);
	void*	Open(HDRVR hDriver, char *szDescription, LPVIDEO_OPEN_PARMS lpVideoOpenParms);
	void	Disable(HDRVR hDriver);
	void	Enable(HDRVR hDriver);
	LRESULT	Default(DWORD dwDriverID, HDRVR hDriver, UINT uiMessage, LPARAM lParam1, LPARAM lParam2);
};

class YUVCodec : public CVideoCompressor {
public:
	YUVCodec();
	~YUVCodec();

	LRESULT Compress(ICCOMPRESS *icc, DWORD cbSize);
	LRESULT CompressFramesInfo(ICCOMPRESSFRAMES *icf, DWORD cbSize);
	LRESULT CompressGetFormat(BITMAPINFO *lpbiInput, BITMAPINFO *lpbiOutput);
	LRESULT CompressGetSize(BITMAPINFO *lpbiInput, BITMAPINFO *lpbiOutput);
	LRESULT CompressQuery(BITMAPINFO *lpbiInput, BITMAPINFO *lpbiOutput);
	LRESULT DecompressGetFormat(BITMAPINFO *lpbiInput, BITMAPINFO *lpbiOutput);
	LRESULT DecompressEx(ICDECOMPRESSEX *icdex, DWORD cbSize);
	LRESULT DecompressExQuery(ICDECOMPRESSEX *icdex, DWORD cbSize);
	LRESULT GetInfo(ICINFO *lpicinfo, DWORD cbSize);
};

extern YUVCodecDriver g_YUVCodecDriver;

#endif

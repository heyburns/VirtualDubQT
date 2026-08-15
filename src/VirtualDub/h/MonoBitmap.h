// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_MONOBITMAP_H
#define f_MONOBITMAP_H

#include <windows.h>

class MonoBitmap {
private:
	struct {
		BITMAPINFO bi;
		COLORREF crFore;
	} bi;

	HBITMAP		hbm;
	HDC			hdcCompat;
	HBRUSH		hbrBlack;
	void *		lpvBits;
	int			iPitch;

public:
	MonoBitmap(HDC hdcRef, int width, int height, COLORREF crFore, COLORREF crBack);
	~MonoBitmap();

	void	Clear();
	void *	getBits() { return lpvBits; }
	HDC		getDC() { return hdcCompat; }
	int		getPitch() { return iPitch; }
	void	BitBlt(HDC hdcDest, LONG x, LONG y);
	void	BitBlt(HDC hdcDest, LONG dx, LONG dy, LONG sx, LONG sy, LONG w, LONG h);
};

#endif

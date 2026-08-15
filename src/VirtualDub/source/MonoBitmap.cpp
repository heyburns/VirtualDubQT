// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include "MonoBitmap.h"

MonoBitmap::MonoBitmap(HDC hdcRef, int width, int height, COLORREF crFore, COLORREF crBack) {
	HDC hdcDisplay = NULL;

	if (!hdcRef)
		hdcRef = hdcDisplay = CreateDCW(L"DISPLAY", NULL, NULL, NULL);

	hdcCompat = CreateCompatibleDC(hdcRef);

	iPitch = ((width + 31)/32)*4;

	bi.bi.bmiHeader.biSize			= sizeof(BITMAPINFOHEADER);
	bi.bi.bmiHeader.biWidth			= width;
	bi.bi.bmiHeader.biHeight		= height;
	bi.bi.bmiHeader.biPlanes		= 1;
	bi.bi.bmiHeader.biBitCount		= 1;
	bi.bi.bmiHeader.biCompression	= BI_RGB;
	bi.bi.bmiHeader.biSizeImage		= iPitch*height;
	bi.bi.bmiHeader.biXPelsPerMeter	= 100;
	bi.bi.bmiHeader.biYPelsPerMeter	= 100;
	bi.bi.bmiHeader.biClrUsed		= 2;
	bi.bi.bmiHeader.biClrImportant	= 2;
	bi.bi.bmiColors[0].rgbBlue	= GetBValue(crBack);
	bi.bi.bmiColors[0].rgbGreen	= GetGValue(crBack);
	bi.bi.bmiColors[0].rgbRed	= GetRValue(crBack);
	bi.bi.bmiColors[1].rgbBlue	= GetBValue(crFore);
	bi.bi.bmiColors[1].rgbGreen	= GetGValue(crFore);
	bi.bi.bmiColors[1].rgbRed	= GetRValue(crFore);

	hbm = CreateDIBSection(hdcCompat, &bi.bi, DIB_RGB_COLORS, &lpvBits, NULL, 0);
	SelectObject(hdcCompat, hbm);

	if (hdcDisplay) DeleteDC(hdcDisplay);

}

MonoBitmap::~MonoBitmap() {
	DeleteDC(hdcCompat);
	DeleteObject(hbm);
}

void MonoBitmap::Clear() {
	GdiFlush();
	memset(lpvBits, 0, bi.bi.bmiHeader.biSizeImage);
}

void MonoBitmap::BitBlt(HDC hdcDest, LONG x, LONG y) {
	BitBlt(hdcDest, x, y, 0, 0, bi.bi.bmiHeader.biWidth, bi.bi.bmiHeader.biHeight);
}

void MonoBitmap::BitBlt(HDC hdcDest, LONG dx, LONG dy, LONG sx, LONG sy, LONG w, LONG h) {
	::BitBlt(hdcDest,
		dx, dy,
		w, h,
		hdcCompat,
		sx, sy,
		SRCCOPY);
}

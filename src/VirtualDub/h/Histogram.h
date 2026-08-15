// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_HISTOGRAM_H
#define f_HISTOGRAM_H

#include <windows.h>

class VBitmap;

extern "C" void asm_histogram_gray_run(void *dst,long width,long height,long stride,long *histo_table);
extern "C" void asm_histogram_gray24_run(void *dst,long width,long height,long stride,long *histo_table);
extern "C" void asm_histogram_color_run(char *dst,long width,long height,long stride,long *histo_table);
extern "C" void asm_histogram_color24_run(char *dst,long width,long height,long stride,long *histo_table);
extern "C" void asm_histogram16_run(void *dst,long width,long height,long stride,long *histo_table, long pixmask);

class Histogram {
private:
	long histo[256];
	long total_pixels;
	static const RGBQUAD fore_colors[];

	char *lpGraphBits;
	HBITMAP hBitmapGraph;
	HDC hDCGraph;
	int max_height;

	int histo_mode;

public:
	enum {
		MODE_BLUE = 0,
		MODE_GREEN = 1,
		MODE_RED = 2,
		MODE_GRAY = 3,
		NUM_MODES,
		MODE_NEXT = -1,
	};

	Histogram(HDC hDC, int max_height);
	~Histogram();

	void Zero();
	void Process(const VBitmap *vbmp);
	void Process24(const VBitmap *vbmp);
	void Process16(const VBitmap *vbmp);
	void ComputeHeights(long *height_ptrs, long graph_height);
	void Draw(HDC hDC, LPRECT lpr);

	void SetMode(int);
	int GetMode();
};

#endif

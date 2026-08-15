// VirtualDub - Video processing and capture application
// Graphics support library
//
// Copyright (C) 2013 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "resample_stages_x64.h"

extern "C" long vdasm_resize_table_row_SSE2(uint32 *out, const uint32 *in, const int *filterBase, int ksize, uint32 w, long u, long dudx) {
	uint32 *dst = out;
	const uint32 *src = in;
	do {
		const uint32 *src2 = src + (u>>16);
		const sint32 *filter = filterBase + ksize*((u>>8)&0xff);
		u += dudx;

		int r = 0x2000, g = 0x2000, b = 0x2000;
		for(int i = ksize; i; --i) {
			uint32 p = *src2++;
			sint32 coeff = *filter++;

			r += ((p>>16)&0xff)*coeff;
			g += ((p>> 8)&0xff)*coeff;
			b += ((p    )&0xff)*coeff;
		}

		r <<= 2;
		g >>= 6;
		b >>= 14;

		if ((uint32)r >= 0x01000000)
			r = ~r >> 31;
		if ((uint32)g >= 0x00010000)
			g = ~g >> 31;
		if ((uint32)b >= 0x00000100)
			b = ~b >> 31;

		*dst++ = (r & 0xff0000) + (g & 0xff00) + (b & 0xff);
	} while(--w);
	return 0;
}

extern "C" long vdasm_resize_table_col_SSE2(uint32 *out, const uint32 *const *src, const int *filter, int ksize, uint32 w) {
	uint32 *dst = out;
	for(uint32 i=0; i<w; ++i) {
		int r = 0x2000, g = 0x2000, b = 0x2000;
		const sint32 *filter2 = filter;
		const uint32 *const *src2 = src;

		for(int j = ksize; j; --j) {
			uint32 p = (*src2++)[i];
			sint32 coeff = *filter2++;

			r += ((p>>16)&0xff)*coeff;
			g += ((p>> 8)&0xff)*coeff;
			b += ((p    )&0xff)*coeff;
		}

		r <<= 2;
		g >>= 6;
		b >>= 14;

		if ((uint32)r >= 0x01000000)
			r = ~r >> 31;
		if ((uint32)g >= 0x00010000)
			g = ~g >> 31;
		if ((uint32)b >= 0x00000100)
			b = ~b >> 31;

		*dst++ = (r & 0xff0000) + (g & 0xff00) + (b & 0xff);
	}
	return 0;
}


VDResamplerSeparableTableRowStageSSE2::VDResamplerSeparableTableRowStageSSE2(const IVDResamplerFilter& filter)
	: VDResamplerRowStageSeparableTable32(filter)
{
	VDResamplerSwizzleTable(mFilterBank.data(), (uint32)mFilterBank.size() >> 1);
}

void VDResamplerSeparableTableRowStageSSE2::Process(void *dst, const void *src, uint32 w, uint32 u, uint32 dudx) {
	vdasm_resize_table_row_SSE2((uint32 *)dst, (const uint32 *)src, (const int *)mFilterBank.data(), (int)mFilterBank.size() >> 8, w, u, dudx);
}

VDResamplerSeparableTableColStageSSE2::VDResamplerSeparableTableColStageSSE2(const IVDResamplerFilter& filter)
	: VDResamplerColStageSeparableTable32(filter)
{
	VDResamplerSwizzleTable(mFilterBank.data(), (uint32)mFilterBank.size() >> 1);
}

void VDResamplerSeparableTableColStageSSE2::Process(void *dst, const void *const *src, uint32 w, sint32 phase) {
	const unsigned filtSize = (unsigned)mFilterBank.size() >> 8;

	vdasm_resize_table_col_SSE2((uint32*)dst, (const uint32 *const *)src, (const int *)mFilterBank.data() + filtSize*((phase >> 8) & 0xff), filtSize, w);
}

// VirtualDub - Video processing and capture application
// Internal filter library
//
// Copyright (C) 1998-2011 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDFILTERS_DSP_H
#define f_VD2_VDFILTERS_DSP_H

void VDDSPBlend8_LerpConst(void *dst, const void *src0, const void *src1, uint32 n16, uint8 factor);
void VDDSPBlend8_Min(void *dst, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_Max(void *dst, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_Add(void *dst, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_Multiply(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_LinearBurn(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_ColorBurn(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_Screen(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_ColorDodge(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_Overlay(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_SoftLight(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_HardLight(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_VividLight(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_LinearLight(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_PinLight(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_HardMix(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_Difference(void *dst0, const void *src0, const void *src1, uint32 n16);
void VDDSPBlend8_Exclusion(void *dst0, const void *src0, const void *src1, uint32 n16);

void VDDSPBlend8_Select(void *dst0, const void *src0, const void *src1, const void *srcm, uint32 n16);
void VDDSPBlend8_Lerp(void *dst0, const void *src0, const void *src1, const void *srcm, uint32 n16);

template<class T>
void VDDSPProcessPlane2(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, uint32 n16, uint32 h, const T& fn) {
	do {
		fn(dst, src, n16);
		dst = (char *)dst + dstpitch;
		src = (const char *)src + srcpitch;
	} while(--h);
}

template<class T>
void VDDSPProcessPlane3(void *dst, ptrdiff_t dstpitch, const void *src1, ptrdiff_t src1pitch, const void *src2, ptrdiff_t src2pitch, uint32 n16, uint32 h, const T& fn) {
	do {
		fn(dst, src1, src2, n16);

		dst = (char *)dst + dstpitch;
		src1 = (const char *)src1 + src1pitch;
		src2 = (const char *)src2 + src2pitch;
	} while(--h);
}

template<class T, class U>
void VDDSPProcessPlane3A(void *dst, ptrdiff_t dstpitch, const void *src1, ptrdiff_t src1pitch, const void *src2, ptrdiff_t src2pitch, uint32 n16, uint32 h, const T& fn, const U& arg) {
	do {
		fn(dst, src1, src2, n16, arg);

		dst = (char *)dst + dstpitch;
		src1 = (const char *)src1 + src1pitch;
		src2 = (const char *)src2 + src2pitch;
	} while(--h);
}

template<class T>
void VDDSPProcessPlane4(void *dst, ptrdiff_t dstpitch, const void *src1, ptrdiff_t src1pitch, const void *src2, ptrdiff_t src2pitch, const void *src3, ptrdiff_t src3pitch, uint32 n16, uint32 h, const T& fn) {
	do {
		fn(dst, src1, src2, src3, n16);

		dst = (char *)dst + dstpitch;
		src1 = (const char *)src1 + src1pitch;
		src2 = (const char *)src2 + src2pitch;
		src3 = (const char *)src3 + src3pitch;
	} while(--h);
}

#endif

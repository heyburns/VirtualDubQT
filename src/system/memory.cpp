// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2016 Anton Shekhovtsov
// Copyright (C) 2024 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <malloc.h>
#include <xmmintrin.h>
#include <vd2/system/atomic.h>

#include <vd2/system/memory.h>
#include <vd2/system/seh.h>
#include <vd2/system/cpuaccel.h>

void *VDAlignedMalloc(size_t n, unsigned alignment) {
	void *ptr = nullptr;
	if (posix_memalign(&ptr, alignment < sizeof(void*) ? sizeof(void*) : alignment, n) != 0) {
		return nullptr;
	}
	return ptr;
}

void VDAlignedFree(void *p) {
	free(p);
}

void *VDAlignedVirtualAlloc(size_t n) {
	void *ptr = nullptr;
	if (posix_memalign(&ptr, 4096, n) != 0) {
		return nullptr;
	}
	return ptr;
}

void VDAlignedVirtualFree(void *p) {
	free(p);
}

void VDSwapMemoryScalar(void *p0, void *p1, size_t bytes) {
	uint32 *dst0 = (uint32 *)p0;
	uint32 *dst1 = (uint32 *)p1;

	while(bytes >= 4) {
		uint32 a = *dst0;
		uint32 b = *dst1;

		*dst0++ = b;
		*dst1++ = a;

		bytes -= 4;
	}

	char *dstb0 = (char *)dst0;
	char *dstb1 = (char *)dst1;

	while(bytes--) {
		char a = *dstb0;
		char b = *dstb1;

		*dstb0++ = b;
		*dstb1++ = a;
	}
}

#if defined(VD_CPU_AMD64) || defined(VD_CPU_X86)
	void VDSwapMemorySSE(void *p0, void *p1, size_t bytes) {
		if (((uint32)(size_t)p0 | (uint32)(size_t)p1) & 15)
			return VDSwapMemoryScalar(p0, p1, bytes);

		__m128 *pv0 = (__m128 *)p0;
		__m128 *pv1 = (__m128 *)p1;

		size_t veccount = bytes >> 4;
		if (veccount) {
			do {
				__m128 v0 = *pv0;
				__m128 v1 = *pv1;

				*pv0++ = v1;
				*pv1++ = v0;
			} while(--veccount);
		}

		uint32 left = bytes & 15;
		if (left) {
			uint8 *pb0 = (uint8 *)pv0;
			uint8 *pb1 = (uint8 *)pv1;
			do {
				uint8 b0 = *pb0;
				uint8 b1 = *pb1;

				*pb0++ = b1;
				*pb1++ = b0;
			} while(--left);
		}
	}
#endif

void (__cdecl *VDSwapMemory)(void *p0, void *p1, size_t bytes) = VDSwapMemoryScalar;

void VDInvertMemory(void *p, unsigned bytes) {
	char *dst = (char *)p;

	if (!bytes)
		return;

	while((size_t)dst & 3) {
		*dst = ~*dst;
		++dst;

		if (!--bytes)
			return;
	}

	unsigned lcount = bytes >> 2;

	if (lcount)
		do {
			*(long *)dst = ~*(long *)dst;
			dst += 4;
		} while(--lcount);

	bytes &= 3;

	while(bytes--) {
		*dst = ~*dst;
		++dst;
	}
}

namespace {
	uintptr VDGetSystemPageSize() {
		return 4096;
	}
}

bool VDIsValidReadRegion(const void *p0, size_t bytes) {
	if (!bytes) return true;
	if (!p0) return false;
	return true;
}

bool VDIsValidWriteRegion(void *p0, size_t bytes) {
	if (!bytes) return true;
	if (!p0) return false;
	return true;
}

bool VDCompareRect(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, size_t w, size_t h) {
	if (!w || !h)
		return false;

	do {
		if (memcmp(dst, src, w))
			return true;

		dst = (char *)dst + dstpitch;
		src = (const char *)src + srcpitch;
	} while(--h);

	return false;
}

const void *VDMemCheck8(const void *src, uint8 value, size_t count) {
	if (count) {
		const uint8 *src8 = (const uint8 *)src;

		do {
			if (*src8 != value)
				return src8;

			++src8;
		} while(--count);
	}

	return NULL;
}

void VDMemset8(void *dst, uint8 value, size_t count) {
	if (count) {
		uint8 *dst2 = (uint8 *)dst;

		do {
			*dst2++ = value;
		} while(--count);
	}
}

void VDMemset16(void *dst, uint16 value, size_t count) {
	if (count) {
		uint16 *dst2 = (uint16 *)dst;

		do {
			*dst2++ = value;
		} while(--count);
	}
}

void VDMemset24(void *dst, uint32 value, size_t count) {
	if (count) {
		uint8 *dst2 = (uint8 *)dst;
		uint8 c0 = (uint8)value;
		uint8 c1 = (uint8)(value >> 8);
		uint8 c2 = (uint8)(value >> 16);

		do {
			*dst2++ = c0;
			*dst2++ = c1;
			*dst2++ = c2;
		} while(--count);
	}
}

void VDMemset32(void *dst, uint32 value, size_t count) {
	if (count) {
		uint32 *dst2 = (uint32 *)dst;

		do {
			*dst2++ = value;
		} while(--count);
	}
}

void VDMemset64(void *dst, uint64 value, size_t count) {
	if (count) {
		uint64 *dst2 = (uint64 *)dst;

		do {
			*dst2++ = value;
		} while(--count);
	}
}

void VDMemset128(void *dst, const void *src0, size_t count) {
	if (count) {
		const uint32 *src = (const uint32 *)src0;
		uint32 a0 = src[0];
		uint32 a1 = src[1];
		uint32 a2 = src[2];
		uint32 a3 = src[3];

		uint32 *dst2 = (uint32 *)dst;

		do {
			dst2[0] = a0;
			dst2[1] = a1;
			dst2[2] = a2;
			dst2[3] = a3;
			dst2 += 4;
		} while(--count);
	}
}

void VDMemsetPointer(void *dst, const void *value, size_t count) {
#if defined(VD_CPU_AMD64)
	VDMemset64(dst, (uint64)(size_t)value, count);
#else
	VDMemset32(dst, (uint32)(size_t)value, count);
#endif
}

void VDMemset8Rect(void *dst, ptrdiff_t pitch, uint8 value, size_t w, size_t h) {
	if (w>0 && h>0) {
		do {
			memset(dst, value, w);
			dst = (char *)dst + pitch;
		} while(--h);
	}
}

void VDMemset16Rect(void *dst, ptrdiff_t pitch, uint16 value, size_t w, size_t h) {
	if (w>0 && h>0) {
		do {
			VDMemset16(dst, value, w);
			dst = (char *)dst + pitch;
		} while(--h);
	}
}

void VDMemset24Rect(void *dst, ptrdiff_t pitch, uint32 value, size_t w, size_t h) {
	if (w>0 && h>0) {
		do {
			VDMemset24(dst, value, w);
			dst = (char *)dst + pitch;
		} while(--h);
	}
}

void VDMemset32Rect(void *dst, ptrdiff_t pitch, uint32 value, size_t w, size_t h) {
	if (w>0 && h>0) {
		do {
			VDMemset32(dst, value, w);
			dst = (char *)dst + pitch;
		} while(--h);
	}
}

void VDMemset64Rect(void *dst, ptrdiff_t pitch, uint64 value, size_t w, size_t h) {
	if (w>0 && h>0) {
		do {
			VDMemset64(dst, value, w);
			dst = (char *)dst + pitch;
		} while(--h);
	}
}

void VDFastMemcpyPartial(void *dst, const void *src, size_t bytes) {
	memcpy(dst, src, bytes);
}

void VDFastMemcpyFinish() {
}

void VDFastMemcpyAutodetect() {
}

void VDMemcpyRect(void *dst, ptrdiff_t dststride, const void *src, ptrdiff_t srcstride, size_t w, size_t h) {
	if (w <= 0 || h <= 0)
		return;

	if (w == (size_t)srcstride && w == (size_t)dststride)
		VDFastMemcpyPartial(dst, src, w*h);
	else {
		char *dst2 = (char *)dst;
		const char *src2 = (const char *)src;

		do {
			VDFastMemcpyPartial(dst2, src2, w);
			dst2 += dststride;
			src2 += srcstride;
		} while(--h);
	}
	VDFastMemcpyFinish();
}

bool VDMemcpyGuarded(void *dst, const void *src, size_t bytes) {
	memcpy(dst, src, bytes);
	return true;
}

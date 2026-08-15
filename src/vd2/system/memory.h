// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2016 Anton Shekhovtsov
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_MEMORY_H
#define f_VD2_SYSTEM_MEMORY_H

#include <vd2/system/vdtypes.h>

void *VDAlignedMalloc(size_t n, unsigned alignment);
void VDAlignedFree(void *p);

template<unsigned alignment>
struct VDAlignedObject {
	inline void *operator new(size_t n) { return VDAlignedMalloc(n, alignment); }
	inline void operator delete(void *p) { VDAlignedFree(p); }
};

void *VDAlignedVirtualAlloc(size_t n);
void VDAlignedVirtualFree(void *p);

extern void (__cdecl *VDSwapMemory)(void *p0, void *p1, size_t bytes);

void VDInvertMemory(void *p, unsigned bytes);

bool VDIsValidReadRegion(const void *p, size_t bytes);
bool VDIsValidWriteRegion(void *p, size_t bytes);

bool VDCompareRect(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, size_t w, size_t h);

const void *VDMemCheck8(const void *src, uint8 value, size_t count);

void VDMemset8(void *dst, uint8 value, size_t count);
void VDMemset16(void *dst, uint16 value, size_t count);
void VDMemset24(void *dst, uint32 value, size_t count);
void VDMemset32(void *dst, uint32 value, size_t count);
void VDMemset64(void *dst, uint64 value, size_t count);
void VDMemset128(void *dst, const void *value, size_t count);
void VDMemsetPointer(void *dst, const void *value, size_t count);

void VDMemset8Rect(void *dst, ptrdiff_t pitch, uint8 value, size_t w, size_t h);
void VDMemset16Rect(void *dst, ptrdiff_t pitch, uint16 value, size_t w, size_t h);
void VDMemset24Rect(void *dst, ptrdiff_t pitch, uint32 value, size_t w, size_t h);
void VDMemset32Rect(void *dst, ptrdiff_t pitch, uint32 value, size_t w, size_t h);
void VDMemset64Rect(void *dst, ptrdiff_t pitch, uint64 value, size_t w, size_t h);

#if defined(_WIN32) && defined(VD_CPU_X86) && defined(VD_COMPILER_MSVC)
	extern void (__cdecl *VDFastMemcpyPartial)(void *dst, const void *src, size_t bytes);
	extern void (__cdecl *VDFastMemcpyFinish)();
	void VDFastMemcpyAutodetect();
#else
	void VDFastMemcpyPartial(void *dst, const void *src, size_t bytes);
	void VDFastMemcpyFinish();
	void VDFastMemcpyAutodetect();
#endif


void VDMemcpyRect(void *dst, ptrdiff_t dststride, const void *src, ptrdiff_t srcstride, size_t w, size_t h);

/// Copy a region of memory with an access violation guard; used in cases where a sporadic
/// AV is unavoidable (dynamic Direct3D VB under XP). The regions must not overlap.
bool VDMemcpyGuarded(void *dst, const void *src, size_t bytes);

#endif

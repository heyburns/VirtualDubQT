// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2017 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <wtypes.h>
#include <winnt.h>
#include <immintrin.h>
#include <vd2/system/cpuaccel.h>


#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
inline void vd_cpuid(int cpuinfo[4], int info_type) {
    __cpuid_count(info_type, 0, cpuinfo[0], cpuinfo[1], cpuinfo[2], cpuinfo[3]);
}
inline unsigned long long get_xcr0() {
    uint32_t eax, edx;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return ((unsigned long long)edx << 32) | eax;
}
#endif

static long g_lCPUExtensionsEnabled;
static long g_lCPUExtensionsAvailable;

extern "C" {
	bool FPU_enabled, MMX_enabled, SSE_enabled, ISSE_enabled, SSE2_enabled;
};

#define IS_BIT_SET(bitfield, bit) ((bitfield) & (1<<(bit)) ? true : false)

#if (!defined(VD_CPU_X86) && !defined(VD_CPU_AMD64))
long CPUCheckForExtensions() {
	return 0;
}
#else

#if defined(_MSC_VER)
inline void vd_cpuid(int cpuinfo[4], int info_type) {
	__cpuid(cpuinfo, info_type);
}

unsigned __int64 get_xcr0(){
	return _xgetbv(0);
}
#endif

long CPUCheckForExtensions() {
	long result = 0;
	int cpuinfo[4];

	vd_cpuid(cpuinfo, 1);
	if (IS_BIT_SET(cpuinfo[3], 0))
		result |= CPUF_SUPPORTS_FPU;
	if (IS_BIT_SET(cpuinfo[3], 23))
		result |= CPUF_SUPPORTS_MMX;
	if (IS_BIT_SET(cpuinfo[3], 25))
		result |= CPUF_SUPPORTS_SSE | CPUF_SUPPORTS_INTEGER_SSE;
	if (IS_BIT_SET(cpuinfo[3], 26))
		result |= CPUF_SUPPORTS_SSE2;
	if (IS_BIT_SET(cpuinfo[2], 0))
		result |= CPUF_SUPPORTS_SSE3;
	if (IS_BIT_SET(cpuinfo[2], 9))
		result |= CPUF_SUPPORTS_SSSE3;
	if (IS_BIT_SET(cpuinfo[2], 19))
		result |= CPUF_SUPPORTS_SSE41;
	if (IS_BIT_SET(cpuinfo[2], 20))
		result |= CPUF_SUPPORTS_SSE42;
	if (IS_BIT_SET(cpuinfo[2], 22))
		result |= CPUF_SUPPORTS_MOVBE;
	if (IS_BIT_SET(cpuinfo[2], 23))
		result |= CPUF_SUPPORTS_POPCNT;
	if (IS_BIT_SET(cpuinfo[2], 25))
		result |= CPUF_SUPPORTS_AES;
	if (IS_BIT_SET(cpuinfo[2], 29))
		result |= CPUF_SUPPORTS_F16C;
	// AVX
	bool xgetbv_supported = IS_BIT_SET(cpuinfo[2], 27);
	bool avx_supported = IS_BIT_SET(cpuinfo[2], 28);
	if (xgetbv_supported && avx_supported)
	{
		unsigned long long xgetbv0 = get_xcr0();
		if ((xgetbv0 & 0x6ull) == 0x6ull) {
			result |= CPUF_SUPPORTS_AVX;
			if (IS_BIT_SET(cpuinfo[2], 12))
				result |= CPUF_SUPPORTS_FMA3;
			vd_cpuid(cpuinfo, 7);
			if (IS_BIT_SET(cpuinfo[1], 5))
				result |= CPUF_SUPPORTS_AVX2;
		}
		if((xgetbv0 & (0x7ull << 5)) &&
			 (xgetbv0 & (0x3ull << 1))) {
			vd_cpuid(cpuinfo, 7);
			if (IS_BIT_SET(cpuinfo[1], 16))
				result |= CPUF_SUPPORTS_AVX512F;
			if (IS_BIT_SET(cpuinfo[1], 17))
				result |= CPUF_SUPPORTS_AVX512DQ;
			if (IS_BIT_SET(cpuinfo[1], 21))
				result |= CPUF_SUPPORTS_AVX512IFMA;
			if (IS_BIT_SET(cpuinfo[1], 26))
				result |= CPUF_SUPPORTS_AVX512PF;
			if (IS_BIT_SET(cpuinfo[1], 27))
				result |= CPUF_SUPPORTS_AVX512ER;
			if (IS_BIT_SET(cpuinfo[1], 28))
				result |= CPUF_SUPPORTS_AVX512CD;
			if (IS_BIT_SET(cpuinfo[1], 30))
				result |= CPUF_SUPPORTS_AVX512BW;
			if (IS_BIT_SET(cpuinfo[1], 31))
				result |= CPUF_SUPPORTS_AVX512VL;
			if (IS_BIT_SET(cpuinfo[2], 1))
				result |= CPUF_SUPPORTS_AVX512VBMI;
		}
	}

	// 3DNow!, 3DNow!, ISSE, FMA4
	vd_cpuid(cpuinfo, 0x80000000);
	if (cpuinfo[0] >= (int)0x80000001)
	{
		vd_cpuid(cpuinfo, 0x80000001);

		if (IS_BIT_SET(cpuinfo[3], 31))
			result |= CPUF_SUPPORTS_3DNOW;

		if (IS_BIT_SET(cpuinfo[3], 30))
			result |= CPUF_SUPPORTS_3DNOW_EXT;

		if (IS_BIT_SET(cpuinfo[3], 22))
			result |= CPUF_SUPPORTS_INTEGER_SSE;

		if (result & CPUF_SUPPORTS_AVX) {
			if (IS_BIT_SET(cpuinfo[2], 16))
				result |= CPUF_SUPPORTS_FMA4;
		}
	}

	return result;	
}
#endif

long CPUEnableExtensions(long lEnableFlags) {
	g_lCPUExtensionsEnabled = lEnableFlags;

	MMX_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_MMX);
	FPU_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_FPU);
	SSE_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_SSE);
	ISSE_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_INTEGER_SSE);
	SSE2_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_SSE2);

	return g_lCPUExtensionsEnabled;
}

long CPUGetAvailableExtensions() {
	return g_lCPUExtensionsAvailable;
}

long CPUGetEnabledExtensions() {
	return g_lCPUExtensionsEnabled;
}

void VDCPUCleanupExtensions() {
#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	_mm_sfence();
#endif
}

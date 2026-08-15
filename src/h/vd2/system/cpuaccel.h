// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2017 Anton Shekhovtsov
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VIRTUALDUB_CPUACCEL_H
#define f_VIRTUALDUB_CPUACCEL_H

#define CPUF_SUPPORTS_CPUID			(0x00000001L)
#define CPUF_SUPPORTS_FPU			(0x00000002L) //  386/486DX
#define CPUF_SUPPORTS_MMX			(0x00000004L) //  P55C, K6, PII
#define CPUF_SUPPORTS_INTEGER_SSE	(0x00000008L) //  PIII, Athlon
#define CPUF_SUPPORTS_SSE			(0x00000010L) //  PIII, Athlon XP/MP
#define CPUF_SUPPORTS_SSE2			(0x00000020L) //  PIV, K8
#define CPUF_SUPPORTS_3DNOW			(0x00000040L) //  K6-2
#define CPUF_SUPPORTS_3DNOW_EXT		(0x00000080L) //  Athlon

#define CPUF_SUPPORTS_SSE3			(0x00000100L) //  PIV+, K8 Venice
#define CPUF_SUPPORTS_SSSE3			(0x00000200L) //  Core 2
#define CPUF_SUPPORTS_SSE41			(0x00000400L) //  Penryn, Wolfdale, Yorkfield 
#define CPUF_SUPPORTS_AVX			(0x00000800L) //  Sandy Bridge, Bulldozer
#define CPUF_SUPPORTS_SSE42			(0x00001000L) //  Nehalem

// VirtualDubFilterMod specific, identical to AVS+
#define CPUF_SUPPORTS_AVX2			(0x00002000L) //  Haswell
#define CPUF_SUPPORTS_FMA3			(0x00004000L)
#define CPUF_SUPPORTS_F16C			(0x00008000L)
#define CPUF_SUPPORTS_MOVBE			(0x00010000L) // Big Endian move
#define CPUF_SUPPORTS_POPCNT		(0x00020000L)
#define CPUF_SUPPORTS_AES			(0x00040000L)
#define CPUF_SUPPORTS_FMA4			(0x00080000L)

#define CPUF_SUPPORTS_AVX512F		(0x00100000L) // AVX-512 Foundation.
#define CPUF_SUPPORTS_AVX512DQ		(0x00200000L) // AVX-512 DQ (Double/Quad granular) Instructions
#define CPUF_SUPPORTS_AVX512PF		(0x00400000L) // AVX-512 Prefetch
#define CPUF_SUPPORTS_AVX512ER		(0x00800000L) // AVX-512 Exponential and Reciprocal
#define CPUF_SUPPORTS_AVX512CD		(0x01000000L) // AVX-512 Conflict Detection
#define CPUF_SUPPORTS_AVX512BW		(0x02000000L) // AVX-512 BW (Byte/Word granular) Instructions
#define CPUF_SUPPORTS_AVX512VL		(0x04000000L) // AVX-512 VL (128/256 Vector Length) Extensions
#define CPUF_SUPPORTS_AVX512IFMA	(0x08000000L) // AVX-512 IFMA integer 52 bit
#define CPUF_SUPPORTS_AVX512VBMI	(0x10000000L) // AVX-512 VBMI

#define CPUF_SUPPORTS_MASK			(0x1FFFFFFFFL)


long CPUCheckForExtensions();
long CPUEnableExtensions(long lEnableFlags);
long CPUGetEnabledExtensions();
void VDCPUCleanupExtensions();

extern "C" bool FPU_enabled, MMX_enabled, SSE_enabled, ISSE_enabled, SSE2_enabled;

#endif

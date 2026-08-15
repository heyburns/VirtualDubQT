// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2016-2019 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_PREFS_H
#define f_PREFS_H

///////////////////////////////////////////////////////////////////////////

enum {
	// The order of these must match the flags in cpuaccel.h!!
	OPTF_FORCE       = 0x01,
	OPTF_FPU         = 0x02,
	OPTF_MMX         = 0x04,
	OPTF_INTEGER_SSE = 0x08,  // Athlon MMX extensions or Intel SSE
	OPTF_SSE         = 0x10,  // Full SSE (PIII)
	OPTF_SSE2        = 0x20,  // (PIV)
	OPTF_3DNOW       = 0x40,
	OPTF_3DNOW_EXT   = 0x80,  // Athlon 3DNow! extensions
	OPTF_SSE3        = 0x100,
	OPTF_SSSE3       = 0x200,
	OPTF_SSE4_1      = 0x400,
	OPTF_AVX         = 0x800,
};

enum {
	pref_time_hmst,
	pref_time_hmst_r,
	pref_time_m,
	pref_time_m_r,
	pref_time_s,
	pref_time_s_r,
	pref_time_ms,
	pref_time_ms_r,
	pref_time_r100,
};

enum {
	PREFERENCES_DISPLAY = 1,
	PREFERENCES_OPTF = 2,
	PREFERENCES_TIMELINE = 4,
};

///////////////////////////////////////////////////////////////////////////

void LoadPreferences();

#endif

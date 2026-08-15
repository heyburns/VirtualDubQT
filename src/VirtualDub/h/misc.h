// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2018 Anton Shekhovtsov
// Copyright (C) 2025-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VIRTUALDUB_MISC_H
#define f_VIRTUALDUB_MISC_H

#ifndef FCC
#define FCC(ch4) ((((DWORD)(ch4) & 0xFF) << 24) |     \
                  (((DWORD)(ch4) & 0xFF00) << 8) |    \
                  (((DWORD)(ch4) & 0xFF0000) >> 8) |  \
                  (((DWORD)(ch4) & 0xFF000000) >> 24))
#endif

long MulDivTrunc(long a, long b, long c);
int NearestLongValue(long v, const long *array, int array_size);
unsigned __stdcall MulDivUnsigned(unsigned a, unsigned b, unsigned c);

// only works properly when d1,d2>0!!

long inline int64divto32(__int64 d1, __int64 d2) {
	return d2?(long)((d1+d2/2)/d2):0;
}

__int64 inline int64divround(__int64 d1, __int64 d2) {
	return d2?((d1+d2/2)/d2):0;
}

__int64 inline int64divroundup(__int64 d1, __int64 d2) {
	return d2?((d1+d2-1)/d2):0;
}

bool isEqualFOURCC(FOURCC fccA, FOURCC fccB);
bool isValidFOURCC(FOURCC fcc);
FOURCC toupperFOURCC(FOURCC fcc);

#endif

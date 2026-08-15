// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/hash.h>
#include <vd2/system/binary.h>

// Based on: SuperFastHash by Paul Hsieh
//		     http://www.azillionmonkeys.com/qed/hash.html

uint32 VDHashString32(const char *s) {
	uint32 len = (uint32)strlen(s);

	return VDHashString32(s, len);
}

uint32 VDHashString32(const char *s, uint32 len) {
	uint32 hash = len;

	uint32 rem = len & 3;
	len >>= 2;

	uint32 tmp;
	for(uint32 i=0; i<len; ++i) {
		hash += VDReadUnalignedU16(s);
		tmp = (VDReadUnalignedU16(s + 2) << 11) ^ hash;
		hash = (hash << 16) ^ tmp;
		s += 4;
		hash += hash >> 11;
	}

	switch(rem) {
		case 3:
			hash += VDReadUnalignedU16(s);
			hash ^= hash << 16;
			hash ^= (uint32)(uint8)s[2] << 18;
			hash += hash >> 11;
			break;
		case 2:
			hash += VDReadUnalignedU16(s);
			hash ^= hash << 11;
			hash += hash >> 17;
			break;
		case 1:
			hash += (uint8)s[0];
			hash ^= hash << 10;
			hash += hash >> 1;
			break;
	}

	hash ^= hash << 3;
	hash += hash >> 5;
	hash ^= hash << 4;
	hash += hash >> 17;
	hash ^= hash << 25;
	hash += hash >> 6;

	return hash;
}

uint32 VDHashString32(const wchar_t *s) {
	return VDHashString32((const char *)s, wcslen(s) * sizeof(wchar_t));
}

uint32 VDHashString32(const wchar_t *s, uint32 len) {
	return VDHashString32((const char *)s, len * sizeof(wchar_t));
}

uint32 VDHashString32I(const char *s) {
	uint32 len = (uint32)strlen(s);

	return VDHashString32I(s, len);
}

uint32 VDHashString32I(const char *s, uint32 len) {
	uint32 hash = 2166136261U;

	for(uint32 i=0; i<len; ++i) {
		hash *= 16777619;
		hash ^= (uint32)tolower((unsigned char)*s++);
	}

	return hash;
}

uint32 VDHashString32I(const wchar_t *s) {
	uint32 len = (uint32)wcslen(s);

	return VDHashString32I(s, len);
}

uint32 VDHashString32I(const wchar_t *s, uint32 len) {
	uint32 hash = 2166136261U;

	for(uint32 i=0; i<len; ++i) {
		hash *= 16777619;
		hash ^= (uint32)towlower(*s++);
	}

	return hash;
}

// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//
#ifndef f_VD2_SYSTEM_STRUTIL_H
#define f_VD2_SYSTEM_STRUTIL_H

#include <string.h>
#include <vd2/system/vdtypes.h>

char *strncpyz(char *strDest, const char *strSource, size_t count);
wchar_t *wcsncpyz(wchar_t *strDest, const wchar_t *strSource, size_t count);
const char *strskipspace(const char *s);

inline char *strskipspace(char *s) {
	return const_cast<char *>(strskipspace(s));
}

size_t vdstrlcpy(char *dst, const char *src, size_t sizeChars);
size_t vdwcslcpy(wchar_t *dst, const wchar_t *src, size_t sizeChars);

size_t vdstrlcat(char *dst, const char *src, size_t sizeChars);

inline int vdstricmp(const char *s, const char *t) {
	return _stricmp(s, t);
}

inline int vdstricmp(const char *s, const char *t, size_t maxlen) {
	return _strnicmp(s, t, maxlen);
}

inline int vdwcsicmp(const wchar_t *s, const wchar_t *t) {
	return _wcsicmp(s, t);
}

inline int vdwcsnicmp(const wchar_t *s, const wchar_t *t, size_t maxlen) {
	return _wcsnicmp(s, t, maxlen);
}

#endif

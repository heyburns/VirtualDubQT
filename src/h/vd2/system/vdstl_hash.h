// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2010 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_VDSTL_HASH_H
#define f_VD2_SYSTEM_VDSTL_HASH_H

#include <vd2/system/vdtypes.h>

///////////////////////////////////////////////////////////////////////////////
//	vdhash
//
// Differences from TR1:
//
// - We omit the hash for long double, as that's not really useful, esp. on
//   Windows.

template<class T> struct vdhash;

#define VDSTL_DECLARE_STANDARD_HASH(T)	\
	template<> struct vdhash<T> {	\
		size_t operator()(T val) const { return (size_t)val; }	\
	};

VDSTL_DECLARE_STANDARD_HASH(char);
VDSTL_DECLARE_STANDARD_HASH(signed char);
VDSTL_DECLARE_STANDARD_HASH(unsigned char);
VDSTL_DECLARE_STANDARD_HASH(wchar_t);
VDSTL_DECLARE_STANDARD_HASH(short);
VDSTL_DECLARE_STANDARD_HASH(unsigned short);
VDSTL_DECLARE_STANDARD_HASH(int);
VDSTL_DECLARE_STANDARD_HASH(unsigned int);
VDSTL_DECLARE_STANDARD_HASH(long);
VDSTL_DECLARE_STANDARD_HASH(unsigned long);
VDSTL_DECLARE_STANDARD_HASH(long long);
VDSTL_DECLARE_STANDARD_HASH(unsigned long long);

template<> struct vdhash<float> {
	size_t operator()(float v) const {
		const union { float f; sint32 i; } conv = {v};

		uint32 i = conv.i;

		// Denormals and infinities are unique encodings and work as-is. NaNs work
		// because they never compare equal to anything else, so their hash value
		// can be arbitrary. Zero and negative zero, however, compare equal.
		if (i == 0x80000000)
			i = 0;

		return i;
	}
};

template<> struct vdhash<double> {
	size_t operator()(double v) const {
		const union conv { double f; sint64 i; } conv = {v};

		uint64 i = conv.i;

		// Denormals and infinities are unique encodings and work as-is. NaNs work
		// because they never compare equal to anything else, so their hash value
		// can be arbitrary. Zero and negative zero, however, compare equal.
		if (i == 0x8000000000000000ULL)
			i = 0;

		if (sizeof(size_t) < 8)
			return (size_t)((i >> 32) ^ i);
		else
			return (size_t)i;
	}
};

#undef VDSTL_DECLARE_STANDARD_HASH

class VDStringA;
class VDStringSpanA;
class VDStringW;
class VDStringSpanW;

template<> struct vdhash<VDStringA> {
	size_t operator()(const VDStringA& s) const;
	size_t operator()(const char *s) const;
};

template<> struct vdhash<VDStringW> {
	size_t operator()(const VDStringW& s) const;
	size_t operator()(const wchar_t *s) const;
};

template<class T> struct vdhash<T *> {
	size_t operator()(T *val) const { return (size_t)val; }
};

struct vdstringhashi {
	size_t operator()(const VDStringA& s) const;
	size_t operator()(const char *s) const;
	size_t operator()(const VDStringW& s) const;
	size_t operator()(const wchar_t *s) const;
};

struct vdstringpred {
	bool operator()(const VDStringA& s, const VDStringA& t) const;
	bool operator()(const VDStringA& s, const VDStringSpanA& t) const;
	bool operator()(const VDStringA& s, const char *t) const;
	bool operator()(const VDStringW& s, const VDStringW& t) const;
	bool operator()(const VDStringW& s, const VDStringSpanW& t) const;
	bool operator()(const VDStringW& s, const wchar_t *t) const;
};

struct vdstringpredi {
	bool operator()(const VDStringA& s, const VDStringA& t) const;
	bool operator()(const VDStringA& s, const VDStringSpanA& t) const;
	bool operator()(const VDStringA& s, const char *t) const;
	bool operator()(const VDStringW& s, const VDStringW& t) const;
	bool operator()(const VDStringW& s, const VDStringSpanW& t) const;
	bool operator()(const VDStringW& s, const wchar_t *t) const;
};

#endif	// f_VD2_SYSTEM_VDSTL_HASH_H

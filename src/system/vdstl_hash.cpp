// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2010 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#include <stdafx.h>
#include <vd2/system/hash.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl_hash.h>

size_t vdhash<VDStringA>::operator()(const VDStringA& s) const {
	return VDHashString32(s.data(), s.length());
}

size_t vdhash<VDStringA>::operator()(const char *s) const {
	return VDHashString32(s, strlen(s));
}

size_t vdhash<VDStringW>::operator()(const VDStringW& s) const {
	return VDHashString32(s.data(), s.length());
}

size_t vdhash<VDStringW>::operator()(const wchar_t *s) const {
	return VDHashString32(s, wcslen(s));
}

size_t vdstringhashi::operator()(const VDStringA& s) const {
	return VDHashString32I(s.data(), s.length());
}

size_t vdstringhashi::operator()(const char *s) const {
	return VDHashString32I(s);
}

size_t vdstringhashi::operator()(const VDStringW& s) const {
	return VDHashString32I(s.data(), s.length());
}

size_t vdstringhashi::operator()(const wchar_t *s) const {
	return VDHashString32I(s);
}

bool vdstringpred::operator()(const VDStringA& s, const VDStringA& t) const {
	return s == t;
}

bool vdstringpred::operator()(const VDStringA& s, const VDStringSpanA& t) const {
	return s == t;
}

bool vdstringpred::operator()(const VDStringA& s, const char *t) const {
	return s == t;
}

bool vdstringpred::operator()(const VDStringW& s, const VDStringW& t) const {
	return s == t;
}

bool vdstringpred::operator()(const VDStringW& s, const VDStringSpanW& t) const {
	return s == t;
}

bool vdstringpred::operator()(const VDStringW& s, const wchar_t *t) const {
	return s == t;
}

bool vdstringpredi::operator()(const VDStringA& s, const VDStringA& t) const {
	return s.comparei(t) == 0;
}

bool vdstringpredi::operator()(const VDStringA& s, const VDStringSpanA& t) const {
	return s.comparei(t) == 0;
}

bool vdstringpredi::operator()(const VDStringA& s, const char *t) const {
	return s.comparei(t) == 0;
}

bool vdstringpredi::operator()(const VDStringW& s, const VDStringW& t) const {
	return s.comparei(t) == 0;
}

bool vdstringpredi::operator()(const VDStringW& s, const VDStringSpanW& t) const {
	return s.comparei(t) == 0;
}

bool vdstringpredi::operator()(const VDStringW& s, const wchar_t *t) const {
	return s.comparei(t) == 0;
}

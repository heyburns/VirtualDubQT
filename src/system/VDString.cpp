// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2024-2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstdc.h>

const VDStringSpanA::value_type VDStringSpanA::sNull[1] = {0};

void VDStringA::push_back_extend() {
	VDASSERT(mpEOS == mpEnd);
	size_type current_size = (size_type)(mpEnd - mpBegin);

	reserve_slow(current_size * 2 + 1, current_size);
}

void VDStringA::resize_slow(size_type n, size_type current_size) {
	resize_slow(n, current_size, 0);
}

void VDStringA::resize_slow(size_type n, size_type current_size, value_type c) {
	VDASSERT(n > current_size);

	size_type current_capacity = (size_type)(mpEOS - mpBegin);
	if (n > current_capacity)
		reserve_slow(n, current_capacity);

	memset(mpBegin + current_size, c, n - current_size);
	mpEnd = mpBegin + n;
	*mpEnd = 0;
}

void VDStringA::reserve_slow(size_type n, size_type current_capacity) {
	VDASSERT(n > current_capacity);

	size_type current_size = (size_type)(mpEnd - mpBegin);
	value_type *s = new value_type[n + 1];
	memcpy(s, mpBegin, (current_size + 1) * sizeof(value_type));
	if (mpBegin != sNull)
		delete[] mpBegin;

	mpBegin = s;
	mpEnd = s + current_size;
	mpEOS = s + n;
}

void VDStringA::reserve_amortized_slow(size_type n, size_type current_size, size_type current_capacity) {
	n += current_size;

	size_type doublesize = current_size * 2;
	if (n < doublesize)
		n = doublesize;

	reserve_slow(n, current_capacity);
}

VDStringA& VDStringA::sprintf(const value_type *format, ...) {
	clear();
	va_list val;
	va_start(val, format);
	append_vsprintf(format, val);
	va_end(val);
	return *this;
}

VDStringA& VDStringA::append_sprintf(const value_type *format, ...) {
	va_list val;
	va_start(val, format);
	append_vsprintf(format, val);
	va_end(val);
	return *this;
}

VDStringA& VDStringA::append_vsprintf(const value_type *format, va_list val) {
	char buf[2048];

	int len = _vsnprintf_s(buf, std::size(buf) - 1, format, val);
	if (len >= 0) {
		append(buf, buf + len);
	} else {
		len = _vscprintf(format, val);
		if (len >= 0) {
			vdfastvector<char> tmp(len + 1);
			len = _vsnprintf_s(tmp.data(), tmp.size(), len, format, val);
			if (len >= 0) {
				append(tmp.data(), tmp.data() + len);
			}
		}
	}

	return *this;
}

void VDStringA::move_from(VDStringA& src) {
	if (mpBegin != sNull)
		delete[] mpBegin;

	mpBegin = src.mpBegin;
	mpEnd = src.mpEnd;
	mpEOS = src.mpEOS;

	src.mpBegin = NULL;
	src.mpEnd = NULL;
	src.mpEOS = NULL;
}

///////////////////////////////////////////////////////////////////////////////

const VDStringSpanW::value_type VDStringSpanW::sNull[1] = {0};

void VDStringW::push_back_extend() {
	VDASSERT(mpEOS == mpEnd);
	size_type current_size = (size_type)(mpEnd - mpBegin);

	reserve_slow(current_size * 2 + 1, current_size);
}

void VDStringW::resize_slow(size_type n, size_type current_size) {
	VDASSERT(n > current_size);

	size_type current_capacity = (size_type)(mpEOS - mpBegin);
	if (n > current_capacity)
		reserve_slow(n, current_capacity);

	mpEnd = mpBegin + n;
	*mpEnd = 0;
}

void VDStringW::reserve_slow(size_type n, size_type current_capacity) {
	VDASSERT(current_capacity == (size_type)(mpEOS - mpBegin));
	VDASSERT(n > current_capacity);

	size_type current_size = (size_type)(mpEnd - mpBegin);
	value_type *s = new value_type[n + 1];
	memcpy(s, mpBegin, (current_size + 1) * sizeof(value_type));
	if (mpBegin != sNull)
		delete[] mpBegin;

	mpBegin = s;
	mpEnd = s + current_size;
	mpEOS = s + n;
}

void VDStringW::reserve_amortized_slow(size_type n, size_type current_size, size_type current_capacity) {
	n += current_size;

	size_type doublesize = current_size * 2;
	if (n < doublesize)
		n = doublesize;

	reserve_slow(n, current_capacity);
}

VDStringW& VDStringW::sprintf(const value_type *format, ...) {
	clear();
	va_list val;
	va_start(val, format);
	append_vsprintf(format, val);
	va_end(val);
	return *this;
}

VDStringW& VDStringW::append_sprintf(const value_type *format, ...) {
	va_list val;
	va_start(val, format);
	append_vsprintf(format, val);
	va_end(val);
	return *this;
}

VDStringW& VDStringW::append_vsprintf(const value_type *format, va_list val) {
	wchar_t buf[1024];

	int len = _vsnwprintf_s(buf, std::size(buf) - 1, format, val);
	if (len >= 0) {
		append(buf, buf + len);
	}
	else {
		len = _vscwprintf(format, val);
		if (len >= 0) {
			vdfastvector<wchar_t> tmp(len + 1);
			len = _vsnwprintf_s(tmp.data(), tmp.size(), len, format, val);
			if (len >= 0) {
				append(tmp.data(), tmp.data() + len);
			}
		}
	}

	return *this;
}

void VDStringW::move_from(VDStringW& src) {
	if (mpBegin != sNull)
		delete[] mpBegin;

	mpBegin = src.mpBegin;
	mpEnd = src.mpEnd;
	mpEOS = src.mpEOS;

	src.mpBegin = NULL;
	src.mpEnd = NULL;
	src.mpEOS = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

template<>
VDStringA *vdmove_forward(VDStringA *src1, VDStringA *src2, VDStringA *dst) {
	VDStringA *p = src1;
	while(p != src2) {
		dst->move_from(*p);
		++dst;
		++p;
	}

	return dst;
}

template<>
VDStringW *vdmove_forward(VDStringW *src1, VDStringW *src2, VDStringW *dst) {
	VDStringW *p = src1;
	while(p != src2) {
		dst->move_from(*p);
		++dst;
		++p;
	}

	return dst;
}

template<>
VDStringA *vdmove_backward(VDStringA *src1, VDStringA *src2, VDStringA *dst) {
	VDStringA *p = src2;
	while(p != src1) {
		--dst;
		--p;
		dst->move_from(*p);
	}

	return dst;
}

template<>
VDStringW *vdmove_backward(VDStringW *src1, VDStringW *src2, VDStringW *dst) {
	VDStringW *p = src2;
	while(p != src1) {
		--dst;
		--p;
		dst->move_from(*p);
	}

	return dst;
}

template<>
void vdmove<VDStringA>(VDStringA& dst, VDStringA& src) {
	dst.move_from(src);
}

template<>
void vdmove<VDStringW>(VDStringW& dst, VDStringW& src) {
	dst.move_from(src);
}

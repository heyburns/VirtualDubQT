// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2016 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_FRACTION_H
#define f_VD2_SYSTEM_FRACTION_H

#include <vd2/system/vdtypes.h>

class VDFraction {
friend VDFraction operator*(unsigned long b, const VDFraction f);
friend VDFraction operator*(int b, const VDFraction f);
private:
	unsigned long	hi, lo;

	static VDFraction reduce(uint64 hi, uint64 lo);

public:
	VDFraction() = default;
	explicit VDFraction(int i) : hi(i), lo(1) {}
	explicit VDFraction(unsigned int i) : hi(i), lo(1) {}
	explicit VDFraction(unsigned long i) : hi(i), lo(1) { }
	explicit VDFraction(unsigned long i, unsigned long j) : hi(i), lo(j) {}
	explicit VDFraction(double d);

	bool	operator<(VDFraction b) const;
	bool	operator<=(VDFraction b) const;
	bool	operator>(VDFraction b) const;
	bool	operator>=(VDFraction b) const;
	bool	operator==(VDFraction b) const;
	bool	operator!=(VDFraction b) const;

	VDFraction operator+(VDFraction b) const;
	VDFraction operator-(VDFraction b) const;
	VDFraction operator*(VDFraction b) const;
	VDFraction operator/(VDFraction b) const;

	VDFraction operator+(unsigned long b) const;
	VDFraction operator-(unsigned long b) const;
	VDFraction operator*(unsigned long b) const;
	VDFraction operator/(unsigned long b) const;

	VDFraction& operator+=(VDFraction b);
	VDFraction& operator-=(VDFraction b);
	VDFraction& operator*=(VDFraction b);
	VDFraction& operator/=(VDFraction b);
	VDFraction& operator+=(unsigned long b);
	VDFraction& operator-=(unsigned long b);
	VDFraction& operator*=(unsigned long b);
	VDFraction& operator/=(unsigned long b);

	void	Assign(unsigned long n, unsigned long d) {
		hi = n;
		lo = d;
	}

	sint64 scale64t(sint64) const;
	sint64 scale64r(sint64) const;
	sint64 scale64u(sint64) const;
	sint64 scale64it(sint64) const;
	sint64 scale64ir(sint64) const;
	sint64 scale64iu(sint64) const;

	double asDouble() const;
	double AsInverseDouble() const;
	unsigned long asInt() const;
	unsigned long roundup32ul() const;

	unsigned long getHi() const { return hi; }
	unsigned long getLo() const { return lo; }

	VDFraction reduce() const { return reduce(hi, lo); }

	bool Parse(const char *s);

	static inline VDFraction reduce64(sint64 hi, sint64 lo) { return reduce(hi, lo); }
};

inline VDFraction operator*(unsigned long b, const VDFraction f) { return f*b; }

typedef VDFraction Fraction;

#endif

// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/vdstl.h>
#include <vd2/system/vectors.h>

bool VDSolveLinearEquation(double *src, int n, ptrdiff_t stride_elements, double *b, double tolerance) {
	vdfastvector<double *> array(n);
	double **m = &array[0];
	int i, j, k;

	for(i=0; i<n; ++i) {
		m[i] = src;
		src += stride_elements;
	}

	// factor U
	for(i=0; i<n; ++i) {
		int best = i;

		for(j=i+1; j<n; ++j) {
			if (fabs(m[best][i]) < fabs(m[j][i]))
				best = j;
		}

		std::swap(m[i], m[best]);
		std::swap(b[i], b[best]);

		if (fabs(m[i][i]) < tolerance)
			return false;

		double f = 1.0 / m[i][i];

		m[i][i] = 1.0;

		for(j=i+1; j<n; ++j)
			m[i][j] *= f;

		b[i] *= f;

		for(j=i+1; j<n; ++j) {
			b[j] -= b[i] * m[j][i];
			for(k=n-1; k>=i; --k)
				m[j][k] -= m[i][k] * m[j][i];
		}
	}

	// factor L
	for(i=n-1; i>=0; --i)
		for(j=i-1; j>=0; --j)
			b[j] -= b[i] * m[j][i];

	return true;
}

template<>
bool vdrect32::contains(const vdpoint32& pt) const {
	return ((uint32)pt.x - (uint32)left) < (uint32)right - (uint32)left
		&& ((uint32)pt.y - (uint32)top) < (uint32)bottom - (uint32)top;
}

// VirtualDub - Video processing and capture application
// Application helper library
//
// Copyright (C) 1998-2006 Avery Lee
// Copyright (C) 2017-2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDLIB_FFT_H
#define f_VD2_VDLIB_FFT_H

#include <vd2/system/vdtypes.h>

void VDMakePermuteTable(uint32 *dst0, unsigned bits);
void VDCreateRaisedCosineWindow(float *dst, int n);
void VDPermuteRevBitsComplex(float *v, unsigned bits, const uint32 *permuteTable);
void VDComputeComplexFFT_DIT(float *p, unsigned bits);
void VDComputeRealFFT(float *p, unsigned bits);
void VDComputeComplexFFT_DIF(float *p, unsigned bits);
void VDComputeRealIFFT(float *p, unsigned bits);
void VDComputeComplexFFT_Reference(float *out, float *in, unsigned bits, double sign = -1);

class VDRealFFT {
public:
	VDRealFFT();
	VDRealFFT(unsigned bits);
	~VDRealFFT();

	void Init(unsigned bits);
	void Shutdown();

	void ComputeRealFFT(float *p);
	void ComputeRealIFFT(float *p);

protected:
	unsigned mBits;
	uint32 *mpPermuteTable;
	float *mpWeightTable;
};

class VDRollingRealFFT {
public:
	VDRollingRealFFT();
	VDRollingRealFFT(unsigned bits);
	~VDRollingRealFFT();

	void Init(unsigned bits);
	void Shutdown();

	void Clear();
	void Advance(uint32 samples);
	void CopyIn8U(const uint8 *src, uint32 count, ptrdiff_t stride);
	void CopyIn16S(const sint16 *src, uint32 count, ptrdiff_t stride);
	void CopyInF(const float *src, uint32 count, ptrdiff_t stride);
	void CopyInZ(uint32 count);

	void Transform();

	float GetPower(int bin) const;

protected:
	uint32	mPoints;
	uint32	mBufferLevel;
	float *mpWindow;
	float *mpSampleBuffer;
	float *mpTempArea;
	VDRealFFT mFFT;
};

#endif

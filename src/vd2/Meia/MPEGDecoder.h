// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_MEIA_MPEGDECODER_H
#define f_VD2_MEIA_MPEGDECODER_H

#include <stddef.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>

struct VDMPEGPredictorSet;
struct VDMPEGConverterSet;
struct VDMPEGIDCTSet;

class VDINTERFACE IVDMPEGDecoder : public IVDRefCount {
public:
	enum {
		kError						= 1,		// it's just... error.
		kErrorNotInitialized		= 2,
		kErrorBadValue				= 4,
		kErrorBadFrameType			= 8,
		kErrorBadReference			= 16,
		kErrorBadMotionVector		= 32,
		kErrorTooManyCoefficients	= 64,
		kErrorTooManyMacroblocks	= 128,
		kErrorSourceOverrun			= 256
	};
	
	// decoding
	
	virtual bool Init(int width, int height)=0;
	virtual void Shutdown()=0;
	virtual void SetIntraQuantizers(const unsigned char *pMatrix)=0;
	virtual void SetNonintraQuantizers(const unsigned char *pMatrix)=0;
	virtual void SetPredictors(const VDMPEGPredictorSet *pPredictors)=0;
	virtual void SetConverters(const VDMPEGConverterSet *pConverters)=0;
	virtual void SetIDCTs(const VDMPEGIDCTSet *pIDCTs)=0;
	
	virtual int DecodeFrame(const void *src, long len, long frame, int dst, int fwd, int rev)=0;
	virtual long GetErrorState()=0;
	virtual int GetFrameBuffer(long frame)=0;
	virtual long GetFrameNumber(int buffer)=0;
	virtual void CopyFrameBuffer(int dst, int src, long newframe)=0;
	virtual void SwapFrameBuffers(int dst, int src)=0;
	virtual void ClearFrameBuffers()=0;

	// framebuffer access

	virtual const void *GetYBuffer(int buffer, ptrdiff_t& pitch)=0;
	virtual const void *GetCrBuffer(int buffer, ptrdiff_t& pitch)=0;
	virtual const void *GetCbBuffer(int buffer, ptrdiff_t& pitch)=0;

	// framebuffer conversion

	virtual bool DecodeUYVY(void *dst, ptrdiff_t pitch, int buffer)=0;
	virtual bool DecodeYUYV(void *dst, ptrdiff_t pitch, int buffer)=0;
	virtual bool DecodeYVYU(void *dst, ptrdiff_t pitch, int buffer)=0;
	virtual bool DecodeY41P(void *dst, ptrdiff_t pitch, int buffer)=0;
	virtual bool DecodeRGB15(void *dst, ptrdiff_t pitch, int buffer)=0;
	virtual bool DecodeRGB16(void *dst, ptrdiff_t pitch, int buffer)=0;
	virtual bool DecodeRGB24(void *dst, ptrdiff_t pitch, int buffer)=0;
	virtual bool DecodeRGB32(void *dst, ptrdiff_t pitch, int buffer)=0;
};

IVDMPEGDecoder *CreateVDMPEGDecoder();

#endif

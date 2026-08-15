// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAMEBUFFERACCEL_H
#define f_VD2_FILTERFRAMEBUFFERACCEL_H

#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include "FilterFrame.h"

class IVDTTexture2D;
class VDFilterAccelEngine;

///////////////////////////////////////////////////////////////////////////
//
//	VDFilterFrameBufferAccel
//
///////////////////////////////////////////////////////////////////////////

class VDFilterFrameBufferAccel : public VDFilterFrameBuffer
{
public:
	enum { kTypeID = 'fbxa' };

	VDFilterFrameBufferAccel();

	void *AsInterface(uint32 iid);

	void Init(VDFilterAccelEngine *accelEngine, uint32 width, uint32 height, uint32 borderWidth, uint32 borderHeight);
	void Shutdown();

	uint32 GetWidth() const { return mWidth; }
	uint32 GetHeight() const { return mHeight; }
	uint32 GetBorderWidth() const { return mBorderWidth; }
	uint32 GetBorderHeight() const { return mBorderHeight; }

	IVDTTexture2D *GetTexture() const { return mpTexture; }
	void SetTexture(IVDTTexture2D *tex);

	void *LockWrite();
	const void *LockRead() const;
	void Unlock();

	uint32 GetSize() const;

public:
	void Decommit();

protected:
	~VDFilterFrameBufferAccel();

	VDFilterAccelEngine *mpAccelEngine;
	IVDTTexture2D *mpTexture;
	uint32	mWidth;
	uint32	mHeight;
	uint32	mBorderWidth;
	uint32	mBorderHeight;
};

#endif	// f_VD2_FILTERFRAMEBUFFERACCEL_H

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAMEBUFFERMEMORY_H
#define f_VD2_FILTERFRAMEBUFFERMEMORY_H

#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include "FilterFrame.h"

///////////////////////////////////////////////////////////////////////////
//
//	VDFilterFrameBufferMemory
//
///////////////////////////////////////////////////////////////////////////

class VDFilterFrameBufferMemory	: public VDFilterFrameBuffer
{
public:
	VDFilterFrameBufferMemory();

	void Init(uint32 size);
	void Shutdown();

	void *LockWrite() { return mpBuffer; }
	const void *LockRead() const { return mpBuffer; }
	void Unlock() {}

	uint32 GetSize() const { return mBufferSize; }

protected:
	~VDFilterFrameBufferMemory();

	void *mpBuffer;
	uint32 mBufferSize;
	bool	mbVirtAlloc;
};

#endif	// f_VD2_FILTERFRAMEBUFFERMEMORY_H

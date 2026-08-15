// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAMEALLOCATOR_H
#define f_VD2_FILTERFRAMEALLOCATOR_H

#include <vd2/system/refcount.h>

class VDFilterFrameBuffer;

///////////////////////////////////////////////////////////////////////////
//
//	VDFilterFrameAllocator
//
///////////////////////////////////////////////////////////////////////////

class IVDFilterFrameAllocator : public IVDRefCount {
public:
	virtual void Trim() = 0;
	virtual bool Allocate(VDFilterFrameBuffer **buffer) = 0;

	virtual void OnFrameBufferIdle(VDFilterFrameBuffer *buf) = 0;
	virtual void OnFrameBufferActive(VDFilterFrameBuffer *buf) = 0;
};

#endif

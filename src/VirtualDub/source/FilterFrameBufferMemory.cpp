// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2017 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <vd2/system/error.h>
#include "FilterFrameBufferMemory.h"

namespace {
	const uint32 max_align = 64;
}

///////////////////////////////////////////////////////////////////////////

VDFilterFrameBufferMemory::VDFilterFrameBufferMemory()
	: mpBuffer(NULL)
	, mBufferSize(0)
	, mbVirtAlloc(false)
{
}

VDFilterFrameBufferMemory::~VDFilterFrameBufferMemory() {
	Shutdown();
}

void VDFilterFrameBufferMemory::Init(uint32 size) {
	VDASSERT(!mpBuffer);

	if (size >= 262144) {
		mbVirtAlloc = true;
		mpBuffer = VDAlignedVirtualAlloc(size);
	} else {
		mbVirtAlloc = false;
		mpBuffer = VDAlignedMalloc(size, max_align);
	}

	if (!mpBuffer)
		throw MyMemoryError(size);

	mBufferSize = size;
}

void VDFilterFrameBufferMemory::Shutdown() {
	VDFilterFrameBuffer::Shutdown();

	if (mpBuffer) {
		if (mbVirtAlloc)
			VDAlignedVirtualFree(mpBuffer);
		else
			VDAlignedFree(mpBuffer);
		mpBuffer = NULL;
		mBufferSize = 0;
	}
}

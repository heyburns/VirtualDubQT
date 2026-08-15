// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include "FilterFrameAllocatorProxy.h"
#include "FilterFrameAllocator.h"

VDFilterFrameAllocatorProxy::VDFilterFrameAllocatorProxy(const VDFilterFrameAllocatorProxy& src)
	: mSizeRequired(src.mSizeRequired)
	, mBorderWRequired(src.mBorderWRequired)
	, mBorderHRequired(src.mBorderHRequired)
	, mAccelMode(src.mAccelMode)
	, mpAllocator(src.mpAllocator)
	, mpLink(NULL)
{
	if (mpAllocator)
		mpAllocator->AddRef();
}

VDFilterFrameAllocatorProxy::~VDFilterFrameAllocatorProxy() {
	SetAllocator(NULL);
}

VDFilterFrameAllocatorProxy& VDFilterFrameAllocatorProxy::operator=(const VDFilterFrameAllocatorProxy& src) {
	if (&src != this) {
		SetAllocator(src.mpAllocator);
		mSizeRequired = src.mSizeRequired;
		mBorderWRequired = src.mBorderWRequired;
		mBorderHRequired = src.mBorderHRequired;
		mAccelMode = src.mAccelMode;
		mpLink = src.mpLink;
	}

	return *this;
}

void VDFilterFrameAllocatorProxy::Clear() {
	SetAllocator(NULL);
	mSizeRequired = 0;
	mBorderWRequired = 0;
	mBorderHRequired = 0;
	mAccelMode = kAccelModeNone;
	mpLink = NULL;
}

void VDFilterFrameAllocatorProxy::SetAllocator(IVDFilterFrameAllocator *alloc) {
	if (alloc != mpAllocator) {
		if (alloc)
			alloc->AddRef();
		if (mpAllocator)
			mpAllocator->Release();
		mpAllocator = alloc;
	}
}

void VDFilterFrameAllocatorProxy::TrimAllocator() {
	if (mpAllocator)
		mpAllocator->Trim();
}

bool VDFilterFrameAllocatorProxy::Allocate(VDFilterFrameBuffer **buffer) {
	return mpAllocator && mpAllocator->Allocate(buffer);
}

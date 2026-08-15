// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <vd2/system/error.h>
#include <vd2/Tessa/Context.h>
#include "FilterAccelEngine.h"
#include "FilterFrameBufferAccel.h"

///////////////////////////////////////////////////////////////////////////

VDFilterFrameBufferAccel::VDFilterFrameBufferAccel()
	: mpAccelEngine(NULL)
	, mpTexture(NULL)
	, mWidth(0)
	, mHeight(0)
	, mBorderWidth(0)
	, mBorderHeight(0)
{
}

VDFilterFrameBufferAccel::~VDFilterFrameBufferAccel() {
	Shutdown();
}

void *VDFilterFrameBufferAccel::AsInterface(uint32 iid) {
	if (iid == VDFilterFrameBufferAccel::kTypeID)
		return static_cast<VDFilterFrameBufferAccel *>(this);

	return VDFilterFrameBuffer::AsInterface(iid);
}

void VDFilterFrameBufferAccel::Init(VDFilterAccelEngine *accelEngine, uint32 width, uint32 height, uint32 borderWidth, uint32 borderHeight) {
	VDASSERT(width && height);
	mpAccelEngine = accelEngine;
	mWidth = width;
	mHeight = height;
	mBorderWidth = borderWidth;
	mBorderHeight = borderHeight;
}

void VDFilterFrameBufferAccel::Shutdown() {
	if (mpTexture)
		mpAccelEngine->DecommitBuffer(this);

	VDFilterFrameBuffer::Shutdown();
}

void VDFilterFrameBufferAccel::SetTexture(IVDTTexture2D *tex) {
	if (tex)
		tex->AddRef();

	if (mpTexture)
		mpTexture->Release();

	mpTexture = tex;
}

void *VDFilterFrameBufferAccel::LockWrite() {
	return NULL;
}

const void *VDFilterFrameBufferAccel::LockRead() const {
	return NULL;
}

void VDFilterFrameBufferAccel::Unlock() {
}

uint32 VDFilterFrameBufferAccel::GetSize() const {
	return 0;
}

void VDFilterFrameBufferAccel::Decommit() {
	if (mpTexture) {
		mpTexture->Release();
		mpTexture = NULL;
	}
}

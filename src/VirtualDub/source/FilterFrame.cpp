// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <vd2/system/error.h>
#include "FilterFrame.h"
#include "FilterFrameAllocator.h"
#include "FilterFrameCache.h"

///////////////////////////////////////////////////////////////////////////

VDFilterFrameBuffer::VDFilterFrameBuffer()
	: mpAllocator(NULL)
{
}

VDFilterFrameBuffer::~VDFilterFrameBuffer() {
}

int VDFilterFrameBuffer::AddRef() {
	int rc = vdrefcounted<IVDRefUnknown>::AddRef();
	if (rc == 2) {
		if (mpAllocator)
			mpAllocator->OnFrameBufferActive(this);
	}

	return rc;
}

int VDFilterFrameBuffer::Release() {
	int rc = vdrefcounted<IVDRefUnknown>::Release();
	if (rc == 1) {
		if (mpAllocator)
			mpAllocator->OnFrameBufferIdle(this);
	}

	return rc;
}

void *VDFilterFrameBuffer::AsInterface(uint32 iid) {
	return NULL;
}

void VDFilterFrameBuffer::Shutdown() {
	EvictFromCaches();
}

void VDFilterFrameBuffer::SetAllocator(IVDFilterFrameAllocator *allocator) {
	mpAllocator = allocator;
}

void VDFilterFrameBuffer::AddCacheReference(VDFilterFrameBufferCacheLinkNode *cacheLink) {
	mCaches.push_back(cacheLink);
}

void VDFilterFrameBuffer::RemoveCacheReference(VDFilterFrameBufferCacheLinkNode *cacheLink) {
	VDASSERT(GetCacheReference(cacheLink->mpCache) == cacheLink);
	mCaches.erase(cacheLink);
	cacheLink->mListNodePrev = NULL;
	cacheLink->mListNodeNext = NULL;
}

VDFilterFrameBufferCacheLinkNode *VDFilterFrameBuffer::GetCacheReference(VDFilterFrameCache *cache) {
	for(Caches::iterator it(mCaches.begin()), itEnd(mCaches.end()); it != itEnd; ++it) {
		VDFilterFrameBufferCacheLinkNode *linkNode = *it;

		if (linkNode->mpCache == cache)
			return linkNode;
	}

	return NULL;
}

bool VDFilterFrameBuffer::Steal(uint32 references) {
	if (mRefCount > 1 + (int)references)
		return false;

	EvictFromCaches();
	return true;
}

void VDFilterFrameBuffer::EvictFromCaches() {
	while(!mCaches.empty()) {
		VDFilterFrameBufferCacheLinkNode *linkNode = mCaches.front();

		linkNode->mpCache->Evict(linkNode);
	}
}

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2015 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAME_H
#define f_VD2_FILTERFRAME_H

#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>
#include <vd2/system/vdstl.h>
#include <vd2/plugin/vdplugin.h>

class IVDFilterFrameAllocator;
class VDFilterFrameCache;

struct VDFilterFrameBufferCacheLinkNode : public vdlist_node {
	VDFilterFrameCache *mpCache;
};

///////////////////////////////////////////////////////////////////////////
//
//	VDFilterFrameBuffer
//
///////////////////////////////////////////////////////////////////////////

struct VDFilterFrameBufferAllocatorNode : public vdlist_node {};

class VDFilterFrameBuffer	: public vdrefcounted<IVDRefUnknown>
							, public VDFilterFrameBufferAllocatorNode
{
	VDFilterFrameBuffer(const VDFilterFrameBuffer&);
	VDFilterFrameBuffer& operator=(const VDFilterFrameBuffer&);
public:
	FilterModPixmapInfo info;

	VDFilterFrameBuffer();

	int AddRef();
	int Release();
	void *AsInterface(uint32 iid);

	virtual void Shutdown();

	void SetAllocator(IVDFilterFrameAllocator *allocator);

	virtual void *LockWrite() = 0;
	virtual const void *LockRead() const = 0;
	virtual void Unlock() = 0;

	virtual uint32 GetSize() const = 0;

	void AddCacheReference(VDFilterFrameBufferCacheLinkNode *cacheLink);
	void RemoveCacheReference(VDFilterFrameBufferCacheLinkNode *cacheLink);
	VDFilterFrameBufferCacheLinkNode *GetCacheReference(VDFilterFrameCache *cache);

	bool Steal(uint32 references);
	void EvictFromCaches();

protected:
	virtual ~VDFilterFrameBuffer();

	IVDFilterFrameAllocator *mpAllocator;

	typedef vdlist<VDFilterFrameBufferCacheLinkNode> Caches;
	Caches mCaches;
};

#endif	// f_VD2_FILTERFRAME_H

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAMECACHE_H
#define f_VD2_FILTERFRAMECACHE_H

#include <vd2/system/vdstl.h>

#include "FilterFrame.h"

class VDFilterFrameBuffer;
class VDFilterFrameCache;

struct VDFilterFrameBufferCacheHashNode : public vdlist_node {
	VDFilterFrameBuffer *mpBuffer;
	sint64 mKey;
};

struct VDFilterFrameBufferCacheNode : public VDFilterFrameBufferCacheHashNode, public VDFilterFrameBufferCacheLinkNode {};

class VDFilterFrameCache {
	VDFilterFrameCache(const VDFilterFrameCache&);
	VDFilterFrameCache& operator=(const VDFilterFrameCache&);
public:
	VDFilterFrameCache();
	~VDFilterFrameCache();

	void Flush();

	void Add(VDFilterFrameBuffer *buf, sint64 key);
	void Remove(VDFilterFrameBuffer *buf);

	bool Lookup(sint64 key, VDFilterFrameBuffer **buffer);
	void Evict(VDFilterFrameBufferCacheLinkNode *cacheLink);

	void InvalidateAllFrames();

protected:
	VDFilterFrameBufferCacheNode *AllocateNode();
	void FreeNode(VDFilterFrameBufferCacheNode *node);

	enum { kBufferHashTableSize = 64 };
	typedef vdlist<VDFilterFrameBufferCacheHashNode> HashNodes;
	HashNodes mHashTable[kBufferHashTableSize];

	HashNodes mFreeNodes;
};

#endif	// f_VD2_FILTERFRAMECACHE_H

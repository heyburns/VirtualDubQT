// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAMEALLOCATORMANAGER_H
#define f_VD2_FILTERFRAMEALLOCATORMANAGER_H

#include <vd2/system/vdstl.h>

class IVDFilterFrameAllocator;
class VDFilterFrameAllocatorProxy;
class VDFilterAccelEngine;

class VDFilterFrameAllocatorManager {
	VDFilterFrameAllocatorManager(const VDFilterFrameAllocatorManager&);
	VDFilterFrameAllocatorManager& operator=(const VDFilterFrameAllocatorManager&);
public:
	VDFilterFrameAllocatorManager();
	~VDFilterFrameAllocatorManager();

	void Shutdown();

	void AddAllocatorProxy(VDFilterFrameAllocatorProxy *proxy);
	void AssignAllocators(VDFilterAccelEngine *accelEngine);

protected:
	void AssignAllocators(VDFilterAccelEngine *accelEngine, int mode);

	struct ProxyEntry {
		VDFilterFrameAllocatorProxy *mpProxy;
		IVDFilterFrameAllocator *mpAllocator;
		uint32 mMinSize;
		uint32 mMaxSize;
		uint32 mBorderWidth;
		uint32 mBorderHeight;
		ProxyEntry *mpParent;
	};

	struct ProxyEntrySort;

	typedef vdfastvector<ProxyEntry> Proxies;

	Proxies		mProxies[3];
};

#endif	// f_VD2_FILTERFRAMEALLOCATORMANAGER_H

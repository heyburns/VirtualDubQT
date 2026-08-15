// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2005 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/cache.h>

///////////////////////////////////////////////////////////////////////////

VDCache::VDCache(IVDCacheAllocator *pAllocator)
	: mpAllocator(pAllocator)
	, mObjectCount(0)
	, mObjectLimit(16)
{
}

VDCache::~VDCache() {
	Shutdown();
}

void VDCache::Shutdown() {
	for(int i=0; i<kVDCacheStateCount; ++i) {
		ObjectList& ol = mLists[i];

		while(!ol.empty()) {
			VDCachedObject *pObject = static_cast<VDCachedObject *>(ol.back());
			ol.pop_back();

			pObject->OnCacheEvict();
			pObject->SetCache(NULL);	// will release object

			if (i != kVDCacheStateFree) {
				VDASSERT((int)--mObjectCount >= 0);
			}
		}
	}
}

int VDCache::GetStateCount(int state) {
	vdsynchronized(mLock) {
		return mLists[state].size();
	}
	return 0;
}


void VDCache::DumpListStatus(int state) {
	vdsynchronized(mLock) {
		ObjectList& ol = mLists[state];

		for(ObjectList::iterator it(ol.begin()), itEnd(ol.end()); it!=itEnd; ++it) {
			VDCachedObject *pObj = static_cast<VDCachedObject *>(*it);

			pObj->DumpStatus();
		}
	}
}

VDCachedObject *VDCache::Allocate(sint64 key) {
	VDCachedObject *pObj = NULL;

	vdsynchronized(mLock) {
		if (mObjectCount >= mObjectLimit - 1)
			Evict(mObjectLimit - 1);

		ObjectList& fl = mLists[kVDCacheStateFree];
		ObjectList& pl = mLists[kVDCacheStatePending];

		if (fl.empty()) {
			VDCachedObject *pNewObject = mpAllocator->OnCacheAllocate();

			pNewObject->SetCache(this);
			pNewObject->SetState(kVDCacheStateFree);

			fl.push_front(pNewObject);
		}

		++mObjectCount;

		pObj = static_cast<VDCachedObject *>(fl.front());
		VDASSERT(pObj->GetState() == kVDCacheStateFree);
		pObj->AddRef();
		pObj->SetState(kVDCacheStatePending);
		pObj->mHashKey = key;
		pl.splice(pl.begin(), fl, fl.fast_find(pObj));
		mHash.insert(pObj);
	}

	return pObj;
}

VDCachedObject *VDCache::Create(sint64 key, bool& is_new) {
	// The pending, ready, active, and complete lists are eligible for lookup.
	// The free and aborted lists are not.

	VDCachedObject *pObj = NULL;

	is_new = false;

	vdsynchronized(mLock) {
		pObj = static_cast<VDCachedObject *>(mHash[key]);

		if (pObj) {
			pObj->AddRef();

			VDASSERT(pObj->GetState() != kVDCacheStateFree);

			if (pObj->GetState() == kVDCacheStateIdle) {
				pObj->SetState(kVDCacheStateComplete);

				ObjectList& il = mLists[kVDCacheStateIdle];
				ObjectList& cl = mLists[kVDCacheStateComplete];

				cl.splice(cl.begin(), il, il.fast_find(pObj));
			}
		}

		if (!pObj) {
			is_new = true;
			pObj = Allocate(key);
		}
	}

	return pObj;
}

void VDCache::Evict(uint32 level) {
	if (mObjectCount <= level)
		return;

	int maxfree = mObjectCount - level;

	ObjectList& il = mLists[kVDCacheStateIdle];
	ObjectList& al = mLists[kVDCacheStateAborting];

	while(maxfree-- > 0 && mObjectCount >= level && !il.empty()) {
		VDCachedObject *pObject = static_cast<VDCachedObject *>(il.back());
		VDASSERT(pObject->GetState() == kVDCacheStateIdle);
		
		pObject->SetState(kVDCacheStateAborting);
		al.splice(al.begin(), il, pObject);

		pObject->WeakAddRef();

		mLock.Unlock();

		pObject->OnCacheEvict();
		pObject->WeakRelease();			// Will move to free list.

		mLock.Lock();
	}
}

void VDCache::NotifyFree(VDCachedObject *pObject) {
	vdsynchronized(mLock) {
		int rc = pObject->GetRefCount();

		// This check is required because it is possible for a call to
		// Allocate() to sneak in before we acquire the lock.
		if (rc < 0x10000) {
			VDCacheState oldState = pObject->GetState();
			VDCacheState newState = oldState;

			if (rc & 0xfffe)
				newState = kVDCacheStateAborting;
			else if (pObject->IsValid())
				newState = kVDCacheStateIdle;
			else {
				VDVERIFY((int)--mObjectCount >= 0);
				newState = kVDCacheStateFree;
				mHash.erase(pObject);
			}

			if (newState != oldState) {
				pObject->SetState(newState);

				ObjectList& nl = mLists[newState];
				ObjectList& ol = mLists[oldState];
				nl.splice(nl.begin(), ol, ol.fast_find(pObject));
			}

			if (oldState == kVDCacheStatePending || oldState == kVDCacheStateReady)
				pObject->OnCacheAbortPending();
		}
	}
}

void VDCache::Schedule(VDCachedObject *pObject) {
	vdsynchronized(mLock) {
		VDCacheState oldState = pObject->GetState();

		VDASSERT(oldState == kVDCacheStatePending || oldState == kVDCacheStateActive);

		ObjectList& ol = mLists[oldState];
		ObjectList& nl = mLists[kVDCacheStateReady];

		nl.splice(nl.back(), ol, ol.fast_find(pObject));
		pObject->SetState(kVDCacheStateReady);
	}
}

VDCachedObject *VDCache::GetNextReady() {
	VDCachedObject *pObject = NULL;

	vdsynchronized(mLock) {
		ObjectList& rl = mLists[kVDCacheStateReady];
		ObjectList& al = mLists[kVDCacheStateActive];

		if (!rl.empty()) {
			pObject = static_cast<VDCachedObject *>(rl.front());
			VDASSERT(pObject->GetState() == kVDCacheStateReady);

			al.splice(al.end(), rl, rl.begin());

			pObject->SetState(kVDCacheStateActive);
			pObject->AddRef();
		}
	}

	return pObject;
}

void VDCache::MarkCompleted(VDCachedObject *pObject) {
	vdsynchronized(mLock) {
		VDCacheState oldState = pObject->GetState();
		VDASSERT(oldState == kVDCacheStatePending || oldState == kVDCacheStateActive);

		ObjectList& al = mLists[oldState];
		ObjectList& cl = mLists[kVDCacheStateComplete];

		if (!al.empty()) {
			cl.splice(cl.end(), al, al.fast_find(pObject));

			pObject = static_cast<VDCachedObject *>(cl.back());
			pObject->SetState(kVDCacheStateComplete);
		}
	}
}

///////////////////////////////////////////////////////////////////////////

VDCachedObject::VDCachedObject()
{
}

int VDCachedObject::AddRef() {
	int rv = (mRefCount += 0x10000);

	return rv >> 16;
}

int VDCachedObject::Release() {
	int rv = (mRefCount -= 0x10000);

	VDASSERT(rv >= 0);

	if (rv < 0x10000) {
		if (!rv)
			delete this;
		else if (mpCache)
			mpCache->NotifyFree(this);
	}

	return rv >> 16;
}

void VDCachedObject::WeakAddRef() {
	mRefCount += 2;
}

void VDCachedObject::WeakRelease() {
	int rv = (mRefCount -= 2);

	VDASSERT((rv & 0xffff) < 0x8000);

	if (rv < 2) {
		if (!rv)
			delete this;
		else
			mpCache->NotifyFree(this);
	}
}

void VDCachedObject::SetCache(VDCache *pCache) {
	mpCache = pCache;
	if (pCache)
		++mRefCount;
	else {
		if (!--mRefCount)
			delete this;
	}
}

///////////////////////////////////////////////////////////////////////////

VDPool::VDPool(IVDPoolAllocator *pAllocator)
	: mpAllocator(pAllocator)
	, mObjectCount(0)
	, mObjectLimit(16)
{
}

VDPool::~VDPool() {
	Shutdown();
}

void VDPool::Shutdown() {
	for(int i=0; i<kVDPoolStateCount; ++i) {
		ObjectList& ol = mLists[i];

		while(!ol.empty()) {
			VDPooledObject *pObject = static_cast<VDPooledObject *>(ol.back());
			ol.pop_back();

			pObject->SetPool(NULL);	// will release object

			VDASSERT((int)--mObjectCount >= 0);
		}
	}
}

VDPooledObject *VDPool::Allocate() {
	VDPooledObject *pObj = NULL;

	vdsynchronized(mLock) {
		ObjectList& fl = mLists[kVDPoolStateFree];
		ObjectList& pl = mLists[kVDPoolStateActive];

		if (fl.empty()) {
			VDPooledObject *pNewObject = mpAllocator->OnPoolAllocate();

			pNewObject->SetPool(this);

			fl.push_front(pNewObject);
			++mObjectCount;
		}

		pObj = static_cast<VDPooledObject *>(fl.front());
		pObj->AddRef();
		pl.splice(pl.begin(), fl, fl.fast_find(pObj));
	}

	return pObj;
}

void VDPool::NotifyFree(VDPooledObject *pObject) {
	vdsynchronized(mLock) {
		// This check is required because it is possible for a call to
		// Allocate() to sneak in before we acquire the lock.

		if (pObject->GetRefCount() < 2) {
			VDPoolState oldState = kVDPoolStateActive;
			VDPoolState newState = kVDPoolStateFree;

			mLists[kVDPoolStateActive].erase(pObject);

			if (mObjectCount > mObjectLimit) {
				delete pObject;
				--mObjectCount;
			} else
				mLists[kVDPoolStateFree].push_back(pObject);
		}
	}
}

///////////////////////////////////////////////////////////////////////////

VDPooledObject::VDPooledObject()
	: mRefCount(0)
	, mpPool(NULL)
{
}

int VDPooledObject::AddRef() {
	return (mRefCount += 2) >> 1;
}

int VDPooledObject::Release() {
	int rv = (mRefCount -= 2);

	VDASSERT(rv >= 0);

	if (rv < 2) {
		if (!rv)
			delete this;
		else if (mpPool)
			mpPool->NotifyFree(this);
	}

	return rv >> 1;
}

void VDPooledObject::SetPool(VDPool *pPool) {
	mpPool = pPool;
	if (pPool)
		++mRefCount;
	else {
		if (!--mRefCount)
			delete this;
	}
}

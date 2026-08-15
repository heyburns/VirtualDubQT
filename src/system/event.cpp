// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2006 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/vdtypes.h>
#include <vd2/system/event.h>

///////////////////////////////////////////////////////////////////////////////

VDDelegate::VDDelegate() {
	mpPrev = mpNext = this;
}

VDDelegate::~VDDelegate() {
	VDDelegateNode *next = mpNext;
	VDDelegateNode *prev = mpPrev;
	prev->mpNext = next;
	next->mpPrev = prev;
}

///////////////////////////////////////////////////////////////////////////////

VDEventBase::VDEventBase() {
	mAnchor.mpPrev = mAnchor.mpNext = &mAnchor;
}

VDEventBase::~VDEventBase() {
	while(mAnchor.mpPrev != &mAnchor)
		Remove(static_cast<VDDelegate&>(*mAnchor.mpPrev));
}

void VDEventBase::Add(VDDelegate& dbase) {
	VDDelegateNode *next = mAnchor.mpNext;

	VDASSERT(dbase.mpPrev == &dbase);

	mAnchor.mpNext = &dbase;
	dbase.mpPrev = &mAnchor;
	dbase.mpNext = next;
	next->mpPrev = &dbase;
}

void VDEventBase::Remove(VDDelegate& dbase) {
	VDASSERT(dbase.mpPrev != &dbase);

	VDDelegateNode *next = dbase.mpNext;
	VDDelegateNode *prev = dbase.mpPrev;
	prev->mpNext = next;
	next->mpPrev = prev;
	dbase.mpPrev = dbase.mpNext = &dbase;
}

void VDEventBase::Raise(void *src, const void *info) {
	// We allow the specific case of removing the delegate that's being removed.
	VDDelegateNode *node = mAnchor.mpNext;
	
	while(node != &mAnchor) {
		VDDelegateNode *next = node->mpNext;

		VDDelegate& dbase = static_cast<VDDelegate&>(*node);

		dbase.mpCallback(src, info, dbase);

		node = next;
	}
}

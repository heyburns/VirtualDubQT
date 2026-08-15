// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include "FilterFrameSharingPredictor.h"

VDFilterFrameSharingPredictor::VDFilterFrameSharingPredictor() {
	Clear();
}

void VDFilterFrameSharingPredictor::Clear() {
	LRUEntry dummy = { -1, true };
	for(int i=0; i<8; ++i)
		mLRU[i] = dummy;

	mShareCount = 8;
}

void VDFilterFrameSharingPredictor::OnRequest(sint64 frame) {
	for(int i=0; i<8; ++i) {
		LRUEntry& e = mLRU[i];

		if (e.mFrame == frame) {
			if (!e.mbShared) {
				e.mbShared = true;
				++mShareCount;
			}

			if (i) {
				const LRUEntry t(e);

				memmove(mLRU + 1, mLRU, i*sizeof(mLRU[0]));
				mLRU[0] = t;
			}

			return;
		}
	}

	if (mLRU[7].mbShared)
		--mShareCount;

	memmove(mLRU + 1, mLRU, 7*sizeof(mLRU[0]));
	mLRU[0].mFrame = frame;
	mLRU[0].mbShared = false;
}

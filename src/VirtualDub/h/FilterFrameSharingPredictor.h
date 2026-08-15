// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_FILTERFRAMESHARINGPREDICTOR_H
#define f_VD2_FILTERFRAMESHARINGPREDICTOR_H

class VDFilterFrameSharingPredictor {
public:
	VDFilterFrameSharingPredictor();

	void Clear();
	void OnRequest(sint64 frame);

	bool IsSharingPredicted(sint64 frame) const {
		return mShareCount > 0;
	}

protected:
	struct LRUEntry {
		sint64	mFrame;
		bool	mbShared;
	};

	uint32		mShareCount;
	LRUEntry	mLRU[8];
};

#endif

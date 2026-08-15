// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AF_POLYPHASE_H
#define f_AF_POLYPHASE_H

#include <vector>
#include <vd2/system/VDRingBuffer.h>
#include "af_base.h"

class VDAudioFilterSymmetricFIR : public VDAudioFilterBase {
protected:
	VDAudioFilterSymmetricFIR();
	~VDAudioFilterSymmetricFIR();

	uint32 Prepare();
	uint32 Run();
	void Start();

	sint64 Seek(sint64);

	virtual void GenerateFilter(int freq) = 0;

	std::vector<sint16, vdaligned_alloc<sint16> >		mFilterBank;
	int mFilterSize;

	std::vector<sint16>		mFIRBuffer;
	int mFIRBufferChannelStride;
	int mFIRBufferReadPoint;
	int mFIRBufferWritePoint;
	int mFIRBufferLimit;
	int mMaxQuantum;
};

class VDAudioFilterPolyphase : public VDAudioFilterBase {
protected:
	VDAudioFilterPolyphase();
	~VDAudioFilterPolyphase();

	uint32 Prepare();
	uint32 Run();
	void Start();

	sint64 Seek(sint64);

	virtual int GenerateFilterBank(int freq) = 0;

	std::vector<sint16, vdaligned_alloc<sint16> >		mFilterBank;
	int mFilterSize;
	uint32 mCurrentPhase;

	std::vector<sint16>		mFIRBuffer;
	int mFIRBufferChannelStride;
	int mFIRBufferPoint;
	int mFIRBufferLimit;

	uint64		mRatio;		// 32:32
};

#endif

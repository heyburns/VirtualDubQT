// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIPIPE_H
#define f_AVIPIPE_H

#include <vd2/system/thread.h>
#include <vd2/system/atomic.h>
#include <vd2/system/event.h>

struct VDRenderVideoPipeFrameInfo {
	void		*mpData;
	uint32		mLength;
	int			mSrcIndex;
	VDPosition	mStreamFrame;
	VDPosition	mDisplayFrame;
	VDPosition	mTargetSample;
	uint32		mFlags;
	int			mDroptype;
	bool		mbFinal;
};

class AVIPipe {
private:
	static char me[];

	VDSignal			msigRead, msigWrite;
	VDCriticalSection	mcsQueue;

	struct AVIPipeBuffer {
		uint32	mBufferSize;
		bool	mbInUse;
		VDRenderVideoPipeFrameInfo mFrameInfo;
	} *pBuffers;

	int		num_buffers;
	long	round_size;

	int		mReadPt;
	int		mWritePt;
	int		mLevel;

	VDAtomicInt		mState;

	enum {
		kFlagFinalizeTriggered		= 1,
		kFlagFinalizeAcknowledged	= 2,
		kFlagAborted				= 4
	};

	VDEvent<AVIPipe, bool> mEventBufferAdded;

public:
	// These are the same as in VideoSourceAVI
	enum {
		kDroppable=0,
		kDependant,
		kIndependent
	};

	AVIPipe(int buffers, long roundup_size);
	~AVIPipe();

	VDSignal& getReadSignal() { return msigRead; }
	VDSignal& getWriteSignal() { return msigWrite; }

	bool isOkay();
	bool isFinalized();
	bool isFinalizeAcked();

	bool full();
	int size() const { return num_buffers; }

	void *getWriteBuffer(long len, int *handle_ptr);
	void postBuffer(const VDRenderVideoPipeFrameInfo& frameInfo);
	const VDRenderVideoPipeFrameInfo *TryReadBuffer();
	const VDRenderVideoPipeFrameInfo *getReadBuffer();
	void releaseBuffer();
	void finalize();
	void finalizeAck();
	void abort();
	void getDropDistances(int& dependant, int& independent);
	void getQueueInfo(int& total, int& finals, int& allocated);

	VDEvent<AVIPipe, bool>& OnBufferAdded() {
		return mEventBufferAdded;
	}
};

#endif

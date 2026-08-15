// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#if 0
#ifndef f_VIDEOSOURCEADAPTER_H
#define f_VIDEOSOURCEADAPTER_H

#include "VideoSource.h"

class VideoSourceAdapter : public VideoSource {
public:
	VideoSourceAdapter();
	~VideoSourceAdapter();

	virtual bool setDecompressedFormat(int depth);
	virtual bool setDecompressedFormat(BITMAPINFOHEADER *pbih);

	BITMAPINFOHEADER *getDecompressedFormat() {
		return bmihDecompressedFormat;
	}

	virtual void streamSetDesiredFrame(long frame_num);
	virtual long streamGetNextRequiredFrame(BOOL *is_preroll);
	virtual int	streamGetRequiredCount(long *pSize);
	virtual void *streamGetFrame(void *inputBuffer, long data_len, BOOL is_key, BOOL is_preroll, long frame_num) = NULL;

	virtual void streamBegin(bool fRealTime, bool bForceReset);

	virtual void invalidateFrameBuffer();
	virtual	BOOL isFrameBufferValid() = NULL;

	virtual void *getFrame(LONG frameNum) = NULL;

	virtual char getFrameTypeChar(long lFrameNum) = 0;

	virtual eDropType getDropType(long lFrameNum)=0;

	virtual bool isKeyframeOnly();
	virtual bool isType1();

	virtual long	streamToDisplayOrder(long sample_num) { return sample_num; }
	virtual long	displayToStreamOrder(long display_num) { return display_num; }

	virtual bool isDecodable(long sample_num) = 0;
};

#endif
#endif

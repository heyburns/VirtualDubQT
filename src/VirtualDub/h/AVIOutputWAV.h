// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUTWAV_H
#define f_AVIOUTPUTWAV_H

#include <vd2/system/fileasync.h>
#include <vd2/system/vdalloc.h>

#include "AVIOutput.h"

class AVIOutputWAV : public AVIOutput {
public:
	AVIOutputWAV();
	~AVIOutputWAV();

	IVDMediaOutputStream *createVideoStream();
	IVDMediaOutputStream *createAudioStream();

	void setBufferSize(sint32 nBytes) {
		mBufferSize = nBytes;
	}

	bool init(const wchar_t *szFile);
	bool init(VDFileHandle h, bool pipeMode);
	void finalize();

	void write(const void *pBuffer, uint32 cbBuffer);

	bool		mbAutoWriteWAVE64;

private:
	void WriteHeader(bool initial);

	vdautoptr<IVDFileAsync>	mpFileAsync;
	bool		mbHeaderOpen;
	bool		mbWriteWAVE64;
	bool		mbPipeMode;
	uint64		mBytesWritten;
	uint32		mHeaderSize;
	sint32		mBufferSize;
};

#endif

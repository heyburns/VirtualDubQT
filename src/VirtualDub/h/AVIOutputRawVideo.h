// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2006 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUTRAWVIDEO_H
#define f_AVIOUTPUTRAWVIDEO_H

#include <vd2/system/fileasync.h>
#include <vd2/system/vdalloc.h>
#include <vd2/Kasumi/pixmap.h>

#include "AVIOutput.h"
#include "DubOutput.h"

class AVIOutputRawVideo : public AVIOutput {
public:
	AVIOutputRawVideo(const VDAVIOutputRawVideoFormat& format);
	~AVIOutputRawVideo();

	void SetInputLayout(const VDPixmapLayout& layout);

	IVDMediaOutputStream *createVideoStream();
	IVDMediaOutputStream *createAudioStream();

	void setBufferSize(sint32 nBytes) {
		mBufferSize = nBytes;
	}

	bool init(const wchar_t *szFile);
	bool init(VDFileHandle h);
	void finalize();

	void write(const void *pBuffer, uint32 cbBuffer);

private:
	void WriteHeader(bool initial);

	vdautoptr<IVDFileAsync>	mpFileAsync;
	uint64		mBytesWritten;
	sint32		mBufferSize;
	bool		mbDoTruncate;

	VDPixmapLayout	mInputLayout;

	const VDAVIOutputRawVideoFormat mFormat;
};

#endif

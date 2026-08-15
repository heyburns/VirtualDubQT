// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#include <vd2/system/error.h>
#include "AVIOutput.h"
#include "oshelper.h"
#include "misc.h"

///////////////////////////////////////////

AVIOutputStream::AVIOutputStream() {
}

AVIOutputStream::~AVIOutputStream() {
}

void *AVIOutputStream::AsInterface(uint32 id) {
	if (id == IVDMediaOutputStream::kTypeID)
		return static_cast<IVDMediaOutputStream *>(this);

	return NULL;
}

////////////////////////////////////

AVIOutput::AVIOutput() {
	audioOut			= NULL;
	videoOut			= NULL;
}

AVIOutput::~AVIOutput() {
	delete audioOut;
	delete videoOut;
}

void *AVIOutput::AsInterface(uint32 id) {
	if (id == IVDMediaOutput::kTypeID)
		return static_cast<IVDMediaOutput *>(this);

	return NULL;
}

////////////////////////////////////

class AVIVideoNullOutputStream : public AVIOutputStream {
public:
	void write(uint32 flags, const void *pBuffer, uint32 cbBuffer, uint32 lSamples) {}
	void partialWriteBegin(uint32 flags, uint32 bytes, uint32 samples) {}
	void partialWrite(const void *pBuffer, uint32 cbBuffer) {}
	void partialWriteEnd() {}
};

AVIOutputNull::~AVIOutputNull() {
}

bool AVIOutputNull::init(const wchar_t *szFile) {
	if (!videoOut)
		return false;
	return true;
}

void AVIOutputNull::finalize() {
}

IVDMediaOutputStream *AVIOutputNull::createAudioStream() {
	return NULL;
}

IVDMediaOutputStream *AVIOutputNull::createVideoStream() {
	videoOut = new_nothrow AVIVideoNullOutputStream;
	if (!videoOut)
		throw MyMemoryError();
	return videoOut;
}

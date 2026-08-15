// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2017 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#include <windows.h>
#include <vfw.h>

#include <vd2/system/error.h>

#include "AVIOutputRawAudio.h"

//////////////////////////////////////////////////////////////////////
//
// AVIAudioOutputStreamRaw
//
//////////////////////////////////////////////////////////////////////

class AVIAudioOutputStreamRaw : public AVIOutputStream {
public:
	AVIAudioOutputStreamRaw(AVIOutputRawAudio *pParent);

	void write(uint32 flags, const void *pBuffer, uint32 cbBuffer, uint32 samples);
	void partialWriteBegin(uint32 flags, uint32 bytes, uint32 samples);
	void partialWrite(const void *pBuffer, uint32 cbBuffer);
	void partialWriteEnd();

protected:
	AVIOutputRawAudio *const mpParent;
};

AVIAudioOutputStreamRaw::AVIAudioOutputStreamRaw(AVIOutputRawAudio *pParent) : mpParent(pParent) {
}

void AVIAudioOutputStreamRaw::write(uint32 flags, const void *pBuffer, uint32 cbBuffer, uint32 samples) {
	mpParent->write(pBuffer, cbBuffer);
}

void AVIAudioOutputStreamRaw::partialWriteBegin(uint32 flags, uint32 bytes, uint32 samples) {

}

void AVIAudioOutputStreamRaw::partialWrite(const void *pBuffer, uint32 cbBuffer) {
	mpParent->write(pBuffer, cbBuffer);
}

void AVIAudioOutputStreamRaw::partialWriteEnd() {
}

//////////////////////////////////////////////////////////////////////
//
// AVIOutputRawAudio
//
//////////////////////////////////////////////////////////////////////

AVIOutputRawAudio::AVIOutputRawAudio() {
	mBytesWritten		= 0;
	mBufferSize			= 65536;
}

AVIOutputRawAudio::~AVIOutputRawAudio() {
}

IVDMediaOutputStream *AVIOutputRawAudio::createVideoStream() {
	return NULL;
}

IVDMediaOutputStream *AVIOutputRawAudio::createAudioStream() {
	VDASSERT(!audioOut);
	if (!(audioOut = new_nothrow AVIAudioOutputStreamRaw(this)))
		throw MyMemoryError();
	return audioOut;
}

bool AVIOutputRawAudio::init(const wchar_t *pwszFile) {
	mpFileAsync = VDCreateFileAsync();
	mpFileAsync->Open(pwszFile, 2, mBufferSize >> 1);
	mbPipeMode = false;

	mBytesWritten = 0;
	return true;
}

bool AVIOutputRawAudio::init(VDFileHandle h, bool pipeMode) {
	mpFileAsync = VDCreateFileAsync();
	mpFileAsync->OpenPipe(h, 2, mBufferSize >> 1);
	mbPipeMode = pipeMode;

	mBytesWritten = 0;
	return true;
}

void AVIOutputRawAudio::finalize() {
	if (!mpFileAsync->IsOpen())
		return;

	mpFileAsync->FastWriteEnd();

	if (!mbPipeMode)
		mpFileAsync->Truncate(mBytesWritten);

	mpFileAsync->Close();
}

void AVIOutputRawAudio::write(const void *pBuffer, uint32 cbBuffer) {
	mpFileAsync->FastWrite(pBuffer, cbBuffer);
	mBytesWritten += cbBuffer;
}

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2015-2018 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#include <vd2/system/error.h>
#include <vd2/Kasumi/blitter.h>
#include <vd2/Riza/bitmap.h>
#include <../Kasumi/h/uberblit_base.h>

#include "AVIOutputRawVideo.h"

//////////////////////////////////////////////////////////////////////
//
// AVIVideoOutputStreamRaw
//
//////////////////////////////////////////////////////////////////////

class AVIVideoOutputStreamRaw : public AVIOutputStream, public IVDVideoImageOutputStream {
public:
	AVIVideoOutputStreamRaw(AVIOutputRawVideo *pParent, const VDAVIOutputRawVideoFormat& format, uint32 w, uint32 h);

	void *AsInterface(uint32 id);

	void write(uint32 flags, const void *pBuffer, uint32 cbBuffer, uint32 samples);
	void partialWriteBegin(uint32 flags, uint32 bytes, uint32 samples);
	void partialWrite(const void *pBuffer, uint32 cbBuffer);
	void partialWriteEnd();

	void WriteVideoImage(const VDPixmap *px);

protected:
	AVIOutputRawVideo *const mpParent;
	vdblock<uint8, vdaligned_alloc<uint8> > mOutputBuffer;
	VDPixmap mOutputPixmap;

	const VDAVIOutputRawVideoFormat& mFormat;
	vdautoptr<IVDPixmapBlitter> mpBlitter;
};

AVIVideoOutputStreamRaw::AVIVideoOutputStreamRaw(AVIOutputRawVideo *pParent, const VDAVIOutputRawVideoFormat& format, uint32 w, uint32 h)
	: mpParent(pParent)
	, mFormat(format)
{
	VDPixmapLayout layout;
	uint32 linsize = VDPixmapCreateLinearLayout(layout, format.mOutputFormat, w, h, format.mScanlineAlignment);

	if (format.mbSwapChromaPlanes) {
		std::swap(layout.data2, layout.data3);
		std::swap(layout.pitch2, layout.pitch3);
	}

	if (format.mbBottomUp)
		VDPixmapLayoutFlipV(layout);

	mOutputBuffer.resize(linsize, 0);
	mOutputPixmap = VDPixmapFromLayout(layout, mOutputBuffer.data());
}

void *AVIVideoOutputStreamRaw::AsInterface(uint32 id) {
	if (id == IVDVideoImageOutputStream::kTypeID)
		return static_cast<IVDVideoImageOutputStream *>(this);

	return AVIOutputStream::AsInterface(id);
}

void AVIVideoOutputStreamRaw::write(uint32 flags, const void *pBuffer, uint32 cbBuffer, uint32 samples) {
	throw MyError("Raw writes are not supported.");
}

void AVIVideoOutputStreamRaw::partialWriteBegin(uint32 flags, uint32 bytes, uint32 samples) {
	throw MyError("Partial writes are not supported.");
}

void AVIVideoOutputStreamRaw::partialWrite(const void *pBuffer, uint32 cbBuffer) {
	throw MyError("Partial writes are not supported.");
}

void AVIVideoOutputStreamRaw::partialWriteEnd() {
	throw MyError("Partial writes are not supported.");
}

void AVIVideoOutputStreamRaw::WriteVideoImage(const VDPixmap *px) {
	if (!mpBlitter) {
		FilterModPixmapInfo out_info;
		VDSetPixmapInfoForBitmap(out_info, px->format);
		IVDPixmapExtraGen* extraDst = VDPixmapCreateNormalizer(px->format, out_info);
		mpBlitter = VDPixmapCreateBlitter(mOutputPixmap, *px, extraDst);
		delete extraDst;
		if (!mpBlitter)
			throw MyMemoryError();
	}

	mpBlitter->Blit(mOutputPixmap, *px);
	mpParent->write(mOutputBuffer.data(), mOutputBuffer.size());
}

//////////////////////////////////////////////////////////////////////
//
// AVIOutputRawVideo
//
//////////////////////////////////////////////////////////////////////

AVIOutputRawVideo::AVIOutputRawVideo(const VDAVIOutputRawVideoFormat& format)
	: mFormat(format)
{
	mBytesWritten		= 0;
	mBufferSize			= 65536;
}

AVIOutputRawVideo::~AVIOutputRawVideo() {
}

void AVIOutputRawVideo::SetInputLayout(const VDPixmapLayout& layout) {
	mInputLayout = layout;
}

IVDMediaOutputStream *AVIOutputRawVideo::createVideoStream() {
	VDASSERT(!videoOut);
	if (!(videoOut = new_nothrow AVIVideoOutputStreamRaw(this, mFormat, mInputLayout.w, mInputLayout.h)))
		throw MyMemoryError();
	return videoOut;
}

IVDMediaOutputStream *AVIOutputRawVideo::createAudioStream() {
	return NULL;
}

bool AVIOutputRawVideo::init(const wchar_t *pwszFile) {
	mpFileAsync = VDCreateFileAsync();
	mpFileAsync->Open(pwszFile, 2, mBufferSize >> 1);

	mBytesWritten = 0;
	mbDoTruncate = true;
	return true;
}

bool AVIOutputRawVideo::init(VDFileHandle h) {
	mpFileAsync = VDCreateFileAsync();
	mpFileAsync->OpenPipe(h, 2, mBufferSize >> 1);

	mBytesWritten = 0;
	mbDoTruncate = false;
	return true;
}

void AVIOutputRawVideo::finalize() {
	if (!mpFileAsync->IsOpen())
		return;

	mpFileAsync->FastWriteEnd();
	if (mbDoTruncate)
		mpFileAsync->Truncate(mBytesWritten);
	mpFileAsync->Close();
}

void AVIOutputRawVideo::write(const void *pBuffer, uint32 cbBuffer) {
	mpFileAsync->FastWrite(pBuffer, cbBuffer);
	mBytesWritten += cbBuffer;
}

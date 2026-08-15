// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2016-2019 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VIDEOSEQUENCECOMPRESSOR_H
#define f_VIDEOSEQUENCECOMPRESSOR_H

#include <windows.h>
#include <vfw.h>
#include <vd2/system/VDString.h>
#include <vd2/system/Fraction.h>
#include <vd2/Kasumi/pixmap.h>

struct EncoderHIC;
struct VDPacketInfo;

class VideoSequenceCompressor {
public:
	VideoSequenceCompressor();
	~VideoSequenceCompressor();
	void SetDriver(EncoderHIC* driver, uint32 kilobytesPerSecond, long quality, long keyrate, bool ownHandle);
	void GetOutputFormat(const void *inputFormat, vdstructex<tagBITMAPINFOHEADER>& outputFormat);
	void GetOutputFormat(const VDPixmapLayout *inputFormat, vdstructex<tagBITMAPINFOHEADER>& outputFormat);
	const void *GetOutputFormat();
	uint32 GetOutputFormatSize();
	int GetInputFormat(FilterModPixmapInfo* info) {
		if (info) {
			info->copy_ref(mInputInfo);
			info->copy_alpha(mInputInfo);
		}
		return mInputLayout.format;
	}
	virtual void GetInputBitmapFormat(vdstructex<tagBITMAPINFOHEADER>& bm) {
		bm = mInputFormat;
	}

	void init(EncoderHIC* driver, long lQ, long lKeyRate);
	void Start(const void *inputFormat, uint32 inputFormatSize, const void *outputFormat, uint32 outputFormatSize, const VDFraction& frameRate, VDPosition frameCount);
	void Start(const VDPixmapLayout& layout, FilterModPixmapInfo& info, const void *outputFormat, uint32 outputFormatSize, const VDFraction& frameRate, VDPosition frameCount);
	void internalStart(const void *outputFormat, uint32 outputFormatSize, const VDFraction& frameRate, VDPosition frameCount);
	void dropFrame();
	bool packFrame(void *dst, const void *src, uint32& size, VDPacketInfo& packetInfo);
	void Stop();
	void Truncate();

	long getMaxSize() {
		return lMaxPackedSize;
	}

	void* createResultBuffer();

private:
	void PackFrameInternal(void* dst, DWORD frameSize, DWORD q, const void *src, DWORD dwFlagsIn, DWORD& dwFlagsOut, VDPacketInfo& packetInfo, sint32& bytes);

	EncoderHIC	*driver;
	bool		mbOwnHandle;
	DWORD		dwFlags;
	DWORD		mVFWExtensionMessageID;
	vdstructex<BITMAPINFOHEADER>	mInputFormat;
	vdstructex<BITMAPINFOHEADER>	mOutputFormat;
	VDPixmapLayout  	mInputLayout;
	FilterModPixmapInfo mInputInfo;
	VDFraction	mFrameRate;
	VDPosition	mFrameCount;
	char		*pPrevBuffer;
	long		lFrameSent;
	long		lFrameDone;
	long		lKeyRate;
	long		lQuality;
	long		lDataRate;
	long		lKeyRateCounter;
	long		lMaxFrameSize;
	long		lMaxPackedSize;
	bool		fCompressionStarted;
	long		lSlopSpace;
	long		lKeySlopSpace;

	bool		mbKeyframeOnly;
	bool		mbCompressionRestarted;

	// crunch emulation
	sint32		mQualityLo;
	sint32		mQualityLast;
	sint32		mQualityHi;

	char		*pConfigData;
	int			cbConfigData;

	VDStringW	mCodecName;
	VDStringW	mDriverName;
};

#endif

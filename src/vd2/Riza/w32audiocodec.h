// VirtualDub - Video processing and capture application
// A/V interface library
//
// Copyright (C) 1998-2004 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_W32AUDIOCODEC_H
#define f_VD2_RIZA_W32AUDIOCODEC_H

#include <vd2/system/vdstl.h>
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>
#include <vector>
#include <vd2/Riza/audiocodec.h>

class VDAudioCodecW32 : public IVDAudioCodec {
public:
	VDAudioCodecW32();
	~VDAudioCodecW32();

	bool Init(const WAVEFORMATEX *pSrcFormat, const WAVEFORMATEX *pDstFormat, bool isCompression, const wchar_t* pShortNameDriverHint, bool throwOnError);
	void Shutdown();

	bool IsEnded() const { return mbEnded; }

	unsigned	GetInputLevel() const { return mBufferHdr.cbSrcLength; }
	unsigned	GetInputSpace() const { return mInputBuffer.size() - mBufferHdr.cbSrcLength; }
	unsigned	GetOutputLevel() const { return mBufferHdr.cbDstLengthUsed - mOutputReadPt; }
	const VDWaveFormat *GetOutputFormat() const { return mDstFormat.data(); }
	unsigned	GetOutputFormatSize() const { return mDstFormat.size(); }

	void		Restart();
	bool		Convert(bool flush, bool requireOutput);

	void		*LockInputBuffer(unsigned& bytes);
	void		UnlockInputBuffer(unsigned bytes);
	const void	*LockOutputBuffer(unsigned& bytes);
	void		UnlockOutputBuffer(unsigned bytes);
	unsigned	CopyOutput(void *dst, unsigned bytes);

protected:
	HACMDRIVER		mhDriver;
	HACMSTREAM		mhStream;
	vdstructex<VDWaveFormat>	mSrcFormat;
	vdstructex<VDWaveFormat>	mDstFormat;
	ACMSTREAMHEADER mBufferHdr;
	wchar_t mDriverName[64];
	wchar_t mDriverFilename[64];

	unsigned		mOutputReadPt;
	bool			mbFirst;
	bool			mbFlushing;
	bool			mbEnded;

	vdblock<uint8>	mInputBuffer;
	vdblock<uint8>	mOutputBuffer;
};

#endif

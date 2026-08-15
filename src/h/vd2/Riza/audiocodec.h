// VirtualDub - Video processing and capture application
// A/V interface library
//
// Copyright (C) 1998-2007 Avery Lee
// Copyright (C) 2017-2018 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_AUDIOCODEC_H
#define f_VD2_RIZA_AUDIOCODEC_H

#ifdef _MSC_VER
	#pragma once
#endif

#ifndef f_VD2_SYSTEM_VDTYPES_H
	#include <vd2/system/vdtypes.h>
#endif
#include <vd2/plugin/vdinputdriver.h>

#ifdef _MSC_VER
	#pragma pack(push, 2)
#endif

struct VDXStreamInfo;

struct VDWaveFormat {
	enum {
		kTagPCM			= 1,      // WAVE_FORMAT_PCM
		kTagCCITTALaw	= 0x0006, // WAVE_FORMAT_ALAW
		kTagCCITTMuLaw	= 0x0007, // WAVE_FORMAT_MULAW
		kTagMPEG1		= 0x0050, // WAVE_FORMAT_MPEG
		kTagMPEGLayer3	= 0x0055, // WAVE_FORMAT_MPEGLAYER3
	};

	uint16		mTag;
	uint16		mChannels;
	uint32		mSamplingRate;
	uint32		mDataRate;
	uint16		mBlockSize;
	uint16		mSampleBits;
	uint16		mExtraSize;
};

#ifdef _MSC_VER
	#pragma pack(pop)
#endif

class VDINTERFACE IVDAudioCodec {
public:
	virtual ~IVDAudioCodec() {}
	virtual void Shutdown() = 0;

	virtual bool IsEnded() const = 0;

	virtual unsigned	GetInputLevel() const = 0;
	virtual unsigned	GetInputSpace() const = 0;
	virtual unsigned	GetOutputLevel() const = 0;
	virtual const VDWaveFormat *GetOutputFormat() const = 0;
	virtual unsigned	GetOutputFormatSize() const = 0;
	virtual void		GetStreamInfo(VDXStreamInfo& si) const {}

	virtual void		Restart() = 0;
	virtual bool		Convert(bool flush, bool requireOutput) = 0;

	virtual void		*LockInputBuffer(unsigned& bytes) = 0;
	virtual void		UnlockInputBuffer(unsigned bytes) = 0;
	virtual const void	*LockOutputBuffer(unsigned& bytes) = 0;
	virtual void		UnlockOutputBuffer(unsigned bytes) = 0;
	virtual unsigned	CopyOutput(void *dst, unsigned bytes) = 0;
	virtual unsigned	CopyOutput(void *dst, unsigned bytes, sint64& duration) {
		duration = -1;
		return CopyOutput(dst,bytes);
	}
	virtual vd2::FormatConfidence SuggestFileFormat(const char* name) { return vd2::kFormat_Unknown; }
};

IVDAudioCodec *VDLocateAudioDecompressor(const VDWaveFormat *srcFormat, const VDWaveFormat *dstFormat, bool preferInternalCodecs, const wchar_t* pShortNameDriverHint = NULL);

IVDAudioCodec *VDCreateAudioDecompressor(const VDWaveFormat *srcFormat, const VDWaveFormat *dstFormat);
IVDAudioCodec *VDCreateAudioDecompressorALaw(const VDWaveFormat *srcFormat, const VDWaveFormat *dstFormat);
IVDAudioCodec *VDCreateAudioDecompressorMuLaw(const VDWaveFormat *srcFormat, const VDWaveFormat *dstFormat);
IVDAudioCodec *VDCreateAudioDecompressorMPEG(const VDWaveFormat *srcFormat, const VDWaveFormat *dstFormat);

IVDAudioCodec *VDCreateAudioCompressorW32(const VDWaveFormat *srcFormat, const VDWaveFormat *dstFormat, const wchar_t* pShortNameDriverHint, bool throwIfNotFound);
IVDAudioCodec *VDCreateAudioDecompressorW32(const VDWaveFormat *srcFormat, const VDWaveFormat *dstFormat, const wchar_t* pShortNameDriverHint, bool throwIfNotFound);

long fill_silence_pcm(void* buf, int samples, const VDWaveFormat *wfex);
bool is_audio_pcm(const VDWaveFormat *wfex);
bool is_audio_pcm8(const VDWaveFormat *wfex);
bool is_audio_pcm16(const VDWaveFormat *wfex);
bool is_audio_float(const VDWaveFormat *wfex);
int get_audio_sampleBits(const VDWaveFormat *wfex);
int default_channel_mask(int n);

#endif	// f_VD2_RIZA_AUDIOCODEC_H

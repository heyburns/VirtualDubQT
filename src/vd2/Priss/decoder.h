// VirtualDub - Video processing and capture application
// MPEG audio decoding library
//
// Copyright (C) 2003-2007 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_PRISS_DECODER_H
#define f_VD2_PRISS_DECODER_H

#ifdef _MSC_VER
	#pragma once
#endif

class VDMPEGAudioStreamInfo {
public:
	long	lBitrate;			// average bits/second for this stream (0=unconstrained)
	long	lSamplingFreq;		// sampling frequency (Hz)
	char	nLayer;				// MPEG audio layer (1-3)
	char	nMPEGVer;			// MPEG version (1/2)
	char	fStereo;			// true: stereo, false: mono
};

class IVDMPEGAudioBitsource {
public:
	virtual int read(void *buffer, int bytes)=0;
};

class IVDMPEGAudioDecoder {
public:
	enum {
		ERR_NONE			= 0,
		ERR_EOF				= 1,
		ERR_READ			= 2,
		ERR_MPEG25			= 3,
/*		ERR_LAYER1			= 4,*/ /* Not applicable to NekoAmp1.4 */
		ERR_FREEFORM		= 5,
		ERR_SYNC			= 6,
		ERR_INTERNAL		= 7,
		ERR_INCOMPLETEFRAME	= 8,
		ERR_INVALIDDATA		= 9
	};

	virtual ~IVDMPEGAudioDecoder() {}

	virtual void	Init()								= 0;
	virtual void	SetSource(IVDMPEGAudioBitsource *pSource) = 0;
	virtual void	SetDestination(sint16 *psDest)		= 0;
	virtual uint32	GetSampleCount()					= 0;
	virtual uint32	GetFrameDataSize()					= 0;
	virtual void	GetStreamInfo(VDMPEGAudioStreamInfo *pasi)	= 0;
	virtual const char *GetErrorString(int err)			= 0;
	virtual void	Reset()								= 0;
	virtual void	ReadHeader()						= 0;
	virtual void	PrereadFrame()						= 0;
	virtual bool	DecodeFrame()						= 0;
	virtual void	ConcealFrame()						= 0;
};

IVDMPEGAudioDecoder *VDCreateMPEGAudioDecoder();

#endif

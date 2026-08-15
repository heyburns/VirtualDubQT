// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_AUDIOOUT_H
#define f_VD2_RIZA_AUDIOOUT_H

#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>

struct HWAVEOUT__;
struct tWAVEFORMATEX;

class IVDAudioOutput {
public:
	virtual ~IVDAudioOutput() {}

	virtual bool	Init(uint32 bufsize, uint32 bufcount, const tWAVEFORMATEX *wf, const wchar_t *preferredDevice) = 0;
	virtual void	Shutdown() = 0;
	virtual void	GoSilent() = 0;

	virtual bool	IsSilent() = 0;
	virtual bool	IsFrozen() = 0;
	virtual bool	IsOverflow() = 0;
	virtual uint32	GetAvailSpace() = 0;
	virtual uint32	GetBufferLevel() = 0;
	virtual uint32	EstimateHWBufferLevel() = 0;
	virtual sint32	GetPosition() = 0;
	virtual sint32	GetPositionBytes() = 0;
	virtual double	GetPositionTime() = 0;

	virtual bool	Start() = 0;
	virtual bool	Stop() = 0;
	virtual bool	Flush() = 0;

	virtual bool	Write(const void *data, uint32 len) = 0;
	virtual bool	Finalize(uint32 timeout = (uint32)-1) = 0;
};

IVDAudioOutput *VDCreateAudioOutputWaveOutW32();
IVDAudioOutput *VDCreateAudioOutputDirectSoundW32();

#endif

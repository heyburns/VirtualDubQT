// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AF_SINK_H
#define f_AF_SINK_H

class IVDAudioFilterSink {
public:
	virtual uint32 ReadSamples(void *dst, uint32 samples) = 0;
	virtual const void *GetFormat()=0;
	virtual int GetFormatLen()=0;
	virtual sint64 GetLength()=0;
	virtual bool IsEnded() = 0;
};

IVDAudioFilterSink *VDGetAudioFilterSinkInterface(void *);

#endif

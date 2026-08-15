// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2016 Anton Shekhovtsov
//
// Animated PNG support by Max Stepin
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUTAPNG_H
#define f_AVIOUTPUTAPNG_H

#include <vd2/system/VDString.h>
#include "AVIOutput.h"

class VideoSource;

class IVDAVIOutputAPNG {
public:
	virtual ~IVDAVIOutputAPNG() {}
	virtual AVIOutput *AsAVIOutput() = 0;
	virtual void SetFramesCount(int framesCount) = 0;
	virtual void SetLoopCount(int loopCount) = 0;
	virtual void SetAlpha(int alpha) = 0;
	virtual void SetGrayscale(int grayscale) = 0;
    virtual void SetRate(int rate) = 0;
    virtual void SetScale(int scale) = 0;
};

IVDAVIOutputAPNG *VDCreateAVIOutputAPNG();

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2007 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUTGIF_H
#define f_AVIOUTPUTGIF_H

#include <vd2/system/VDString.h>
#include "AVIOutput.h"

class VideoSource;

class IVDAVIOutputGIF {
public:
	virtual ~IVDAVIOutputGIF() {}
	virtual AVIOutput *AsAVIOutput() = 0;
	virtual void SetLoopCount(int loopCount) = 0;
};

IVDAVIOutputGIF *VDCreateAVIOutputGIF();

#endif

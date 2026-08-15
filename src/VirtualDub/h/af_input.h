// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2005 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AF_INPUT_H
#define f_AF_INPUT_H

class IVDAudioFilterInput {
public:
	virtual void EnableDecompression(bool enable) = 0;
};

IVDAudioFilterInput *VDGetAudioFilterInputInterface(void *);

#endif

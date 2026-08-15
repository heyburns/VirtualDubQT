// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2006 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUTFLM_H
#define f_AVIOUTPUTFLM_H

#include <vd2/system/fileasync.h>

class AVIOutputFLM : public AVIOutput {
public:
	AVIOutputFLM();

	IVDMediaOutputStream *createVideoStream();
	IVDMediaOutputStream *createAudioStream();

	bool init(const wchar_t *szFile);
	void finalize();

protected:
	vdautoptr<IVDFileAsync> mpAsync;
};

#endif

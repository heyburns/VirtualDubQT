// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2017 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AUDIOSOURCE_H
#define f_AUDIOSOURCE_H

#include <vd2/Riza/audiocodec.h>
#include "DubSource.h"

class IAVIReadHandler;
class IAVIReadStream;

class AudioSource : public DubSource {
public:
	std::unique_ptr<char[]>	src_format;
	int src_format_len = 0;

	AudioSource() = default;

	void* allocSrcWaveFormat(int format_len) {
		if (src_format_len != format_len) {
			src_format.reset(new char[format_len]);
			src_format_len = format_len;
		}

		return (void*)src_format.get();
	}

	const VDWaveFormat *getWaveFormat() const {
		return (const VDWaveFormat *)getFormat();
	}
	const VDWaveFormat *getSourceWaveFormat() const {
		return (const VDWaveFormat *)src_format.get();
	}
	virtual void SetTargetFormat(const VDWaveFormat* format){}
	virtual void streamAppendReinit(){}
};

AudioSource *VDCreateAudioSourceWAV(const wchar_t *fn, uint32 inputBufferSize);

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2016-2017 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_SCENEDETECTOR_H
#define f_SCENEDETECTOR_H

#include <vd2/system/vdstl.h>

struct VDPixmap;

class SceneDetector {
public:
	SceneDetector();
	~SceneDetector();

	void Resize(uint32 width, uint32 height);
	void SetThresholds(int cut_threshold, int fade_threshold);
	bool Submit(const VDPixmap& src);
	void Reset();
	bool Enabled();

private:
	vdfastvector<uint32>	mCurrentLummap;
	vdfastvector<uint32>	mPrevLummap;
	uint32 tile_w, tile_h;
	bool last_valid;
	bool first_diff;
	bool last_fade_state;
	bool enabled;

	long cut_threshold;
	double fade_threshold;

	void BitmapToLummap(uint32 *lummap, const VDPixmap& src);
	void FlipBuffers();

};

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_F_CONVOLUTE_H
#define f_F_CONVOLUTE_H

#include <vd2/plugin/vdvideofilt.h>

struct ConvoluteFilterData {
	long m[9];
	long bias;
	void *dyna_func;
	uint32 dyna_size;
	uint32 dyna_old_protect;
	bool fClip;
};

int filter_convolute_run(const VDXFilterActivation *fa, const VDXFilterFunctions *ff);
long filter_convolute_param(VDXFilterActivation *fa, const VDXFilterFunctions *ff);
int filter_convolute_end(VDXFilterActivation *fa, const VDXFilterFunctions *ff);
int filter_convolute_start(VDXFilterActivation *fa, const VDXFilterFunctions *ff);

#endif

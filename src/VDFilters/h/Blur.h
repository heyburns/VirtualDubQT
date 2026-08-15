// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDFILTERS_BLUR_H
#define f_VD2_VDFILTERS_BLUR_H

struct VDPixmap;

class VEffect {
public:
	virtual ~VEffect() {}

	virtual void run(const VDPixmap&) = 0;
	virtual void run(const VDPixmap&, const VDPixmap&) = 0;
};

VEffect *VCreateEffectBlur(const VDPixmapLayout&);
VEffect *VCreateEffectBlurHi(const VDPixmapLayout&);

#endif

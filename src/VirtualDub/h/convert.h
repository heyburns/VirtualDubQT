// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_CONVERT_H
#define f_CONVERT_H

extern "C" void DIBconvert_32_to_16_dithered(void *dest, long dest_pitch, void *src, long src_pitch, long width, long height);
extern "C" void DIBconvert_32_to_16_565_dithered(void *dest, long dest_pitch, void *src, long src_pitch, long width, long height);

#endif

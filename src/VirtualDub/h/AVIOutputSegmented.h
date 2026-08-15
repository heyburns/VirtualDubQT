// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2006 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUTFILESEGMENTED_H
#define f_AVIOUTPUTFILESEGMENTED_H

class IVDDubberOutputSystem;
class IVDMediaOutput;

IVDMediaOutput *VDCreateMediaOutputSegmented(IVDDubberOutputSystem *pOutputSystem, double interval, double preload, sint64 max_bytes, sint64 max_frames);

#endif

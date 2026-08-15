// VirtualDub - Video processing and capture application
// Internal filter library
//
// Copyright (C) 1998-2011 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDFILTERS_SINGLEVALUEDIALOG_H
#define f_VD2_VDFILTERS_SINGLEVALUEDIALOG_H

#include <vd2/plugin/vdvideofilt.h>

bool VDFilterGetSingleValue(VDXHWND hWnd, sint32 cVal, sint32 *result, sint32 lMin, sint32 lMax, const char *title, IVDXFilterPreview2 *ifp2, void (*pUpdateFunction)(long value, void *data), void *pUpdateFunctionData);

#endif	// f_VD2_VDFILTERS_SINGLEVALUEDIALOG_H

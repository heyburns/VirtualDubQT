// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2019 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include "filter.h"
#include "filters.h"
#include <vd2/plugin/vdvideofilt.h>
#include <vd2/VDXFrame/VideoFilter.h>
#include <vd2/VDFilters/VFList.h>

extern const VDXFilterDefinition
#ifdef _DEBUG
	filterDef_debugerror,
	filterDef_debugcrop,
	filterDef_showinfo,
#endif
	filterDef_curves,
	filterDef_resize,
	filterDef_canvas,
	filterDef_fill,
	filterDef_test;

extern FilterDefinition
	filterDef_levels,
	filterDef_logo,
	filterDef_convolute,
	filterDef_emboss
	;

static const FilterDefinition *const builtin_filters[]={
	&filterDef_fill,
	&filterDef_resize,
	&filterDef_canvas,
	&filterDef_levels,
	&filterDef_logo,
//	&filterDef_curves,
	&filterDef_convolute,
	&filterDef_emboss,

#ifdef _DEBUG
	&filterDef_debugerror,
	&filterDef_debugcrop,
	&filterDef_showinfo,
	&filterDef_test,
#endif

	NULL
};

void InitBuiltinFilters() {
	const FilterDefinition *cur, *const *cpp;

	VDXVideoFilter::SetAPIVersion(VIRTUALDUB_FILTERDEF_VERSION);

	cpp = builtin_filters;
	while(cur = *cpp++)
		FilterAddBuiltin(cur);

	cpp = VDVFGetList();
	while(cur = *cpp++)
		FilterAddBuiltin(cur);
}

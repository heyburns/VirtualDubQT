// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_FILTER_H
#define f_FILTER_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/plugin/vdvideofilt.h>

typedef VDXFilterDefinition		FilterDefinition;
typedef VDXFilterActivation		FilterActivation;
typedef VDXFilterFunctions		FilterFunctions;
typedef IVDXScriptInterpreter	IScriptInterpreter;
typedef VDXScriptValue			CScriptValue;
typedef VDXScriptObject			CScriptObject;
typedef VDXScriptFunctionDef	ScriptFunctionDef;
typedef VDXScriptFunctionPtr	ScriptFunctionPtr;
typedef IVDXFilterPreview		IFilterPreview;
typedef IVDXFilterPreview2		IVDFilterPreview2;

#endif

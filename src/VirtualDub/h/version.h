// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2008 Avery Lee
// Copyright (C) 2016-2018 Anton Shekhovtsov
// Copyright (C) 2024-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VERSION_H
#define f_VD2_VERSION_H

#ifdef _MSC_VER
	#pragma once
#endif

#ifndef f_VD2_SYSTEM_VDTYPES_H
	#include <vd2/system/vdtypes.h>
#endif

#include "../h/Version_num.h"

#define VD_WIDE_STRING_CONST2(name) L ## name
#define VD_WIDE_STRING_CONST(name) VD_WIDE_STRING_CONST2(name)

#define VD_PROGRAM_NAMEA		"VirtualDub2"
#define VD_PROGRAM_NAMEW		VD_WIDE_STRING_CONST(VD_PROGRAM_NAMEA)

#define VD_PROGRAM_VERSIONA		VERSION_STR
#define VD_PROGRAM_VERSIONW		VD_WIDE_STRING_CONST(VD_PROGRAM_VERSIONA)

#define VD_PROGRAM_REV_HASHW	VD_WIDE_STRING_CONST(MAKE_STR(REV_HASH))

#define VD_PROGRAM_SPECIAL_BUILDA	""
#define VD_PROGRAM_SPECIAL_BUILDW	VD_WIDE_STRING_CONST(VD_PROGRAM_SPECIAL_BUILDA)

#ifdef _DEBUG
	#define VD_PROGRAM_CONFIGA	"debug"
#else
	#define VD_PROGRAM_CONFIGA	""
#endif

#define VD_PROGRAM_CONFIGW			VD_WIDE_STRING_CONST(VD_PROGRAM_CONFIGA)

#if defined(_M_AMD64)
	#define VD_PROGRAM_PLATFORM_NAMEW		L"x64"
#else
	#define VD_PROGRAM_PLATFORM_NAMEW		L"x86"
#endif
#define VD_PROGRAM_EXEFILE_NAMEA		"VirtualDub2.exe"
#define VD_PROGRAM_CLIEXE_NAMEA			"vdub2.exe"

#endif

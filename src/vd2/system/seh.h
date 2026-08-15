// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_SEH_H
#define f_VD2_SYSTEM_SEH_H

#include <vd2/system/vdtypes.h>

//////////////////////////////////////////////////////////////////////////////
// Structured Exception Handling (SEH) macros.
//
// These are used for memory access operations that may be possibly invalid
// and must be guarded. This is currently only supported on Win32/Win64
// platforms with the VC++ compiler, since GCC does not currently support SEH.
//
// For cases where a memcpy() is the guarded operation, the VDMemcpyGuarded()
// function should be used instead.
//
//////////////////////////////////////////////////////////////////////////////

#if defined(VD_COMPILER_MSVC) && defined(_WIN32)
	#include <excpt.h>

	#define vd_seh_guard_try		__try
	#define vd_seh_guard_except		__except(GetExceptionCode() == STATUS_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
#else
	#define vd_seh_guard_try		if (true)
	#define vd_seh_guard_except		else
#endif

#endif


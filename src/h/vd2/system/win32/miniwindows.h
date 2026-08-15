// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_WIN32_MINIWINDOWS_H
#define f_VD2_SYSTEM_WIN32_MINIWINDOWS_H

#define VDZCALLBACK __stdcall

#ifndef _WIN64
	#ifdef VD_COMPILER_MSVC
		typedef __w64 int		VDZINT_PTR;
		typedef __w64 unsigned	VDZUINT_PTR;
		typedef __w64 long		VDZLONG_PTR;
	#else
		typedef int			VDZINT_PTR;
		typedef unsigned	VDZUINT_PTR;
		typedef long		VDZLONG_PTR;
	#endif
#else
	typedef __int64				VDZINT_PTR;
	typedef unsigned __int64	VDZUINT_PTR;
	typedef __int64				VDZLONG_PTR;
#endif

typedef struct HWND__	*VDZHWND;
typedef struct HDC__	*VDZHDC;
typedef struct HKEY__	*VDZHKEY;
typedef unsigned		VDZUINT;
typedef unsigned short	VDZWORD;
typedef unsigned long	VDZDWORD;
typedef VDZUINT_PTR		VDZWPARAM;
typedef VDZLONG_PTR		VDZLPARAM;
typedef VDZLONG_PTR		VDZLRESULT;
typedef struct HDROP__	*VDZHDROP;
typedef struct HACCEL__	*VDZHACCEL;

typedef VDZWORD			VDZATOM;

#endif

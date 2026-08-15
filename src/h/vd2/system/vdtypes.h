// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_VDTYPES_H
#define f_VD2_SYSTEM_VDTYPES_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <algorithm>
#include <stdio.h>
#include <stdarg.h>
#include <new>

#ifndef NULL
#define NULL 0
#endif

///////////////////////////////////////////////////////////////////////////
//
//	compiler detection
//
///////////////////////////////////////////////////////////////////////////

#ifndef VD_COMPILER_DETECTED
	#define VD_COMPILER_DETECTED

	#if defined(_MSC_VER)
		#define VD_COMPILER_MSVC	_MSC_VER

		#if _MSC_VER >= 1900
			#define VD_COMPILER_MSVC_1900_OR_LATER 1
		#else
			#error MSVC 19.00 (Visual Studio 2015 v14.0) compiler or newer required. 
		#endif
	#elif defined(__GNUC__)
		#define VD_COMPILER_GCC
		#if defined(__MINGW32__) || defined(__MINGW64__)
			#define VD_COMPILER_GCC_MINGW
		#endif
	#endif
#endif

#ifndef VD_CPU_DETECTED
	#define VD_CPU_DETECTED

	#if defined(_M_AMD64)
		#define VD_CPU_AMD64	1
	#elif defined(_M_IX86) || defined(__i386__)
		#define VD_CPU_X86		1
	#elif defined(_M_ARM)
		#define VD_CPU_ARM
	#endif
#endif

///////////////////////////////////////////////////////////////////////////
//
//	types
//
///////////////////////////////////////////////////////////////////////////

#ifndef VD_STANDARD_TYPES_DECLARED
	#if defined(_MSC_VER)
		typedef signed __int64		sint64;
		typedef unsigned __int64	uint64;
	#elif defined(__GNUC__)
		typedef signed long long	sint64;
		typedef unsigned long long	uint64;
	#endif
	typedef signed int			sint32;
	typedef unsigned int		uint32;
	typedef signed short		sint16;
	typedef unsigned short		uint16;
	typedef signed char			sint8;
	typedef unsigned char		uint8;

	typedef sint64				int64;
	typedef sint32				int32;
	typedef sint16				int16;
	typedef sint8				int8;

	#ifdef _M_AMD64
		typedef sint64 sintptr;
		typedef uint64 uintptr;
	#else
		#if _MSC_VER
			typedef __w64 sint32 sintptr;
			typedef __w64 uint32 uintptr;
		#else
			typedef sint32 sintptr;
			typedef uint32 uintptr;
		#endif
	#endif
#endif

#if defined(_MSC_VER)
	#define VD64(x) x##i64
#elif defined(__GNUC__)
	#define VD64(x) x##ll
#else
	#error Please add an entry for your compiler for 64-bit constant literals.
#endif

	
#define VDAPIENTRY			__cdecl

typedef int64 VDTime;
typedef int64 VDPosition;
typedef	struct __VDGUIHandle *VDGUIHandle;

// enforce wchar_t under Visual C++

#if defined(_MSC_VER) && !defined(_WCHAR_T_DEFINED)
	#include <ctype.h>
#endif

///////////////////////////////////////////////////////////////////////////
//
//	allocation
//
///////////////////////////////////////////////////////////////////////////

#define new_nothrow new(std::nothrow)

///////////////////////////////////////////////////////////////////////////
//
//	compiler fixes
//
///////////////////////////////////////////////////////////////////////////

#define vdfor for

///////////////////////////////////////////////////////////////////////////
//
//	attribute support
//
///////////////////////////////////////////////////////////////////////////

#if defined(VD_COMPILER_MSVC)
	#define VDINTERFACE			__declspec(novtable)
	#define VDNORETURN			__declspec(noreturn)
	#define VDPUREFUNC

	#if VD_COMPILER_MSVC >= 1400
		#define VDRESTRICT		__restrict
	#else
		#define VDRESTRICT
	#endif

	#define VDNOINLINE			__declspec(noinline)
	#define VDFORCEINLINE		__forceinline
	#define VDALIGN(alignment)	__declspec(align(alignment))
#elif defined(VD_COMPILER_GCC)
	#define VDINTERFACE
	#define VDNORETURN			__attribute__((noreturn))
	#define VDPUREFUNC			__attribute__((pure))
	#define VDRESTRICT			__restrict
	#define VDNOINLINE			__attribute__((noinline))
	#define VDFORCEINLINE		inline __attribute__((always_inline))
	#define VDALIGN(alignment)	__attribute__((aligned(alignment)))
#else
	#define VDINTERFACE
	#define VDNORETURN
	#define VDPUREFUNC
	#define VDRESTRICT
	#define VDFORCEINLINE
	#define VDALIGN(alignment)
#endif

///////////////////////////////////////////////////////////////////////////
//
//	debug support
//
///////////////////////////////////////////////////////////////////////////

enum VDAssertResult {
	kVDAssertBreak,
	kVDAssertContinue,
	kVDAssertIgnore
};

extern VDAssertResult VDAssert(const char *exp, const char *file, int line);
extern VDAssertResult VDAssertPtr(const char *exp, const char *file, int line);
extern void VDDebugPrint(const char* format, ...);
extern void VDDebugPrint(const wchar_t* format, ...);

#if defined(_MSC_VER)
	#define VDBREAK		__debugbreak()
#elif defined(__GNUC__)
	#define VDBREAK		__asm__ volatile ("int3" : : )
#else
	#define VDBREAK		*(volatile char *)0 = *(volatile char *)0
#endif


#ifdef _DEBUG

	namespace {
		template<int line>
		struct VDAssertHelper {
			VDAssertHelper(const char *exp, const char *file) {
				if (!sbAssertDisabled)
					switch(VDAssert(exp, file, line)) {
					case kVDAssertBreak:
						VDBREAK;
						break;
					case kVDAssertIgnore:
						sbAssertDisabled = true;
						break;
					}
			}

			static bool sbAssertDisabled;
		};

		template<int lineno>
		bool VDAssertHelper<lineno>::sbAssertDisabled;

		template<int lineno>
		struct VDAssertHelper2 { static bool sDisabled; };

		template<int lineno>
		bool VDAssertHelper2<lineno>::sDisabled;
	}

	#define VDASSERT(exp)		if (!VDAssertHelper2<__LINE__>::sDisabled) if (exp); else switch(VDAssert   (#exp, __FILE__, __LINE__)) { case kVDAssertBreak: VDBREAK; break; case kVDAssertIgnore: VDAssertHelper2<__LINE__>::sDisabled = true; } else ((void)0)
	#define VDASSERTPTR(exp) 	if (!VDAssertHelper2<__LINE__>::sDisabled) if (exp); else switch(VDAssertPtr(#exp, __FILE__, __LINE__)) { case kVDAssertBreak: VDBREAK; break; case kVDAssertIgnore: VDAssertHelper2<__LINE__>::sDisabled = true; } else ((void)0)
	#define VDVERIFY(exp)		if (exp); else if (!VDAssertHelper2<__LINE__>::sDisabled) switch(VDAssert   (#exp, __FILE__, __LINE__)) { case kVDAssertBreak: VDBREAK; break; case kVDAssertIgnore: VDAssertHelper2<__LINE__>::sDisabled = true; } else ((void)0)
	#define VDVERIFYPTR(exp) 	if (exp); else if (!VDAssertHelper2<__LINE__>::sDisabled) switch(VDAssertPtr(#exp, __FILE__, __LINE__)) { case kVDAssertBreak: VDBREAK; break; case kVDAssertIgnore: VDAssertHelper2<__LINE__>::sDisabled = true; } else ((void)0)
	#define VDASSERTCT(exp)		(void)sizeof(int[(exp)?1:-1])

	#define VDINLINEASSERT(exp)			((exp)||(VDAssertHelper<__LINE__>(#exp, __FILE__),false))
	#define VDINLINEASSERTFALSE(exp)	((exp)&&(VDAssertHelper<__LINE__>("!("#exp")", __FILE__),true))

	#define NEVER_HERE			do { if (VDAssert( "[never here]", __FILE__, __LINE__ )) VDBREAK; __assume(false); } while(false)
	#define	VDNEVERHERE			do { if (VDAssert( "[never here]", __FILE__, __LINE__ )) VDBREAK; __assume(false); } while(false)

	#define VDDEBUG				VDDebugPrint

#else

	#if defined(_MSC_VER)
		#ifndef _M_AMD64
			#define VDASSERT(exp)		__assume(!!(exp))
			#define VDASSERTPTR(exp)	__assume(!!(exp))
		#else
			#define VDASSERT(exp)		__noop(exp)
			#define VDASSERTPTR(exp)	__noop(exp)
		#endif
	#elif defined(__GNUC__)
		#define VDASSERT(exp)		__builtin_expect(0 != (exp), 1)
		#define VDASSERTPTR(exp)	__builtin_expect(0 != (exp), 1)
	#endif

	#define VDVERIFY(exp)		(exp)
	#define VDVERIFYPTR(exp)	(exp)
	#define VDASSERTCT(exp)

	#define VDINLINEASSERT(exp)	(exp)
	#define VDINLINEASSERTFALSE(exp)	(exp)

	#if defined(VD_COMPILER_MSVC)
		#define NEVER_HERE			__assume(false)
		#define	VDNEVERHERE			__assume(false)
	#else
		#define NEVER_HERE			VDASSERT(false)
		#define	VDNEVERHERE			VDASSERT(false)
	#endif

	#define VDDEBUG(...)			__noop

#endif

#define VDDEBUG2			VDDebugPrint

// TODO macros
//
// These produce a diagnostic during compilation that indicate a TODO for
// later:
//
//		#pragma message(__TODO__ "Fix this.)
//		#vdpragma_TODO("Fix this.")

#define vdpragma_TODO2(x)	#x
#define vdpragma_TODO1(x)	vdpragma_TODO2(x)
#define vdpragma_TODO0		__FILE__ "(" vdpragma_TODO1(__LINE__) ") : TODO: "

#ifdef _MSC_VER
#define vdpragma_TODO(x)		message(vdpragma_TODO0 x)
#else
#define vdpragma_TODO(x)
#endif

// BS macros
//
// These tag code that is not meant to go into a final build.

#define vdpragma_BS2(x)	#x
#define vdpragma_BS1(x)	vdpragma_BS2(x)
#define vdpragma_BS0		__FILE__ "(" vdpragma_BS1(__LINE__) ") : BS: "

#ifdef _MSC_VER
#define vdpragma_BS(x)		message(vdpragma_BS0 x)
#else
#define vdpragma_BS(x)
#endif

///////////////////////////////////////////////////////////////////////////
//
// Object scope macros
//
// vdobjectscope() allows you to define a construct where an object is
// constructed and live only within the controlled statement.  This is
// used for vdsynchronized (thread.h) and protected scopes below.
// It relies on a strange quirk of C++ regarding initialized objects
// in the condition of a selection statement and also horribly abuses
// the switch statement, generating rather good code in release builds.
// The catch is that the controlled object must implement a conversion to
// bool returning false and must only be initialized with one argument (C
// syntax).
//
// Unfortunately, handy as this macro is, it is also damned good at
// breaking compilers.  For a start, declaring an object with a non-
// trivial destructor in a switch() kills both VC6 and VC7 with a C1001.
// The bug is fixed in VC8 (MSC 14.00).
//
// A somewhat safer alternative is the for() statement, along the lines
// of:
//
// switch(bool v=false) case 0: default: for(object_def; !v; v=true)
//
// This avoids the conversion operator but unfortunately usually generates
// an actual loop in the output.

#define vdobjectscope(object_def) switch(object_def) case 0: default:

#endif

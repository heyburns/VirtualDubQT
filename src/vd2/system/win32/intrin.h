// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2011 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_WIN32_INTRIN_H
#define f_VD2_SYSTEM_WIN32_INTRIN_H

#pragma once

// The Windows SDK conflicts with the VS2005 declaration of a couple
// of intrinsics starting with the Vista SDK. The conflict is between
// intrin.h and winnt.h. To work around this, we wrap intrin.h and
// rename its declaration.
#pragma push_macro("_interlockedbittestandset")
#pragma push_macro("_interlockedbittestandreset")
#pragma push_macro("_interlockedbittestandset64")
#pragma push_macro("_interlockedbittestandreset64")

#define _interlockedbittestandset _interlockedbittestandset_vc
#define _interlockedbittestandreset _interlockedbittestandreset_vc
#define _interlockedbittestandset64 _interlockedbittestandset64_vc
#define _interlockedbittestandreset64 _interlockedbittestandreset64_vc

#ifdef _MSC_VER
	#include <intrin.h>
#else
	#include <emmintrin.h>
#endif

#pragma pop_macro("_interlockedbittestandreset64")
#pragma pop_macro("_interlockedbittestandset64")
#pragma pop_macro("_interlockedbittestandreset")
#pragma pop_macro("_interlockedbittestandset")

#endif

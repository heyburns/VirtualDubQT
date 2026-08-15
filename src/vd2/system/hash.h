// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_HASH_H
#define f_VD2_SYSTEM_HASH_H

#ifdef _MSC_VER
	#pragma once
#endif

#ifndef f_VD2_SYSTEM_VDTYPES_H
	#include <vd2/system/vdtypes.h>
#endif

// Case-sensitive string hashes

uint32 VDHashString32(const char *s);
uint32 VDHashString32(const char *s, uint32 len);
uint32 VDHashString32(const wchar_t *s);
uint32 VDHashString32(const wchar_t *s, uint32 len);

// Case-insensitive, culture-invariant string hashes

uint32 VDHashString32I(const char *s);
uint32 VDHashString32I(const char *s, uint32 len);
uint32 VDHashString32I(const wchar_t *s);
uint32 VDHashString32I(const wchar_t *s, uint32 len);

#endif

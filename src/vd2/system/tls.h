// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_SYSTEM_TLS_H
#define f_SYSTEM_TLS_H

#include <ctype.h>

void VDInitThreadData(const char *pszThreadName);
void VDDeinitThreadData();

typedef void (*VDThreadInitHook)(bool init, const char *threadName);

void VDSetThreadInitHook(VDThreadInitHook pHook);

#endif

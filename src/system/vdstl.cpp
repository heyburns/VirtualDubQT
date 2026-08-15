// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2008 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/Error.h>

#include <vd2/system/vdstl.h>

void VDNORETURN vdallocator_base::throw_oom(size_t n, size_t elsize) {
	size_t nbytes = ~(size_t)0;

	if (n <= nbytes / elsize)
		nbytes = n * elsize;

	throw MyMemoryError(nbytes);
}

// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2011 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_PROCESS_H
#define f_VD2_SYSTEM_PROCESS_H

/// Simple asynchronous program launch -- no arguments. The current directory
/// defaults to the windows directory to prevent inadvernent locking.
void VDLaunchProgram(const wchar_t *path);

#endif	// f_VD2_SYSTEM_PROCESS_H

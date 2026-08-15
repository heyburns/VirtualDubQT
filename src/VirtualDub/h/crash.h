// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_CRASH_H
#define f_CRASH_H

#ifdef f_CRASH_CPP
#define EXTERN
#else
#define EXTERN extern
#endif

struct VirtualDubCheckpoint {
	const char *file;
	int line;

	inline void set(const char *f, int l) { file=f; line=l; }
};

#define CHECKPOINT_COUNT		(16)

struct VirtualDubThreadState {
	const char				*pszThreadName;
	unsigned long			dwThreadId;
	void *					hThread;

	VirtualDubCheckpoint	cp[CHECKPOINT_COUNT];
	int						nNextCP;
};

EXTERN __declspec(thread) VirtualDubThreadState g_PerThreadState;

#define VDCHECKPOINT (g_PerThreadState.cp[g_PerThreadState.nNextCP++&(CHECKPOINT_COUNT-1)].set(__FILE__, __LINE__))

void VDThreadInitHandler(bool, const char *);
void VDSetCrashDumpPath(const wchar_t *s);

#endif

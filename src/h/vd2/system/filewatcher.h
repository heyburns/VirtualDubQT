// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 2013 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_FILEWATCHER_H
#define f_VD2_SYSTEM_FILEWATCHER_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>

class VDFunctionThunk;

class IVDFileWatcherCallback {
public:
	virtual bool OnFileUpdated(const wchar_t *path) = 0;
};

class VDFileWatcher {
public:
	VDFileWatcher() = default;
	~VDFileWatcher();

	bool IsActive() const;

	void Init(const wchar_t *file, IVDFileWatcherCallback *cb);
	void InitDir(const wchar_t *path, bool subdirs, IVDFileWatcherCallback *cb);
	void Shutdown();

	bool Wait(uint32 delay = 0xFFFFFFFFU);

protected:
	void StaticTimerCallback(void *, unsigned, unsigned, unsigned long);

	void *mChangeHandle   = INVALID_HANDLE_VALUE;
	uint64 mLastWriteTime = 0;
	bool mbWatchDir       = false;
	VDStringW mPath;

	IVDFileWatcherCallback* mpCB = nullptr;

	bool mbRepeatRequested = false;
	bool mbThunksInited    = false;
	VDFunctionThunk* mpThunk = nullptr;
	uint32 mTimerId = 0;
};

#endif

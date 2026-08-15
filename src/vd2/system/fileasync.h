// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2017 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_FILEASYNC_H
#define f_VD2_SYSTEM_FILEASYNC_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>
#include <vd2/system/file.h>

class VDRTProfileChannel;

class IVDFileAsync {
public:
	virtual ~IVDFileAsync() {}
	virtual void SetPreemptiveExtend(bool b) = 0;
	virtual bool IsPreemptiveExtendActive() = 0;
	virtual bool IsOpen() = 0;
	virtual void Open(const wchar_t *pszFilename, uint32 count, uint32 bufferSize) = 0;
	virtual void OpenPipe(VDFileHandle h, uint32 count, uint32 bufferSize) = 0;
	virtual void Close() = 0;
	virtual void FastWrite(const void *pData, uint32 bytes) = 0;
	virtual void FastWriteEnd() = 0;
	virtual void Write(sint64 pos, const void *pData, uint32 bytes) = 0;
	virtual bool Extend(sint64 pos) = 0;
	virtual void Truncate(sint64 pos) = 0;
	virtual void SafeTruncateAndClose(sint64 pos) = 0;
	virtual sint64 GetFastWritePos() = 0;
	virtual sint64 GetSize() = 0;
};

IVDFileAsync *VDCreateFileAsync();

#endif

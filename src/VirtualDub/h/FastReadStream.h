// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VIRTUALDUB_FASTREADSTREAM_H
#define f_VIRTUALDUB_FASTREADSTREAM_H

#include <windows.h>

class FastReadStreamHeader;

class FastReadStream {
public:
	FastReadStream(HANDLE hFile, long lBlockCount, long lBlockSize);
	FastReadStream(int iFile, long lBlockCount, long lBlockSize);
	~FastReadStream();

	bool Ready();
	long Read(int stream, __int64 i64Pos, void *pBuffer, long lBytes);
	void Flush();

private:
	HANDLE hFile;
	int iFile;
	long lBlockCount;
	long lBlockSize;
	long lHistory;

	FastReadStreamHeader *pHeaders;
	void *pBuffer;

	void _Init(long lBlockCount, long lBlockSize);
	int _PickVictim(int stream);
	int _Commit(int stream, __int64 i64BlockNo);
};

#endif

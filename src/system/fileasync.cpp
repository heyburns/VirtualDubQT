// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2017 Anton Shekhovtsov
// Copyright (C) 2023-2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <windows.h>
#include <malloc.h>
#include <vd2/system/Error.h>

#include <vd2/system/file.h>
#include <vd2/system/fileasync.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/VDRingBuffer.h>
#include <vd2/system/w32assist.h>

///////////////////////////////////////////////////////////////////////////
//
//	VDFileAsync - Windows NT implementation
//
///////////////////////////////////////////////////////////////////////////

struct VDFileAsyncNTBuffer : public OVERLAPPED {
	bool    mbActive  = false;
	bool    mbPending = false;
	uint32  mLength   = 0;

	VDFileAsyncNTBuffer() { hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); }
	~VDFileAsyncNTBuffer() { if (hEvent) CloseHandle(hEvent); }
};

class VDFileAsyncNT : public IVDFileAsync, private VDThread {
public:
	VDFileAsyncNT();
	~VDFileAsyncNT();

	void SetPreemptiveExtend(bool b) { mbPreemptiveExtend = b; }
	bool IsPreemptiveExtendActive() { return mbPreemptiveExtend; }

	bool IsOpen() { return mhFileSlow != INVALID_HANDLE_VALUE; }

	void Open(const wchar_t *pszFilename, uint32 count, uint32 bufferSize);
	void OpenPipe(VDFileHandle h, uint32 count, uint32 bufferSize);
	void Close();
	void FastWrite(const void *pData, uint32 bytes);
	void FastWriteEnd();
	void Write(sint64 pos, const void *pData, uint32 bytes);
	bool Extend(sint64 pos);
	void Truncate(sint64 pos);
	void SafeTruncateAndClose(sint64 pos);
	sint64 GetSize();
	sint64 GetFastWritePos() { return mClientFastPointer; }

protected:
	void WriteZero(sint64 pos, uint32 bytes);
	void Seek(sint64 pos);
	bool SeekNT(sint64 pos);
	void ThrowError();
	void ThreadRun();

	HANDLE		mhFileSlow  = INVALID_HANDLE_VALUE;
	HANDLE		mhFileFast  = INVALID_HANDLE_VALUE;
	uint32		mBlockSize  = 0;
	uint32		mBlockCount = 0;
	uint32		mBufferSize = 0;
	uint32		mSectorSize = 0;

	enum {
		kStateNormal,
		kStateFlush,
		kStateAbort
	};
	VDAtomicInt	mState = 0;

	VDSignal	mReadOccurred;
	VDSignal	mWriteOccurred;

	uint32		mWriteOffset = 0;
	VDAtomicInt	mBufferLevel = 0;
	sint64		mClientSlowPointer = 0;
	sint64		mClientFastPointer = 0;
	sint64		mFastPointer = 0;

	volatile bool	mbPreemptiveExtend = false;

	vdautoarrayptr<VDFileAsyncNTBuffer>	mpBlocks;

	vdblock<char, VDFileUnbufferAllocator<char> >	mBuffer;

	VDAtomicPtr<MyError> mpError = nullptr;
	VDStringW	mFilename;
	bool mPipeMode = false;
};

VDFileAsyncNT::VDFileAsyncNT()
{
}

VDFileAsyncNT::~VDFileAsyncNT() {
	Close();
}

void VDFileAsyncNT::Open(const wchar_t *pszFilename, uint32 count, uint32 bufferSize) {
	try {
		mFilename = pszFilename;

		mhFileSlow = CreateFileW(pszFilename, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (mhFileSlow == INVALID_HANDLE_VALUE) {
			throw MyWin32Error(L"Unable to open file \"%s\" for write: %%s", GetLastError(), mFilename.c_str());
		}

		mhFileFast = CreateFileW(pszFilename, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);
		if (mhFileFast == INVALID_HANDLE_VALUE)
			mhFileFast = CreateFileW(pszFilename, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OVERLAPPED, NULL);

		mSectorSize = 4096;		// guess for now... proper way would be GetVolumeMountPoint() followed by GetDiskFreeSpace().

		mBlockSize = bufferSize;
		mBlockCount = count;
		mBufferSize = mBlockSize * mBlockCount;

		mWriteOffset = 0;
		mBufferLevel = 0;

		mState = kStateNormal;

		if (mhFileFast != INVALID_HANDLE_VALUE) {
			mpBlocks = new VDFileAsyncNTBuffer[count];
			mBuffer.resize(count * bufferSize);
			ThreadStart();
		}
	} catch(const MyError&) {
		Close();
		throw;
	}
}

void VDFileAsyncNT::OpenPipe(VDFileHandle h, uint32 count, uint32 bufferSize) {
	try {
		mFilename = L"<anonymous pipe>";
		mPipeMode = true;

		HANDLE hProcess = GetCurrentProcess();
		if (!DuplicateHandle(hProcess, h, hProcess, &mhFileSlow, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
			throw MyWin32Error(L"Unable to open file \"%s\" for write: %%s", GetLastError(), mFilename.c_str());
		}

		mSectorSize = 4096;		// guess for now... proper way would be GetVolumeMountPoint() followed by GetDiskFreeSpace().

		mBlockSize = bufferSize;
		mBlockCount = count;
		mBufferSize = mBlockSize * mBlockCount;

		mWriteOffset = 0;
		mBufferLevel = 0;

		mState = kStateNormal;

		if (mhFileFast != INVALID_HANDLE_VALUE) {
			mpBlocks = new VDFileAsyncNTBuffer[count];
			mBuffer.resize(count * bufferSize);
			ThreadStart();
		}
	} catch(const MyError&) {
		Close();
		throw;
	}
}

void VDFileAsyncNT::Close() {
	mState = kStateAbort;
	mWriteOccurred.signal();
	ThreadWait();

	if (mpError) {
		delete mpError;
		mpError = NULL;
	}

	if (mhFileSlow != INVALID_HANDLE_VALUE) {
		CloseHandle(mhFileSlow);
		mhFileSlow = INVALID_HANDLE_VALUE;
	}
	if (mhFileFast != INVALID_HANDLE_VALUE) {
		CloseHandle(mhFileFast);
		mhFileFast = INVALID_HANDLE_VALUE;
	}

	mpBlocks = NULL;
}

void VDFileAsyncNT::FastWrite(const void *pData, uint32 bytes) {
	if (mhFileFast == INVALID_HANDLE_VALUE) {
		if (pData)
			Write(mClientFastPointer, pData, bytes);
		else
			WriteZero(mClientFastPointer, bytes);
	} else {
		if (mpError)
			ThrowError();

		uint32 bytesLeft = bytes;
		while(bytesLeft) {
			uint32 actual = mBufferSize - mBufferLevel;

			if (actual > bytesLeft)
				actual = bytesLeft;

			if (mWriteOffset + actual > mBufferSize)
				actual = mBufferSize - mWriteOffset;

			if (!actual) {
				mReadOccurred.wait();
				if (mpError)
					ThrowError();
				continue;
			}

			if (pData) {
				memcpy(&mBuffer[mWriteOffset], pData, actual);
				pData = (const char *)pData + actual;
			} else {
				memset(&mBuffer[mWriteOffset], 0, actual);
			}

			uint32 oldWriteOffset = mWriteOffset;
			mWriteOffset += actual;
			if (mWriteOffset >= mBufferSize)
				mWriteOffset = 0;
			mBufferLevel += actual;

			// only bother signaling if the write offset crossed a block boundary
			if (oldWriteOffset % mBlockSize + actual >= mBlockSize) {
				mWriteOccurred.signal();
				if (mpError)
					ThrowError();
			}

			bytesLeft -= actual;
		}
	}
		
	mClientFastPointer += bytes;
}

void VDFileAsyncNT::FastWriteEnd() {
	if (mhFileFast != INVALID_HANDLE_VALUE) {
		if (!mPipeMode) FastWrite(NULL, mSectorSize - 1);
		mState = kStateFlush;
		mWriteOccurred.signal();
		ThreadWait();
	}

	if (mpError)
		ThrowError();
}

void VDFileAsyncNT::Write(sint64 pos, const void *p, uint32 bytes) {
	Seek(pos);

	DWORD dwActual;
	if (!WriteFile(mhFileSlow, p, bytes, &dwActual, NULL) || (mClientSlowPointer += dwActual),(dwActual != bytes)) {
		throw MyWin32Error(L"Write error occurred on file \"%s\": %%s", GetLastError(), mFilename.c_str());
	}
}

void VDFileAsyncNT::WriteZero(sint64 pos, uint32 bytes) {
	uint32 bufsize = bytes > 2048 ? 2048 : bytes;
	void *p = _malloca(bufsize);
	memset(p, 0, bufsize);

	while(bytes > 0) {
		uint32 tc = bytes > 2048 ? 2048 : bytes;

		Write(pos, p, tc);
		pos += tc;
		bytes -= tc;
	}
	_freea(p);
}

bool VDFileAsyncNT::Extend(sint64 pos) {
	return SeekNT(pos) && SetEndOfFile(mhFileSlow);
}

void VDFileAsyncNT::Truncate(sint64 pos) {
	Seek(pos);
	if (!SetEndOfFile(mhFileSlow)) {
		throw MyWin32Error(L"I/O error on file \"%s\": %%s", GetLastError(), mFilename.c_str());
	}
}

void VDFileAsyncNT::SafeTruncateAndClose(sint64 pos) {
	if (isThreadAttached()) {
		mState = kStateAbort;
		mWriteOccurred.signal();
		ThreadWait();

		if (mpError) {
			delete mpError;
			mpError = NULL;
		}
	}

	if (mhFileSlow != INVALID_HANDLE_VALUE) {
		Extend(pos);
		Close();
	}
}

sint64 VDFileAsyncNT::GetSize()
{
	LARGE_INTEGER filesize;
	BOOL result = GetFileSizeEx(mhFileSlow, &filesize);

	if (!result) {
		DWORD err = GetLastError();
		throw MyWin32Error(L"I/O error on file \"%s\": %%s", err, mFilename.c_str());
	}

	return filesize.QuadPart;
}

void VDFileAsyncNT::Seek(sint64 pos) {
	if (!SeekNT(pos)) {
		throw MyWin32Error(L"I/O error on file \"%s\": %%s", GetLastError(), mFilename.c_str());
	}
}

bool VDFileAsyncNT::SeekNT(sint64 pos)
{
	if (mClientSlowPointer == pos) {
		return true;
	}

	LARGE_INTEGER filepos;
	filepos.QuadPart = pos;
	BOOL result = SetFilePointerEx(mhFileSlow, filepos, &filepos, FILE_BEGIN);

	if (!result) {
		return false;
	}

	mClientSlowPointer = filepos.QuadPart;
	return true;
}

void VDFileAsyncNT::ThrowError() {
	MyError *e = mpError.xchg(NULL);

	if (e) {
		if (mhFileFast != INVALID_HANDLE_VALUE) {
			CloseHandle(mhFileFast);
			mhFileFast = INVALID_HANDLE_VALUE;
		}

		MyError tmp;
		tmp.TransferFrom(*e);
		delete e;
		throw tmp;
	}
}

void VDFileAsyncNT::ThreadRun() {
	int requestHead = 0;
	int requestTail = 0;
	int requestCount = mBlockCount;
	uint32 pendingLevel = 0;
	uint32 readOffset = 0;
	bool	bPreemptiveExtend = mbPreemptiveExtend;
	sint64	currentSize;

	try {
		if (!VDGetFileSizeW32(mhFileFast, currentSize)) {
			throw MyWin32Error(L"I/O error on file \"%s\": %%s", GetLastError(), mFilename.c_str());
		}

		for(;;) {
			int state = mState;

			if (state == kStateAbort) {
				CancelIo(mhFileFast);

				// Wait for any pending blocks to complete.
				for(int i=0; i<requestCount; ++i) {
					VDFileAsyncNTBuffer& buf = mpBlocks[i];

					if (buf.mbActive) {
						WaitForSingleObject(buf.hEvent, INFINITE);
						buf.mbActive = false;
					}
				}
				break;
			}

			uint32 actual = mBufferLevel - pendingLevel;
			VDASSERT((int)actual >= 0);
			if (readOffset + actual > mBufferSize)
				actual = mBufferSize - readOffset;

			if (actual < mBlockSize) {
				if (state == kStateNormal || actual < mSectorSize) {
					// check for blocks that have completed
					bool blocksCompleted = false;
					for(;;) {
						VDFileAsyncNTBuffer& buf = mpBlocks[requestTail];

						if (!buf.mbActive) {
							if (state == kStateFlush)
								goto all_done;

							if (!blocksCompleted) {
								// wait for further writes
								mWriteOccurred.wait();
							}
							break;
						}

						if (buf.mbPending) {
							HANDLE h[2] = {buf.hEvent, mWriteOccurred.getHandle()};
							DWORD waitResult = WaitForMultipleObjects(2, h, FALSE, INFINITE);

							if (waitResult == WAIT_OBJECT_0+1)	// write pending
								break;

							DWORD dwActual;
							if (!GetOverlappedResult(mhFileFast, &buf, &dwActual, TRUE)) {
								throw MyWin32Error(L"Write error occurred on file \"%s\": %%s", GetLastError(), mFilename.c_str());
							}
						}

						buf.mbActive = false;

						blocksCompleted = true;

						if (++requestTail >= requestCount)
							requestTail = 0;

						mBufferLevel -= buf.mLength;
						pendingLevel -= buf.mLength;
						VDASSERT((int)mBufferLevel >= 0);
						VDASSERT((int)pendingLevel >= 0);

						mReadOccurred.signal();

					}

					continue;
				}

				VDASSERT(state == kStateFlush);

				actual &= ~(mSectorSize-1);

				VDASSERT(actual > 0);
			} else {
				actual = mBlockSize;

				if (bPreemptiveExtend) {
					sint64 checkpt = mFastPointer + mBlockSize + mBufferSize;

					if (checkpt > currentSize) {
						currentSize += mBufferSize;
						if (currentSize < checkpt)
							currentSize = checkpt;

						if (!VDSetFilePointerW32(mhFileFast, currentSize, FILE_BEGIN)
							|| !SetEndOfFile(mhFileFast))
							mbPreemptiveExtend = bPreemptiveExtend = false;
					}
				}
			}

			// Issue a write to OS
			VDFileAsyncNTBuffer& buf = mpBlocks[requestHead];

			VDASSERT(!buf.mbActive);

			DWORD dwActual;

			buf.Offset = (DWORD)mFastPointer;
			buf.OffsetHigh = (DWORD)((uint64)mFastPointer >> 32);
			buf.Internal = 0;
			buf.InternalHigh = 0;
			buf.mLength = actual;
			buf.mbPending = false;

			if (!WriteFile(mhFileFast, &mBuffer[readOffset], actual, &dwActual, &buf)) {
				if (GetLastError() != ERROR_IO_PENDING) {
					throw MyWin32Error(L"Write error occurred on file \"%s\": %%s", GetLastError(), mFilename.c_str());
				}

				buf.mbPending = true;
			}

			buf.mbActive = true;

			pendingLevel += actual;
			VDASSERT(pendingLevel <= (uint32)mBufferLevel);

			readOffset += actual;
			VDASSERT(readOffset <= mBufferSize);
			if (readOffset >= mBufferSize)
				readOffset = 0;

			mFastPointer += actual;

			if (++requestHead >= requestCount)
				requestHead = 0;
		}
all_done:
		;

	} catch(MyError& e) {
		MyError *p = new MyError;

		p->TransferFrom(e);
		delete mpError.xchg(p);
		mReadOccurred.signal();
	}
}

///////////////////////////////////////////////////////////////////////////

IVDFileAsync *VDCreateFileAsync()
{
	return new VDFileAsyncNT;
}

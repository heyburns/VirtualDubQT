// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2023-2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/Error.h>

#include <vd2/system/filesys.h>
#include <vd2/system/VDString.h>
#include <vd2/system/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

namespace {
	bool IsHardDrivePath(const wchar_t *path) {
		(void)path;
		return true;
	}
};

using namespace nsVDFile;

VDFile::VDFile(const char *pszFileName, uint32 flags)
	: mhFile(NULL)
{
	open_internal(VDTextAToW(pszFileName).c_str(), flags, true);
}

VDFile::VDFile(const wchar_t *pwszFileName, uint32 flags)
	: mhFile(NULL)
{
	open_internal(pwszFileName, flags, true);
}

VDFile::VDFile(VDFileHandle h)
	: mhFile(h)
{
	if (mhFile) {
		int fd = (int)(intptr_t)mhFile;
		mFilePosition = lseek(fd, 0, SEEK_CUR);
	}
}

VDFile::~VDFile()
{
	closeNT();
}

void VDFile::open(const char *pszFileName, uint32 flags)
{
	open_internal(VDTextAToW(pszFileName).c_str(), flags, true);
}

void VDFile::open(const wchar_t *pwszFilename, uint32 flags)
{
	open_internal(pwszFilename, flags, true);
}

bool VDFile::openNT(const wchar_t *pwszFilename, uint32 flags)
{
	return open_internal(pwszFilename, flags, false);
}

bool VDFile::open_internal(const wchar_t *pwszFilename, uint32 flags, bool throwOnError)
{
	close();

	mpFilename = _wcsdup(VDFileSplitPath(pwszFilename));
	if (!mpFilename) {
		if (!throwOnError) return false;
		throw MyMemoryError();
	}

	VDStringA pathUtf8 = VDTextWToA(pwszFilename);
	int oflags = 0;

	if ((flags & kReadWrite) == kReadWrite) {
		oflags |= O_RDWR;
	} else if (flags & kWrite) {
		oflags |= O_WRONLY;
	} else {
		oflags |= O_RDONLY;
	}

	uint32 creationType = flags & kCreationMask;
	switch(creationType) {
	case kOpenExisting:
		break;
	case kOpenAlways:
		oflags |= O_CREAT;
		break;
	case kCreateAlways:
		oflags |= O_CREAT | O_TRUNC;
		break;
	case kCreateNew:
		oflags |= O_CREAT | O_EXCL;
		break;
	case kTruncateExisting:
		oflags |= O_TRUNC;
		break;
	default:
		if (!throwOnError) return false;
		return false;
	}

	int fd = ::open(pathUtf8.c_str(), oflags, 0666);
	if (fd < 0) {
		mhFile = NULL;
		if (!throwOnError) return false;
		throw MyError("Cannot open file \"%s\"", pathUtf8.c_str());
	}

	mhFile = (VDFileHandle)(intptr_t)fd;
	mFilePosition = 0;
	return true;
}

bool VDFile::closeNT()
{
	if (mhFile) {
		int fd = (int)(intptr_t)mhFile;
		mhFile = NULL;
		if (::close(fd) != 0) {
			return false;
		}
	}
	return true;
}

void VDFile::close()
{
	if (!closeNT()) {
		throw MyError("Cannot close file");
	}
}

bool VDFile::truncateNT()
{
	if (!mhFile) return false;
	int fd = (int)(intptr_t)mhFile;
	return 0 == ::ftruncate(fd, mFilePosition);
}

void VDFile::truncate()
{
	if (!truncateNT()) {
		throw MyError("Cannot truncate file");
	}
}

bool VDFile::extendValidNT(sint64 pos)
{
	(void)pos;
	return true;
}

void VDFile::extendValid(sint64 pos)
{
	(void)pos;
}

bool VDFile::enableExtendValid()
{
	return true;
}

long VDFile::readData(void *buffer, long length)
{
	if (!mhFile) return 0;
	int fd = (int)(intptr_t)mhFile;
	ssize_t bytesRead = ::read(fd, buffer, length);
	if (bytesRead < 0) {
		throw MyError("Cannot read from file");
	}
	mFilePosition += bytesRead;
	return (long)bytesRead;
}

void VDFile::read(void *buffer, long length)
{
	if (length != readData(buffer, length)) {
		throw MyError("Cannot read from file: Premature end of file.");
	}
}

long VDFile::writeData(const void *buffer, long length)
{
	if (!mhFile) return 0;
	int fd = (int)(intptr_t)mhFile;
	ssize_t bytesWritten = ::write(fd, buffer, length);
	if (bytesWritten < 0) {
		throw MyError("Cannot write to file");
	}
	mFilePosition += bytesWritten;
	return (long)bytesWritten;
}

void VDFile::write(const void *buffer, long length)
{
	if (length != writeData(buffer, length)) {
		throw MyError("Cannot write to file: Unable to write all data.");
	}
}

bool VDFile::seekNT(sint64 newPos, eSeekMode mode)
{
	if (!mhFile) return false;
	int fd = (int)(intptr_t)mhFile;
	int whence = SEEK_SET;
	switch(mode) {
	case kSeekStart: whence = SEEK_SET; break;
	case kSeekCur:   whence = SEEK_CUR; break;
	case kSeekEnd:   whence = SEEK_END; break;
	}

	off64_t res = ::lseek64(fd, newPos, whence);
	if (res == (off64_t)-1) {
		return false;
	}
	mFilePosition = res;
	return true;
}

void VDFile::seek(sint64 newPos, eSeekMode mode)
{
	if (!seekNT(newPos, mode)) {
		throw MyError("Cannot seek within file");
	}
}

bool VDFile::skipNT(sint64 delta)
{
	return seekNT(delta, kSeekCur);
}

void VDFile::skip(sint64 delta)
{
	seek(delta, kSeekCur);
}

sint64 VDFile::size()
{
	if (!mhFile) return 0;
	int fd = (int)(intptr_t)mhFile;
	struct stat st;
	if (fstat(fd, &st) == 0) {
		return st.st_size;
	}
	return 0;
}

sint64 VDFile::tell()
{
	return mFilePosition;
}

bool VDFile::flushNT()
{
	if (!mhFile) return false;
	int fd = (int)(intptr_t)mhFile;
	return 0 == ::fsync(fd);
}

void VDFile::flush()
{
	if (!flushNT()) {
		throw MyError("Cannot flush file");
	}
}

bool VDFile::isOpen()
{
	return mhFile != NULL;
}

VDFileHandle VDFile::getRawHandle()
{
	return mhFile;
}

void *VDFile::AllocUnbuffer(size_t nBytes)
{
	void *ptr = nullptr;
	if (posix_memalign(&ptr, 4096, nBytes) != 0) {
		return nullptr;
	}
	return ptr;
}

void VDFile::FreeUnbuffer(void *p)
{
	free(p);
}

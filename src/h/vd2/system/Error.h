// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2019 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_ERROR_H
#define f_VD2_ERROR_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>

class MyError;

///////////////////////////////////////////////////////////////////////////
//	IVDAsyncErrorCallback
//
class IVDAsyncErrorCallback {
public:
	virtual bool OnAsyncError(MyError& e) = 0;
};

///////////////////////////////////////////////////////////////////////////
//	MyError
//
class MyError {
private:
	const MyError& operator=(const MyError&);		// protect against accidents

protected:
	wchar_t* mbuf = nullptr;

public:
	MyError();
	MyError(const MyError& err);
	MyError(const wchar_t* f, ...);
	MyError(const char* f, ...);
	~MyError();
	void clear();
	void assign(const MyError& e);
	void assign(const wchar_t* s);
	void setf(const wchar_t* f, ...);
	void setf(const char* f, ...);
	void vsetf(const wchar_t* f, va_list val);
	void vsetf(const char* f, va_list val);
	void post(struct HWND__ *hWndParent, const wchar_t* title) const;
	wchar_t* gets() const {
		return mbuf;
	}
	wchar_t* c_str() const {
		return mbuf;
	}
	bool empty() const { return !mbuf; }
	void discard();
	void swap(MyError& err);
	void TransferFrom(MyError& err);
};

class MyTextError : public MyError {
public:
	int line;
	int col;
};

class MyICError : public MyError {
public:
	MyICError(const char *s, uint32 icErr);
	MyICError(uint32 icErr, const char *format, ...);
};

class MyMMIOError : public MyError {
public:
	MyMMIOError(const char *s, uint32 icErr);
};

class MyAVIError : public MyError {
public:
	MyAVIError(const char *s, uint32 aviErr);
};

class MyMemoryError : public MyError {
public:
	MyMemoryError();
	MyMemoryError(size_t attemptedSize);
};

class MyWin32Error : public MyError {
public:
	MyWin32Error(const wchar_t* format, uint32 err, ...);
	MyWin32Error(const char* format, uint32 err, ...);

	uint32 GetWin32Error() const { return mWin32Error; }

protected:
	const uint32 mWin32Error;
};

class MyCrashError : public MyError {
public:
	MyCrashError(const char *format, uint32 dwExceptionCode);
};

class MyUserAbortError : public MyError {
public:
	MyUserAbortError();
};

class MyInternalError : public MyError {
public:
	MyInternalError(const wchar_t* format, ...);
	MyInternalError(const char* format, ...);
};

#endif

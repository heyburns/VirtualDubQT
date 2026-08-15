// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2012 Avery Lee, All Rights Reserved.
// Copyright (C) 2024-2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include "stdafx.h"
#include <vdwin32_shim.h>
#include <vd2/system/Error.h>

#ifndef ICERR_OK
#define ICERR_OK ((uint32)0L)
#define ICERR_UNSUPPORTED ((uint32)-1L)
#define ICERR_BADFORMAT ((uint32)-2L)
#define ICERR_MEMORY ((uint32)-3L)
#define ICERR_INTERNAL ((uint32)-4L)
#define ICERR_BADFLAGS ((uint32)-5L)
#define ICERR_BADPARAM ((uint32)-6L)
#define ICERR_BADSIZE ((uint32)-7L)
#define ICERR_BADHANDLE ((uint32)-8L)
#define ICERR_CANTUPDATE ((uint32)-9L)
#define ICERR_ABORT ((uint32)-10L)
#define ICERR_ERROR ((uint32)-11L)
#define ICERR_BADBITDEPTH ((uint32)-12L)
#define ICERR_BADIMAGESIZE ((uint32)-13L)
#define ICERR_CUSTOM ((uint32)-4000L)
#endif


#ifndef AVIERR_UNSUPPORTED
#define AVIERR_UNSUPPORTED 0x80044001
#define AVIERR_BADFORMAT 0x80044002
#define AVIERR_MEMORY 0x80044003
#define AVIERR_INTERNAL 0x80044004
#define AVIERR_BADFLAGS 0x80044005
#define AVIERR_BADPARAM 0x80044006
#define AVIERR_BADSIZE 0x80044007
#define AVIERR_BADHANDLE 0x80044008
#define AVIERR_FILEREAD 0x80044009
#define AVIERR_FILEWRITE 0x8004400A
#define AVIERR_FILEOPEN 0x8004400B
#define AVIERR_COMPRESSOR 0x8004400C
#define AVIERR_NOCOMPRESSOR 0x8004400D
#define AVIERR_READONLY 0x8004400E
#define AVIERR_NODATA 0x8004400F
#define AVIERR_BUFFERTOOSMALL 0x80044010
#define AVIERR_CANTCOMPRESS 0x80044011
#define AVIERR_USERABORT 0x80044012
#define AVIERR_ERROR 0x80044013
#endif


/////////////////////////////////////////////////////////////////////////////

static const char *GetVCMErrorString(uint32 icErr) {
	const char *err = "(unknown)";

	// Does anyone have the *real* text strings for this?

	switch(icErr) {
	case ICERR_OK:				err = "The operation completed successfully."; break;		// sorry, couldn't resist....
	case ICERR_UNSUPPORTED:		err = "The operation is not supported."; break;
	case ICERR_BADFORMAT:		err = "The source image format is not acceptable."; break;
	case ICERR_MEMORY:			err = "Not enough memory."; break;
	case ICERR_INTERNAL:		err = "An internal error occurred."; break;
	case ICERR_BADFLAGS:		err = "An invalid flag was specified."; break;
	case ICERR_BADPARAM:		err = "An invalid parameter was specified."; break;
	case ICERR_BADSIZE:			err = "An invalid size was specified."; break;
	case ICERR_BADHANDLE:		err = "The handle is invalid."; break;
	case ICERR_CANTUPDATE:		err = "Cannot update the destination image."; break;
	case ICERR_ABORT:			err = "The operation was aborted by the user."; break;
	case ICERR_ERROR:			err = "An unknown error occurred (may be corrupt data)."; break;
	case ICERR_BADBITDEPTH:		err = "The source color depth is not acceptable."; break;
	case ICERR_BADIMAGESIZE:	err = "The source image size is not acceptable."; break;
	default:
		if (icErr <= ICERR_CUSTOM) err = "A codec-specific error occurred.";
		break;
	}

	return err;
}

MyICError::MyICError(const char *s, uint32 icErr) {
	setf("%s error: %s (error code %ld)", s, GetVCMErrorString(icErr), icErr);
}

MyICError::MyICError(uint32 icErr, const char *format, ...) {
	char tmpbuf[1024];
	tmpbuf[0] = 0;

	va_list val;
	va_start(val, format);
	_vsnprintf_s(tmpbuf, _TRUNCATE, format, val);
	va_end(val);

	setf(tmpbuf, GetVCMErrorString(icErr));
}

MyAVIError::MyAVIError(const char *s, uint32 avierr) {
	const char *err = "(Unknown)";

	switch(avierr) {
	case AVIERR_UNSUPPORTED:		err = "unsupported"; break;
	case AVIERR_BADFORMAT:			err = "bad format"; break;
	case AVIERR_MEMORY:				err = "out of memory"; break;
	case AVIERR_INTERNAL:			err = "internal error"; break;
	case AVIERR_BADFLAGS:			err = "bad flags"; break;
	case AVIERR_BADPARAM:			err = "bad parameters"; break;
	case AVIERR_BADSIZE:			err = "bad size"; break;
	case AVIERR_BADHANDLE:			err = "bad AVIFile handle"; break;
	case AVIERR_FILEREAD:			err = "file read error"; break;
	case AVIERR_FILEWRITE:			err = "file write error"; break;
	case AVIERR_FILEOPEN:			err = "file open error"; break;
	case AVIERR_COMPRESSOR:			err = "compressor error"; break;
	case AVIERR_NOCOMPRESSOR:		err = "compressor not available"; break;
	case AVIERR_READONLY:			err = "file marked read-only"; break;
	case AVIERR_NODATA:				err = "no data (?)"; break;
	case AVIERR_BUFFERTOOSMALL:		err = "buffer too small"; break;
	case AVIERR_CANTCOMPRESS:		err = "can't compress (?)"; break;
	case AVIERR_USERABORT:			err = "aborted by user"; break;
	case AVIERR_ERROR:				err = "error (?)"; break;
	}

	setf("%s error: %s (%08lx)", s, err, avierr);
}

MyWin32Error::MyWin32Error(const wchar_t* format, uint32 err, ...)
	: mWin32Error(err)
{
	wchar_t szError[1024];
	wchar_t szTemp[1024];
	szError[0] = 0;

	va_list val;
	va_start(val, err);
	_vsnwprintf_s(szError, _TRUNCATE, format, val);
	va_end(val);

	// Determine the position of the last %s, and escape everything else. This doesn't
	// track escaped % signs properly, but it works for the strings that we receive (and at
	// worst just produces a funny message).
	const wchar_t* keep = wcsstr(szError, L"%s");
	if (keep) {
		for (;;) {
			const wchar_t* test = wcsstr(keep + 1, L"%s");
			if (!test) {
				break;
			}
			keep = test;
		}
	}

	wchar_t* t = szTemp;
	wchar_t* end = szTemp + std::size(szTemp) - 1;
	const wchar_t* s = szError;

	while (wchar_t c = *s++) {
		if (c == L'%') {
			// We allow one %s to go through. Everything else gets escaped.
			if (s - 1 != keep) {
				if (t >= end) {
					break;
				}

				*t++ = L'%';
			}
		}

		if (t >= end) {
			break;
		}

		*t++ = c;
	}

	*t = 0;

	if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		0,
		err,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		szError,
		std::size(szError),
		NULL))
	{
		szError[0] = 0;
	}

	if (szError[0]) {
		long l = wcslen(szError);

		if (l > 1 && szError[l - 2] == L'\r') {
			szError[l - 2] = 0;
		}
		else if (szError[l - 1] == L'\n') {
			szError[l - 1] = 0;
		}
	}

	setf(szTemp, szError);
}

MyWin32Error::MyWin32Error(const char *format, uint32 err, ...)
	: mWin32Error(err)
{
	char szError[1024];
	char szTemp[1024];
	szError[0] = 0;

	va_list val;
	va_start(val, err);
	_vsnprintf_s(szError, _TRUNCATE, format, val);
	va_end(val);

	// Determine the position of the last %s, and escape everything else. This doesn't
	// track escaped % signs properly, but it works for the strings that we receive (and at
	// worst just produces a funny message).
	const char *keep = strstr(szError, "%s");
	if (keep) {
		for(;;) {
			const char *test = strstr(keep + 1, "%s");

			if (!test)
				break;

			keep = test;
		}
	}

	char *t = szTemp;
	char *end = szTemp + std::size(szTemp) - 1;
	const char *s = szError;

	while(char c = *s++) {
		if (c == '%') {
			// We allow one %s to go through. Everything else gets escaped.
			if (s-1 != keep) {
				if (t >= end)
					break;

				*t++ = '%';
			}
		}

		if (t >= end)
			break;

		*t++ = c;
	}

	*t = 0;

	if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			0,
			err,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			szError,
			std::size(szError),
			NULL))
	{
		szError[0] = 0;
	}

	if (szError[0]) {
		long l = strlen(szError);

		if (l>1 && szError[l-2] == '\r')
			szError[l-2] = 0;
		else if (szError[l-1] == '\n')
			szError[l-1] = 0;
	}

	setf(szTemp, szError);
}

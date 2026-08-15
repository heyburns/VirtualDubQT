// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2024 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef _f_IVIDEODRIVER_H
#define _f_IVIDEODRIVER_H

#include <windows.h>
#include <vfw.h>
//#include "msviddrv.h"

// From msviddrv.h (Video for Windows 1.1e SDK)

typedef struct tag_video_open_parms {
    DWORD               dwSize;         // sizeof(VIDEO_OPEN_PARMS)
    FOURCC              fccType;        // 'vcap'
    FOURCC              fccComp;        // unused
    DWORD               dwVersion;      // version of msvideo opening you
    DWORD               dwFlags;        // channel type
    DWORD               dwError;        // if open fails, this is why
} VIDEO_OPEN_PARMS, FAR * LPVIDEO_OPEN_PARMS;

class IVideoDriver {
public:
	virtual ~IVideoDriver() {};

	virtual BOOL	Load(HDRVR hDriver)																	= 0;
	virtual void	Free(HDRVR hDriver)																	= 0;
	virtual void*	Open(HDRVR hDriver, char *szDescription, LPVIDEO_OPEN_PARMS lpVideoOpenParms)		= 0;
	virtual void	Disable(HDRVR hDriver)																= 0;
	virtual void	Enable(HDRVR hDriver)																= 0;
	virtual LRESULT	Default(DWORD dwDriverID, HDRVR hDriver, UINT uiMessage, LPARAM lParam1, LPARAM lParam2) = 0;
};

#endif

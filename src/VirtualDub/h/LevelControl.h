// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_LEVELCONTROL_H
#define f_LEVELCONTROL_H

#include <windows.h>

#define VIDEOLEVELCONTROLCLASS (g_szLevelControlName)

#ifndef f_LEVELCONTROL_CPP
extern const wchar_t g_szLevelControlName[];
#endif

#define VLCM_SETTABCOUNT		(WM_USER+0x100)
#define	VLCM_SETTABCOLOR		(WM_USER+0x101)
#define VLCM_SETTABPOS			(WM_USER+0x102)
#define	VLCM_GETTABPOS			(WM_USER+0x103)
#define VLCM_MOVETABPOS			(WM_USER+0x104)
#define	VLCM_SETGRADIENT		(WM_USER+0x105)

#define VLCN_TABCHANGE			(2)

typedef struct NMVLTABCHANGE {
	NMHDR	hdr;
	int		iTab;
	int		iNewPos;
} NMVLTABCHANGE;

ATOM RegisterLevelControl();

#endif

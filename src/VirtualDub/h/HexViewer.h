// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_HEXVIEWER_H
#define f_HEXVIEWER_H

#include <windows.h>

#define HEXEDITORCLASS (szHexEditorClassName)

#ifndef f_HEXVIEWER_CPP
extern const wchar_t szHexEditorClassName[];
#endif

ATOM RegisterHexEditor();

void HexEdit(HWND, const wchar_t *filename, bool readonly);

#endif

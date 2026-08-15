// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2015-2019 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VIRTUALDUB_SCRIPT_H
#define f_VIRTUALDUB_SCRIPT_H

#ifdef _MSC_VER
	#pragma once
#endif

bool InitScriptSystem();
void DeinitScriptSystem();
void RunProject(const wchar_t *name, void *hwnd=0);
void RunScript(const wchar_t *name, void *hwnd=0);
void RunScriptMemory(const char *mem, int start_line, bool stopAtReloadMarker = false, bool partial = false);

bool strfuzzycompare(const char *s, const char *t);
void memunbase64(char *t, const char *s);
void membase64(char *t, const char *s, long l);

#endif

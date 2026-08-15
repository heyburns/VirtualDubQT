// VirtualDub - Video processing and capture application
// JSON I/O library
//
// Copyright (C) 1998-2010 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDJSON_JSONNAMETABLE_H
#define f_VD2_VDJSON_JSONNAMETABLE_H

#include <vd2/system/vdstl.h>

class VDStringW;

struct VDJSONNameToken {
	VDJSONNameToken(uint32 token) : mToken(token) {}

	const uint32 mToken;
};

class VDJSONNameTable {
public:
	VDJSONNameTable();
	~VDJSONNameTable();

	const wchar_t *GetName(uint32 token) const;
	uint32 GetNameLength(uint32 token) const;

	uint32 AddName(const wchar_t *s);
	uint32 AddName(const wchar_t *s, size_t len);
	VDJSONNameToken GetToken(const char *s) const;
	VDJSONNameToken GetToken(const wchar_t *s) const;

protected:
	typedef vdfastvector<const VDStringW *> NameTable;
	NameTable mNameTable;

	typedef vdhashmap<VDStringW, uint32, vdhash<VDStringW>, vdstringpred> NameMap;
	NameMap mNameMap;
};

#endif

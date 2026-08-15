// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_MRULIST_H
#define f_MRULIST_H

#include <vector>
#include <vd2/system/VDString.h>

class MRUList {
private:
	std::vector<char>      mKey;
	std::vector<VDStringW> mFiles;
	const char*            mpKeyName;
	int                    mMaxCount;
	bool                   mbDirty;

public:
	MRUList(int max_files, const char* key);
	~MRUList();

	VDStringW operator[](int i);
	bool NonEmpty() const;

	void set_capacity(int max_files);

	void add(const wchar_t *file);
	void move_to_top(int index);
	void clear();
	void clear_history();
	void load();
	void flush();
};

#endif

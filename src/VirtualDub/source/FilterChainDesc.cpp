// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2011 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include <stdafx.h>
#include <FilterChainDesc.h>

VDFilterChainEntry::VDFilterChainEntry() {
}

VDFilterChainDesc::VDFilterChainDesc() {
}

VDFilterChainDesc::VDFilterChainDesc(const VDFilterChainDesc& src) {
	for(Entries::const_iterator it(mEntries.begin()), itEnd(mEntries.end());
		it != itEnd;
		++it)
	{
		VDFilterChainEntry *ent = new VDFilterChainEntry(**it);

		ent->AddRef();
		mEntries.push_back(ent);
	}
}

VDFilterChainDesc::~VDFilterChainDesc() {
	Clear();
}

bool VDFilterChainDesc::IsEmpty() const {
	return mEntries.empty();
}

void VDFilterChainDesc::Clear() {
	while(!mEntries.empty()) {
		VDFilterChainEntry *ent = mEntries.back();
		mEntries.pop_back();

		ent->Release();
	}
}

void VDFilterChainDesc::AddEntry(VDFilterChainEntry *ent) {
	mEntries.push_back(ent);
	ent->AddRef();
}

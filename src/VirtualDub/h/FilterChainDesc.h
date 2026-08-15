// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2011 Avery Lee
// Copyright (C) 2016 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_FILTERCHAINDESC_H
#define f_FILTERCHAINDESC_H

#include <vd2/system/refcount.h>
#include "FilterInstance.h"

class FilterInstance;
class IVDPixmapViewDialog;

struct VDFilterChainEntry : public vdrefcount {
	VDFilterChainEntry();

	vdrefptr<FilterInstance> mpInstance;
	vdvector<VDStringA> mSources;
	VDStringA	mOutputName;
	vdrefptr<IVDPixmapViewDialog> mpView;
};

class VDFilterChainDesc {
	VDFilterChainDesc& operator=(const VDFilterChainDesc&);
public:
	VDFilterChainDesc();
	VDFilterChainDesc(const VDFilterChainDesc&);
	~VDFilterChainDesc();

	bool IsEmpty() const;

	void Clear();
	void AddEntry(VDFilterChainEntry *ent);

	typedef vdfastvector<VDFilterChainEntry *> Entries;
	Entries mEntries;
};

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 2013 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDLIB_WIN32_FILEMAPPING_H
#define f_VD2_VDLIB_WIN32_FILEMAPPING_H

#ifdef _MSC_VER
	#pragma once
#endif

class VDFileMappingW32 {
	VDFileMappingW32(const VDFileMappingW32&);
	VDFileMappingW32& operator=(const VDFileMappingW32&);
public:
	VDFileMappingW32();
	~VDFileMappingW32();

	bool Init(uint32 bytes);
	void Shutdown();

	void *GetHandle() const { return mpHandle; }

protected:
	void	*mpHandle;
};

class VDFileMappingViewW32 {
	VDFileMappingViewW32(const VDFileMappingViewW32&);
	VDFileMappingViewW32& operator=(const VDFileMappingViewW32&);
public:
	VDFileMappingViewW32();
	~VDFileMappingViewW32();

	bool Init(const VDFileMappingW32& mapping, uint64 offset, uint32 size);
	void Shutdown();

	void *GetPointer() const { return mpView; }

protected:
	void *mpView;
};

#endif

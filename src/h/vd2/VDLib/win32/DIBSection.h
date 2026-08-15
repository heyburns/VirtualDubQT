// VirtualDub - Video processing and capture application
// Application helper library
//
// Copyright (C) 1998-2007 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDLIB_WIN32_DIBSECTION_H
#define f_VD2_VDLIB_WIN32_DIBSECTION_H

#ifdef _MSC_VER
	#pragma once
#endif

struct HBITMAP__;
struct HDC__;
struct tagBITMAPINFO;
struct VDPixmap;
class VDFileMappingW32;

class VDDIBSectionW32 {
public:
	VDDIBSectionW32();
	~VDDIBSectionW32();

	bool	Init(int w, int h, int depth, const VDFileMappingW32 *mapping = NULL, uint32 mapOffset = 0);
	bool	Init(const tagBITMAPINFO *bi, const VDFileMappingW32 *mapping = NULL, uint32 mapOffset = 0);
	void	Shutdown();

	void		*GetPointer()	const { return mpBits; }
	HDC__		*GetHDC()		const { return mhdc; }
	HBITMAP__	*GetHBITMAP()	const { return mhbm; }

	VDPixmap	GetPixmap()		const;

protected:
	void		*mpBits;
	HBITMAP__	*mhbm;
	HDC__		*mhdc;
	void		*mhgo;
	int			mWidth;
	int			mHeight;
	int			mDepth;
	ptrdiff_t	mPitch;
	void		*mpScan0;
	bool		mbForceUnmap;
};

#endif

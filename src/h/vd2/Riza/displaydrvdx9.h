// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2005 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_RIZA_DISPLAYDRVDX9_H
#define f_VD2_RIZA_DISPLAYDRVDX9_H

#include <vd2/system/refcount.h>
#include <vd2/VDDisplay/renderer.h>

struct IDirect3DTexture9;

class VDINTERFACE IVDFontRendererD3D9 : public IVDRefCount {
public:
	virtual bool Init(VDD3D9Manager *d3dmgr) = 0;
	virtual void Shutdown() = 0;

	virtual bool Begin() = 0;
	virtual void DrawTextLine(int x, int y, uint32 textColor, uint32 outlineColor, const char *s) = 0;
	virtual void End() = 0;
};

bool VDCreateFontRendererD3D9(IVDFontRendererD3D9 **);

class VDINTERFACE IVDDisplayRendererD3D9 : public IVDRefCount, public IVDDisplayRenderer {
public:
	virtual bool Init(VDD3D9Manager *d3dmgr) = 0;
	virtual void Shutdown() = 0;

	virtual bool Begin() = 0;
	virtual void End() = 0;
};

bool VDCreateDisplayRendererD3D9(IVDDisplayRendererD3D9 **);

class VDINTERFACE IVDVideoDisplayDX9Manager : public IVDRefCount {
public:
	enum CubicMode {
		kCubicNotInitialized,
		kCubicNotPossible,		// Your card is LAME
		kCubicUseFF2Path,		// Use fixed-function, 2 stage path (GeForce2/GeForce4MX - 8 passes)
		kCubicUseFF3Path,		// Use fixed-function, 3 stage path (RADEON 7xxx - 4 passes)
		kCubicUsePS1_1Path,		// Use programmable, 3 stage path (GeForce3/GeForce4 - 4 passes)
		kCubicUsePS1_4Path,		// Use programmable, 5 stage path (RADEON 85xx+/GeForceFX+ - 2 passes)
		kMaxCubicMode = kCubicUsePS1_4Path
	};
};

class VDINTERFACE IVDVideoUploadContextD3D9 : public IVDRefCount {
public:
	virtual IDirect3DTexture9 *GetD3DTexture(int i) = 0;

	virtual bool Init(void *hmonitor, bool use9ex, const VDPixmap& source, bool allowConversion, bool preserveYCbCr, int buffers) = 0;
	virtual void Shutdown() = 0;

	virtual bool Update(const VDPixmap& source, int fieldMask) = 0;
};

bool VDCreateVideoUploadContextD3D9(IVDVideoUploadContextD3D9 **);
bool VDInitDisplayDX9(HMONITOR hmonitor, bool use9ex, IVDVideoDisplayDX9Manager **ppManager);

#endif

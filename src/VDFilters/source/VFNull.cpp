// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2015-2019 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include "resource.h"

#include <vd2/VDXFrame/VideoFilter.h>
#include <vd2/VDLib/Dialog.h>

class VDVFNull : public VDXVideoFilter {
public:
	uint32 GetParams();
	void Run();
	bool Configure(VDXHWND hwnd);
};

uint32 VDVFNull::GetParams() {
	switch(fa->src.mpPixmapLayout->format) {
	case nsVDXPixmap::kPixFormat_XRGB1555:
	case nsVDXPixmap::kPixFormat_RGB565:
	case nsVDXPixmap::kPixFormat_RGB888:
	case nsVDXPixmap::kPixFormat_XRGB8888:
	case nsVDXPixmap::kPixFormat_Y8:
	case nsVDXPixmap::kPixFormat_Y16:
	case nsVDXPixmap::kPixFormat_YUV422_UYVY:
	case nsVDXPixmap::kPixFormat_YUV422_YUYV:
	case nsVDXPixmap::kPixFormat_YUV444_Planar:
	case nsVDXPixmap::kPixFormat_YUV422_Planar:
	case nsVDXPixmap::kPixFormat_YUV420_Planar:
	case nsVDXPixmap::kPixFormat_YUV411_Planar:
	case nsVDXPixmap::kPixFormat_YUV410_Planar:
	case nsVDXPixmap::kPixFormat_YUV422_UYVY_709:
	case nsVDXPixmap::kPixFormat_YUV420_NV12:
	case nsVDXPixmap::kPixFormat_Y8_FR:
	case nsVDXPixmap::kPixFormat_YUV422_YUYV_709:
	case nsVDXPixmap::kPixFormat_YUV444_Planar_709:
	case nsVDXPixmap::kPixFormat_YUV422_Planar_709:
	case nsVDXPixmap::kPixFormat_YUV420_Planar_709:
	case nsVDXPixmap::kPixFormat_YUV411_Planar_709:
	case nsVDXPixmap::kPixFormat_YUV410_Planar_709:
	case nsVDXPixmap::kPixFormat_YUV422_UYVY_FR:
	case nsVDXPixmap::kPixFormat_YUV422_YUYV_FR:
	case nsVDXPixmap::kPixFormat_YUV444_Planar_FR:
	case nsVDXPixmap::kPixFormat_YUV422_Planar_FR:
	case nsVDXPixmap::kPixFormat_YUV420_Planar_FR:
	case nsVDXPixmap::kPixFormat_YUV411_Planar_FR:
	case nsVDXPixmap::kPixFormat_YUV410_Planar_FR:
	case nsVDXPixmap::kPixFormat_YUV422_UYVY_709_FR:
	case nsVDXPixmap::kPixFormat_YUV422_YUYV_709_FR:
	case nsVDXPixmap::kPixFormat_YUV444_Planar_709_FR:
	case nsVDXPixmap::kPixFormat_YUV422_Planar_709_FR:
	case nsVDXPixmap::kPixFormat_YUV420_Planar_709_FR:
	case nsVDXPixmap::kPixFormat_YUV411_Planar_709_FR:
	case nsVDXPixmap::kPixFormat_YUV410_Planar_709_FR:
	case nsVDXPixmap::kPixFormat_YUV420i_Planar:
	case nsVDXPixmap::kPixFormat_YUV420i_Planar_FR:
	case nsVDXPixmap::kPixFormat_YUV420i_Planar_709:
	case nsVDXPixmap::kPixFormat_YUV420i_Planar_709_FR:
	case nsVDXPixmap::kPixFormat_YUV420it_Planar:
	case nsVDXPixmap::kPixFormat_YUV420it_Planar_FR:
	case nsVDXPixmap::kPixFormat_YUV420it_Planar_709:
	case nsVDXPixmap::kPixFormat_YUV420it_Planar_709_FR:
	case nsVDXPixmap::kPixFormat_YUV420ib_Planar:
	case nsVDXPixmap::kPixFormat_YUV420ib_Planar_FR:
	case nsVDXPixmap::kPixFormat_YUV420ib_Planar_709:
	case nsVDXPixmap::kPixFormat_YUV420ib_Planar_709_FR:
	case nsVDXPixmap::kPixFormat_XRGB64:
	case nsVDXPixmap::kPixFormat_YUV444_Planar16:
	case nsVDXPixmap::kPixFormat_YUV422_Planar16:
	case nsVDXPixmap::kPixFormat_YUV420_Planar16:

	case nsVDXPixmap::kPixFormat_YUV444_V410:
	case nsVDXPixmap::kPixFormat_YUV444_Y410:
	case nsVDXPixmap::kPixFormat_R210:
	case nsVDXPixmap::kPixFormat_R10K:
	case nsVDXPixmap::kPixFormat_YUV444_V308:
	case nsVDXPixmap::kPixFormat_YUV422_P210:
	case nsVDXPixmap::kPixFormat_YUV420_P010:
	case nsVDXPixmap::kPixFormat_YUV422_P216:
	case nsVDXPixmap::kPixFormat_YUV420_P016:
	case nsVDXPixmap::kPixFormat_YUV444_Alpha_Planar:
	case nsVDXPixmap::kPixFormat_YUV422_Alpha_Planar:
	case nsVDXPixmap::kPixFormat_YUV420_Alpha_Planar:
	case nsVDXPixmap::kPixFormat_YUV444_Alpha_Planar16:
	case nsVDXPixmap::kPixFormat_YUV422_Alpha_Planar16:
	case nsVDXPixmap::kPixFormat_YUV420_Alpha_Planar16:

	case nsVDXPixmap::kPixFormat_RGB_Planar:
	case nsVDXPixmap::kPixFormat_RGB_Planar16:
	case nsVDXPixmap::kPixFormat_RGB_Planar32F:
	case nsVDXPixmap::kPixFormat_RGBA_Planar:
	case nsVDXPixmap::kPixFormat_RGBA_Planar16:
	case nsVDXPixmap::kPixFormat_RGBA_Planar32F:
		break;

	default:
		return FILTERPARAM_NOT_SUPPORTED;
	}

	fa->dst.mpPixmapLayout->pitch = fa->src.mpPixmapLayout->pitch;
	return FILTERPARAM_SUPPORTS_ALTFORMATS | FILTERPARAM_PURE_TRANSFORM;
}

void VDVFNull::Run() {
}

class VDVFNullDialog: public VDDialogFrameW32 {
public:
	IVDXFilterPreview2 *mifp2;

	VDVFNullDialog(IVDXFilterPreview2 *ifp2)
		: VDDialogFrameW32(IDD_FILTER_NULL)
		, mifp2(ifp2)
	{
	}

	bool OnLoaded() {
		if (mifp2)
			mifp2->InitButton((VDXHWND)GetControl(IDC_PREVIEW));
		return false;
	}
	void OnDestroy() {
		if (mifp2)
			mifp2->Close();
	}
	bool OnCommand(uint32 id, uint32 extcode) {
		switch(id) {
			case IDC_PREVIEW:
				if (mifp2)
					mifp2->Toggle((VDXHWND)mhdlg);
				return true;
		}

		return false;
	}
};

bool VDVFNull::Configure(VDXHWND hwnd) {
	VDVFNullDialog dlg(fa->ifp2);

	if (!dlg.ShowDialog((VDGUIHandle)hwnd))
		return false;

	return true;
}

extern const VDXFilterDefinition g_VDVFNull = VDXVideoFilterDefinition<VDVFNull>(
	NULL,
	"null transform",
	"Does nothing. Typically used as a placeholder for cropping.");

extern const VDXFilterDefinition g_VDVFCrop = VDXVideoFilterDefinition<VDVFNull>(
	NULL,
	"crop",
	"Used for cropping.");


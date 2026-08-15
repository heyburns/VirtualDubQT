// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2006 Avery Lee
// Copyright (C) 2020 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_PARAMETERCURVECONTROL_H
#define f_VD2_PARAMETERCURVECONTROL_H

#include <vd2/system/unknown.h>
#include <vd2/system/event.h>

#define VDPARAMETERCURVECONTROLCLASS (g_VDParameterCurveControlClass)

extern const wchar_t g_VDParameterCurveControlClass[];

class VDParameterCurve;

class IVDUIParameterCurveControl : public IVDRefUnknown {
public:
	enum { kTypeID = 'uipc' };

	enum Status {
		kStatus_Nothing,
		kStatus_Focused,
		kStatus_PointHighlighted,
		kStatus_PointDrag
	};

	virtual VDParameterCurve *GetCurve() = 0;
	virtual void SetCurve(VDParameterCurve *pCurve) = 0;

	virtual void SetPosition(VDPosition pos) = 0;
	virtual void SetSelectedPoint(int x) = 0;
	virtual int GetSelectedPoint() = 0;
	virtual void DeletePoint(int x) = 0;
	virtual void SetValue(int x, double v) = 0;

	// events
	virtual VDEvent<IVDUIParameterCurveControl, int>& CurveUpdatedEvent() = 0;
	virtual VDEvent<IVDUIParameterCurveControl, Status>& StatusUpdatedEvent() = 0;
};

IVDUIParameterCurveControl *VDGetIUIParameterCurveControl(VDGUIHandle h);

#endif

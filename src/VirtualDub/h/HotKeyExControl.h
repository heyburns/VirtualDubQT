// VirtualDub - Video processing and capture application
//
// Copyright (C) 2013 Avery Lee
// Copyright (C) 2019 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_HOTKEYEXCONTROL_H
#define f_VD2_HOTKEYEXCONTROL_H

#include <vd2/system/event.h>
#include <vd2/system/unknown.h>

struct VDUIAccelerator;
class IVDUIHotKeyExControl;

#define VDUIHOTKEYEXCLASS L"VDHotKeyEx"

bool VDUIRegisterHotKeyExControl();
IVDUIHotKeyExControl *VDGetUIHotKeyExControl(VDGUIHandle h);

class IVDUIHotKeyExControl : public IVDRefUnknown {
public:
	enum { kTypeID = 'uihk' };

	virtual void GetAccelerator(VDUIAccelerator& accel) = 0;
	virtual void SetAccelerator(const VDUIAccelerator& accel) = 0;
	virtual void Clear() = 0;

	virtual VDEvent<IVDUIHotKeyExControl, VDUIAccelerator>& OnChange() = 0;
};

#endif

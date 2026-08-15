// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2005 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_CAPTUREUI_H
#define f_CAPTUREUI_H

class IVDCaptureProject;

class VDINTERFACE IVDCaptureProjectUI : public IVDRefCount {
public:
	virtual bool	Attach(VDGUIHandle hwnd, IVDCaptureProject *pProject) = 0;
	virtual void	Detach() = 0;

	virtual bool	SetDriver(const wchar_t *s) = 0;
	virtual void	SetCaptureFile(const wchar_t *s) = 0;
	virtual void	PreallocateCaptureFile(sint64 size) = 0;
	virtual bool	SetTunerChannel(int ch) = 0;
	virtual bool	SetTunerExactFrequency(uint32 freq) = 0;
	virtual void	SetTunerInputMode(bool cable) = 0;
	virtual void	SetTimeLimit(int limitsecs) = 0;
	virtual void	SetAudioCaptureEnabled(bool enable) = 0;
	virtual void	SetAudioPlaybackEnabled(bool enable) = 0;
	virtual void	Capture() = 0;
};

IVDCaptureProjectUI *VDCreateCaptureProjectUI();

#endif

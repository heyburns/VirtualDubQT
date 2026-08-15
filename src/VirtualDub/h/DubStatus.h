// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2016-2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_DUBSTATUS_H
#define f_DUBSTATUS_H

typedef void (*DubPositionCallback)(VDPosition start, VDPosition cur, VDPosition end, int progress, bool fast_update, void *cookie);

class DubAudioStreamInfo;
class DubVideoStreamInfo;
class AudioSource;
class VideoSource;
class InputFile;
class AudioStream;
class IDubber;
class DubOptions;

class VDINTERFACE IDubStatusHandler {
public:
	virtual ~IDubStatusHandler() {}
	virtual void InitLinks(DubAudioStreamInfo	*painfo,
		DubVideoStreamInfo	*pvinfo,
		AudioStream			*audioStreamSource,

		IDubber				*pDubber,
		DubOptions			*opt)=0;
	virtual void NotifyNewFrame(uint32 size, bool isKey)=0;
	virtual HWND Display(HWND hwndParent, int iInitialPriority)=0;
	virtual void Destroy()=0;
	virtual void DeferDestroy(void** ref)=0;
	virtual void SetPositionCallback(DubPositionCallback dpc, void *cookie)=0;
	virtual bool ToggleStatus()=0;
	virtual void SetLastPosition(VDPosition pos, bool fast_update)=0;
	virtual void NotifyPositionChange(VDPosition pos)=0;
	virtual void Freeze(bool failed, bool completed)=0;
	virtual bool isVisible()=0;
	virtual bool isFrameVisible(bool)=0;
	virtual bool ToggleFrame(bool)=0;
	virtual void OnBackgroundStateUpdated()=0;
	virtual HWND GetHwnd()=0;
	virtual bool IsNormalWindow()=0;
};

IDubStatusHandler *CreateDubStatusHandler();

#ifndef f_DUBSTATUS_CPP
extern const char *const g_szDubPriorities[];
#endif

#endif

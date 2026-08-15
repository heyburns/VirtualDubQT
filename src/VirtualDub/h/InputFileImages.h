// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2016-2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_INPUTFILEIMAGES_H
#define f_INPUTFILEIMAGES_H

#include <vd2/system/VDString.h>
#include "InputFile.h"
#include "VideoSourceImages.h"

class VDInputFileImages : public InputFile {
private:
	static INT_PTR APIENTRY _InfoDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
public:
	VDInputFileImages(uint32 flags);
	~VDInputFileImages();

	void Init(const wchar_t *szFile);

	void setAutomated(bool fAuto);
	void InfoDialog(VDGUIHandle hwndParent);

	bool GetVideoSource(int index, IVDVideoSource **ppSrc);
	bool GetAudioSource(int index, AudioSource **ppSrc);
	int GetFileFlags() {
		if (single_file_mode) return 0;
		return IVDInputDriver::kFF_Sequence;
	}

public:
	VDPosition	GetFrameCount() const { return mFrames; }
	const wchar_t *ComputeFilename(vdfastvector<wchar_t>& pathBuf, VDPosition pos);

protected:
	VDStringW	mBaseName;
	int			mLastDigitPos;
	VDPosition	mFrames;
	bool single_file_mode;
};

#endif

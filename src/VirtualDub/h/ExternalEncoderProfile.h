// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2010 Avery Lee
// Copyright (C) 2015-2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_EXTERNALENCODERPROFILE_H
#define f_EXTERNALENCODERPROFILE_H

#include <vd2/system/VDString.h>
#include <vd2/system/refcount.h>

enum VDExtEncType {
	kVDExtEncType_Video,
	kVDExtEncType_Audio,
	kVDExtEncType_Mux,
	kVDExtEncTypeCount
};

enum VDExtEncInputFormat {
	kVDExtEncInputFormat_Raw,
	kVDExtEncInputFormat_WAV,
	kVDExtEncInputFormatCount
};

class VDExtEncProfile : public vdrefcount {
public:
	VDExtEncProfile();

	VDStringW	mName;
	VDStringW	mProgram;
	VDStringW	mCommandArguments;
	VDStringW	mOutputFilename;
	VDExtEncType	mType;

	VDExtEncInputFormat	mInputFormat;
	VDStringW	mPixelFormat;

	bool	mbCheckReturnCode;
	bool	mbLogStdout;
	bool	mbLogStderr;
	bool	mbPredeleteOutputFile;
	bool	mbBypassCompression;
};

class VDExtEncSet : public vdrefcount {
public:
	VDExtEncSet();

	VDStringW	mName;
	VDStringW	mVideoEncoder;
	VDStringW	mAudioEncoder;
	VDStringW	mMultiplexer;
	VDStringW	mFileDesc;
	VDStringW	mFileExt;
	bool		mbProcessPartialOutput;
	bool		mbUseOutputAsTemp;
};

uint32 VDGetExternalEncoderProfileCount();
bool VDGetExternalEncoderProfileByIndex(uint32 idx, VDExtEncProfile **pp);
bool VDGetExternalEncoderProfileByName(const wchar_t *name, VDExtEncProfile **pp);
void VDAddExternalEncoderProfile(VDExtEncProfile *profile);
void VDRemoveExternalEncoderProfile(VDExtEncProfile *profile);

uint32 VDGetExternalEncoderSetCount();
bool VDGetExternalEncoderSetByIndex(uint32 idx, VDExtEncSet **pp);
bool VDGetExternalEncoderSetByName(const wchar_t *name, VDExtEncSet **pp);
void VDAddExternalEncoderSet(VDExtEncSet *eset);
void VDRemoveExternalEncoderSet(VDExtEncSet *eset);
bool VDGetExternalEncoderSetAsk(HWND parent, VDExtEncSet **pp);

void VDLoadExternalEncoderProfiles();
void VDSaveExternalEncoderProfiles();
void VDShutdownExternalEncoderProfiles();

#endif

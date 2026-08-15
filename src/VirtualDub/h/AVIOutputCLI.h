// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2019 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUTCLI_H
#define f_AVIOUTPUTCLI_H

#include <vd2/system/unknown.h>
#include "ExternalEncoderProfile.h"

struct VDPixmapLayout;
class DubOptions;

struct VDAVIOutputCLITemplate {
	vdrefptr<VDExtEncProfile> mpVideoEncoderProfile;
	vdrefptr<VDExtEncProfile> mpAudioEncoderProfile;
	vdrefptr<VDExtEncProfile> mpMultiplexerProfile;
	bool mbUseOutputPathAsTemp;
};

class IAVIOutputCLI : public IVDUnknown {
public:
	enum { kTypeID = 'aocl' };

	virtual ~IAVIOutputCLI() {}
	virtual void SetInputLayout(const VDPixmapLayout& layout) = 0;
	virtual void SetOpt(DubOptions& opt) = 0;
	virtual void SetBufferSize(sint32 nBytes) = 0;
	virtual void CloseWithoutFinalize() = 0;
};

IAVIOutputCLI *VDCreateAVIOutputCLI(const VDAVIOutputCLITemplate& templ);

#endif

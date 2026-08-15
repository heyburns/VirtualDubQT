// VirtualDub - Video processing and capture application
//
// Copyright (C) 2017-2018 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVIOUTPUTPLUGIN_H
#define f_AVIOUTPUTPLUGIN_H

#ifdef _MSC_VER
	#pragma once
#endif

#include "AVIOutput.h"

class IVDMediaOutput;
struct VDPluginDescription;
class IVDXOutputFileDriver;
class VDOutputDriverContextImpl;
class IVDXAudioEnc;
class IVDAudioCodec;
struct VDWaveFormat;

class VDINTERFACE IVDOutputDriver : public IVDRefCount {
public:
	virtual VDPluginDescription* GetDesc() = 0;
	virtual const wchar_t *	GetSignatureName() = 0;
	virtual IVDXOutputFileDriver * GetDriver() = 0;
	virtual VDOutputDriverContextImpl* GetContext() = 0;
	virtual uint32 GetFormatCaps(int i) = 0;
};

class VDINTERFACE IVDAudioEnc : public IVDRefCount {
public:
	virtual const wchar_t *	GetName() = 0;
	virtual const wchar_t*	GetSignatureName() = 0;
	virtual IVDXAudioEnc * GetDriver() = 0;
	virtual VDOutputDriverContextImpl* GetContext() = 0;
};

typedef std::vector<vdrefptr<IVDOutputDriver> > tVDOutputDrivers;
typedef std::vector<vdrefptr<IVDAudioEnc> > tVDAudioEncList;

void VDInitOutputDrivers();
void VDShutdownOutputDrivers();
void VDGetOutputDrivers(tVDOutputDrivers& l);
IVDOutputDriver *VDGetOutputDriverByName(const wchar_t *name);
void VDInitAudioEnc();
void VDShutdownAudioEnc();
void VDGetAudioEncList(tVDAudioEncList& l);
IVDAudioEnc *VDGetAudioEncByName(const wchar_t* name);

class IVDMediaOutputPlugin : public IVDMediaOutput {
public:
	virtual void setTextInfo(uint32 ckid, const char *text) = 0;
};

IVDMediaOutputPlugin *VDCreateMediaOutputPlugin(IVDOutputDriver* driver, const char* format);
IVDAudioCodec *VDCreateAudioCompressorPlugin(const VDWaveFormat *srcFormat, const wchar_t* pSignatureName, vdblock<uint8>& config, bool throwIfNotFound);

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2017-2018 Anton Shekhovtsov
// Copyright (C) 2025-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_OSHELPER_H
#define f_OSHELPER_H

#include <vd2/system/win32/miniwindows.h>
#include <vd2/system/VDString.h>

void Draw3DRect(VDZHDC hDC, int x, int y, int dx, int dy, bool inverted);

void VDShowHelp(HWND hwnd, const wchar_t* filename = 0);

bool IsFilenameOnFATVolume(const wchar_t *pszFilename);

VDZHWND VDGetAncestorW32(HWND hwnd, uint32 gaFlags);
VDStringW VDLoadStringW32(uint32 uID, bool doSubstitutions);
void VDSubstituteStrings(VDStringW& s);

void VDSetDataPath(const wchar_t *path);
const wchar_t *VDGetDataPath();
VDStringW VDGetLocalAppDataPath();

void VDCopyTextToClipboard(const wchar_t *s);

// Creates a 32-bit signature from the current date, time, and process ID. Not
// guaranteed to be unique, but reasonably non-conflicting.
uint32 VDCreateAutoSaveSignature();

void LaunchURL(const char *pURL);

enum VDSystemShutdownMode
{
	kVDSystemShutdownMode_Shutdown,
	kVDSystemShutdownMode_Hibernate,
	kVDSystemShutdownMode_Sleep
};

bool VDInitiateSystemShutdownWithUITimeout(VDSystemShutdownMode mode, const wchar_t *reason, uint32 timeout);
bool VDInitiateSystemShutdown(VDSystemShutdownMode mode);

class VDCPUUsageReader {
public:
	VDCPUUsageReader() = default;
	~VDCPUUsageReader() = default;

	void Init();
	void Shutdown() {};

	void read(int& vd, int& sys);

private:
	uint64 kt_last;
	uint64 ut_last;
	uint64 skt_last;
	uint64 sut_last;
	uint64 idle_last;
};

void VDEnableSampling(bool bEnable);

struct VDSamplingAutoProfileScope {
	VDSamplingAutoProfileScope() {
		VDEnableSampling(true);
	}
	~VDSamplingAutoProfileScope() {
		VDEnableSampling(false);
	}
};

#endif

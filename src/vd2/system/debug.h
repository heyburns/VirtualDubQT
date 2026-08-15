// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_DEBUG_H
#define f_VD2_SYSTEM_DEBUG_H

#include <vd2/system/vdtypes.h>

class IVDExternalCallTrap {
public:
	virtual void OnMMXTrap(const wchar_t *context, const char *file, int line) = 0;
	virtual void OnFPUTrap(const wchar_t *context, const char *file, int line, uint16 fpucw) = 0;
	virtual void OnSSETrap(const wchar_t *context, const char *file, int line, uint32 mxcsr) = 0;
};

void VDSetExternalCallTrap(IVDExternalCallTrap *);

bool IsMMXState();
void ClearMMXState();
void VDClearEvilCPUStates();
void VDPreCheckExternalCodeCall(const char *file, int line);
void VDPostCheckExternalCodeCall(const wchar_t *mpContext, const char *mpFile, int mLine);

struct VDSilentExternalCodeBracket {
	VDSilentExternalCodeBracket() {
		VDClearEvilCPUStates();
	}

	~VDSilentExternalCodeBracket() {
		VDClearEvilCPUStates();
	}
};

struct VDExternalCodeBracketLocation {
	VDExternalCodeBracketLocation(const wchar_t *pContext, const char *file, const int line)
		: mpContext(pContext)
		, mpFile(file)
		, mLine(line)
	{
	}

	const wchar_t *mpContext;
	const char *mpFile;
	const int mLine;	
};

struct VDExternalCodeBracket {
	VDExternalCodeBracket(const wchar_t *pContext, const char *file, const int line)
		: mpContext(pContext)
		, mpFile(file)
		, mLine(line)
	{
		VDPreCheckExternalCodeCall(file, line);
	}

	VDExternalCodeBracket(const VDExternalCodeBracketLocation& loc)
		: mpContext(loc.mpContext)
		, mpFile(loc.mpFile)
		, mLine(loc.mLine)
	{
	}

	~VDExternalCodeBracket() {
		VDPostCheckExternalCodeCall(mpContext, mpFile, mLine);
	}

	operator bool() const { return false; }

	const wchar_t *mpContext;
	const char *mpFile;
	const int mLine;
};

#endif

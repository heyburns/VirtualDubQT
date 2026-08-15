// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_LOG_H
#define f_VD2_SYSTEM_LOG_H

#include <vd2/system/VDString.h>
#include <list>

class IVDLogger {
public:
	virtual void AddLogEntry(int severity, const wchar_t *s) = 0;
};

enum {
	kVDLogInfo, kVDLogMarker, kVDLogWarning, kVDLogError
};

void VDLog(int severity, const wchar_t *format);
void VDLog(int severity, const VDStringW& s);
void VDLogF(int severity, const wchar_t *format, ...);
void VDAttachLogger(IVDLogger *pLogger, bool bThisThreadOnly, bool bReplayLog);
void VDDetachLogger(IVDLogger *pLogger);

class VDAutoLogger : public IVDLogger {
public:
	struct Entry {
		int severity;
		VDStringW text;

		Entry(int sev, const wchar_t *s) : severity(sev), text(s) {}
	};

	typedef std::list<Entry>	tEntries;

	VDAutoLogger(int min_severity);
	~VDAutoLogger();

	void AddLogEntry(int severity, const wchar_t *s);

	const tEntries& GetEntries();

protected:
	tEntries mEntries;
	const int mMinSeverity;
	bool	mbAttached;
};

#endif

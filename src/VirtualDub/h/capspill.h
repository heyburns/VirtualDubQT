// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VIRTUALDUB_CAPSPILL_H
#define f_VIRTUALDUB_CAPSPILL_H

template<class T> class ListNode2;

class CapSpillDrive : public ListNode2<CapSpillDrive> {
public:
	int threshold;
	int priority;
	VDStringW	path;

	CapSpillDrive();
	~CapSpillDrive();

	void setPath(const wchar_t *s);
	wchar_t *makePath(wchar_t *buf, const wchar_t *fn) const;
};


__int64 CapSpillGetFreeSpace();
CapSpillDrive *CapSpillFindDrive(const wchar_t *path);
void CapSpillSaveToRegistry();
void CapSpillRestoreFromRegistry();
CapSpillDrive *CapSpillPickDrive(bool fAudio);

#endif

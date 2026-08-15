// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2011 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_DATE_H
#define f_VD2_SYSTEM_DATE_H

#include <vd2/system/vdtypes.h>

class VDStringW;

struct VDDate {
	uint64	mTicks;

	bool operator==(const VDDate& x) const { return mTicks == x.mTicks; }
	bool operator!=(const VDDate& x) const { return mTicks != x.mTicks; }
	bool operator< (const VDDate& x) const { return mTicks <  x.mTicks; }
	bool operator> (const VDDate& x) const { return mTicks >  x.mTicks; }
	bool operator<=(const VDDate& x) const { return mTicks <= x.mTicks; }
	bool operator>=(const VDDate& x) const { return mTicks >= x.mTicks; }
};

struct VDExpandedDate {
	uint32	mYear;
	uint8	mMonth;
	uint8	mDayOfWeek;
	uint8	mDay;
	uint8	mHour;
	uint8	mMinute;
	uint8	mSecond;
	uint16	mMilliseconds;
};

VDDate VDGetCurrentDate();
VDExpandedDate VDGetLocalDate(const VDDate& date);
void VDAppendLocalDateString(VDStringW& dst, const VDExpandedDate& date);
void VDAppendLocalTimeString(VDStringW& dst, const VDExpandedDate& date);

#endif	// f_VD2_SYSTEM_DATE_H

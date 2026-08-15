// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2011 Avery Lee, All Rights Reserved.
// Copyright (C) 2023 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/date.h>
#include <vd2/system/w32assist.h>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <chrono>

VDDate VDGetCurrentDate() {
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

	VDDate r;
	r.mTicks = (uint64)millis;
	return r;
}

VDExpandedDate VDGetLocalDate(const VDDate& date) {
	VDExpandedDate r = {0};
	QDateTime dt = QDateTime::fromMSecsSinceEpoch(date.mTicks);
	QDate d = dt.date();
	QTime t = dt.time();

	r.mYear = d.year();
	r.mMonth = (uint8)d.month();
	r.mDayOfWeek = (uint8)(d.dayOfWeek() % 7);
	r.mDay = (uint8)d.day();
	r.mHour = (uint8)t.hour();
	r.mMinute = (uint8)t.minute();
	r.mSecond = (uint8)t.second();
	r.mMilliseconds = (uint16)t.msec();

	return r;
}

void VDAppendLocalDateString(VDStringW& dst, const VDExpandedDate& ed) {
	QDate d(ed.mYear, ed.mMonth, ed.mDay);
	QString str = d.toString("yyyy-MM-dd");
	dst += VDStringW((const wchar_t*)str.utf16());
}

void VDAppendLocalTimeString(VDStringW& dst, const VDExpandedDate& ed) {
	QTime t(ed.mHour, ed.mMinute, ed.mSecond, ed.mMilliseconds);
	QString str = t.toString("hh:mm:ss");
	dst += VDStringW((const wchar_t*)str.utf16());
}


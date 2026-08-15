// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include "DubSource.h"

DubSource::DubSource() = default;

void *DubSource::allocFormat(int format_len) {
	if (this->format_len != format_len) {
		format.reset(new char[format_len]);
		this->format_len = format_len;
	}

	return (void *)format.get();
}

bool DubSource::isStreaming() {
	return false;
}

int DubSource::read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) {
	if (lStart < mSampleFirst || lStart >= mSampleLast) {
		if (lSamplesRead)
			*lSamplesRead = 0;
		if (lBytesRead)
			*lBytesRead = 0;
		return 0;
	}

	if (lCount != (uint32)-1 && lCount > mSampleLast - lStart) lCount = (uint32)(mSampleLast - lStart);

	return _read(lStart, lCount, lpBuffer, cbBuffer, lBytesRead, lSamplesRead);
}

void DubSource::streamBegin(bool fRealTime, bool bForceReset) {
}

void DubSource::streamEnd() {
}

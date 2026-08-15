// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_PROGRESSDIALOG_H
#define f_PROGRESSDIALOG_H

#include <windows.h>

class ProgressDialog {
private:
	HWND hwndProgressBar, hwndDialog, hwndValue, mhwndParent;
	long newval, curval, maxval;
	int mSparseCount;
	int mSparseInterval;
	DWORD dwLastTime;
	bool fAbortEnabled;
	bool fAbort;
	bool mbPreviouslyEnabled;

	const char *lpszTitle, *lpszCaption, *lpszValueFormat;

	static INT_PTR CALLBACK ProgressDlgProc(HWND, UINT, WPARAM, LPARAM);
public:
	ProgressDialog(HWND hwndParent, const char *szTitle, const char *szCaption, long maxval, bool fAbortEnabled);
	~ProgressDialog();

	void setValueFormat(const char *);
	void setCaption(const char *);
	void setLimit(long);

	void advance(long newval) {
		this->newval = newval;
	}

	void check();
	void close();
};

#endif

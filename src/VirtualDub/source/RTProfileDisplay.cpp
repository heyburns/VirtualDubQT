// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
// Copyright (C) 2016 Anton Shekhovtsov
// Copyright (C) 2024-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#include <windows.h>
#include <vd2/system/thread.h>
#include <vd2/system/profile.h>
#include <vector>
#include <list>
#include <algorithm>

#include "gui.h"
#include "oshelper.h"
#include "RTProfileDisplay.h"

extern HINSTANCE g_hInst;
extern const wchar_t g_szError[];

const wchar_t g_szRTProfileDisplayControlName[]=L"phaeronRTProfileDisplay";

/////////////////////////////////////////////////////////////////////////////

class VDRTProfileDisplay : public IVDRTProfileDisplay {
public:
	VDRTProfileDisplay(HWND hwnd);
	~VDRTProfileDisplay();

	static VDRTProfileDisplay *Create(HWND hwndParent, int x, int y, int cx, int cy, UINT id);

	static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

public:
	void SetProfiler(VDRTProfiler *pProfiler);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	void OnSize(int w, int h);
	void OnPaint();
	void OnSetFont(HFONT hfont, bool bRedraw);
	bool OnKey(INT wParam);
	void OnTimer();

protected:
	typedef VDRTProfiler::Event	Event;
	typedef VDRTProfiler::Channel	Channel;

	const HWND mhwnd;
	HFONT		mhfont;
	VDRTProfiler	*mpProfiler;

	int			mDisplayResolution;

	int			mFontHeight;
	int			mFontAscent;
	int			mFontInternalLeading;
};

/////////////////////////////////////////////////////////////////////////////

VDRTProfileDisplay::VDRTProfileDisplay(HWND hwnd)
	: mhwnd(hwnd)
	, mhfont(0)
	, mpProfiler(0)
	, mDisplayResolution(4)
{
}

VDRTProfileDisplay::~VDRTProfileDisplay() {
	if (mpProfiler)
		mpProfiler->EndCollection();
}

VDRTProfileDisplay *VDRTProfileDisplay::Create(HWND hwndParent, int x, int y, int cx, int cy, UINT id) {
	HWND hwnd = CreateWindowW(g_szRTProfileDisplayControlName, L"", WS_VISIBLE|WS_CHILD, x, y, cx, cy, hwndParent, (HMENU)(UINT_PTR)id, g_hInst, NULL);

	if (hwnd) {
		return (VDRTProfileDisplay*)GetWindowLongPtrW(hwnd, 0);
	}

	return NULL;
}

ATOM RegisterRTProfileDisplayControl() {
	WNDCLASSW wc;

	wc.style         = 0;
	wc.lpfnWndProc   = VDRTProfileDisplay::StaticWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = sizeof(VDRTProfileDisplay *);
	wc.hInstance     = g_hInst;
	wc.hIcon         = NULL;
	wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = g_szRTProfileDisplayControlName;

	return RegisterClassW(&wc);
}

IVDRTProfileDisplay *VDGetIRTProfileDisplayControl(HWND hwnd) {
	return static_cast<IVDRTProfileDisplay*>(reinterpret_cast<VDRTProfileDisplay*>(GetWindowLongPtrW(hwnd, 0)));
}

/////////////////////////////////////////////////////////////////////////////

void VDRTProfileDisplay::SetProfiler(VDRTProfiler *pProfiler) {
	if (mpProfiler)
		mpProfiler->EndCollection();

	mpProfiler = pProfiler;
	InvalidateRect(mhwnd, NULL, TRUE);

	if (mpProfiler)
		mpProfiler->BeginCollection();
}

/////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK VDRTProfileDisplay::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	VDRTProfileDisplay* pThis = (VDRTProfileDisplay*)GetWindowLongPtrW(hwnd, 0);

	switch(msg) {
	case WM_NCCREATE:
		if (!(pThis = new_nothrow VDRTProfileDisplay(hwnd)))
			return FALSE;

		SetWindowLongPtrW(hwnd, 0, (LONG_PTR)pThis);
		break;

	case WM_NCDESTROY:
		delete pThis;
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	if (pThis) {
		return pThis->WndProc(msg, wParam, lParam);
	} else {
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

LRESULT VDRTProfileDisplay::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_CREATE:
		OnSetFont(NULL, FALSE);
		SetTimer(mhwnd, 1, 1000, NULL);
		VDSetDialogDefaultIcons(mhwnd);
		return 0;
	case WM_SIZE:
		OnSize(LOWORD(lParam), HIWORD(lParam));
		return 0;
	case WM_PAINT:
		OnPaint();
		return 0;
	case WM_ERASEBKGND:
		return 0;
	case WM_SETFONT:
		OnSetFont((HFONT)wParam, lParam != 0);
		return 0;
	case WM_GETFONT:
		return (LRESULT)mhfont;
	case WM_KEYDOWN:
		if (OnKey(wParam))
			return 0;
		break;
	case WM_TIMER:
		OnTimer();
		return 0;
	case WM_GETDLGCODE:
		return DLGC_WANTARROWS;
	}
	return DefWindowProcW(mhwnd, msg, wParam, lParam);
}

void VDRTProfileDisplay::OnSize(int w, int h) {
}

void VDRTProfileDisplay::OnPaint() {
	PAINTSTRUCT ps;
	if (HDC hdc = BeginPaint(mhwnd, &ps)) {
		HBRUSH hbrBackground = (HBRUSH)GetClassLongPtrW(mhwnd, GCLP_HBRBACKGROUND);
		HGDIOBJ hOldFont = 0;
		if (mhfont)
			hOldFont = SelectObject(hdc, mhfont);

		RECT rClient;
		GetClientRect(mhwnd, &rClient);

		FillRect(hdc, &rClient, hbrBackground);

		if (mpProfiler) {
			int i;
			const int nChannels = mpProfiler->mChannelArrayToPaint.size();
			int maxwidth = 0;

			SetBkMode(hdc, TRANSPARENT);
			for(i=0; i<nChannels; ++i) {
				const Channel& chan = mpProfiler->mChannelArrayToPaint[i];
				if (!chan.mpName)
					continue;

				SIZE siz;
				GetTextExtentPoint32A(hdc, chan.mpName, strlen(chan.mpName), &siz);
				if (siz.cx > maxwidth)
					maxwidth = siz.cx;
			}

			const int left = maxwidth + 16;
			const int width = rClient.right - left;
			const int nChannelHt = mFontHeight * 2;
			const int nTextYOffset = (nChannelHt - mFontAscent) / 2;
			int y = nChannelHt;

			SetBkColor(hdc, RGB(0,0,0));
			for(i=0; i<10; ++i) {
				const int x = left + width*i/10;
				RECT rMarker = { x, 0, x+1, nChannelHt * (nChannels+1) };
				char buf[32];

				sprintf(buf, "%d ms", i * (100 << mDisplayResolution) / 64);

				ExtTextOutA(hdc, 0, 0, ETO_CLIPPED|ETO_OPAQUE, &rMarker, "", 0, NULL);
				ExtTextOutA(hdc, x + 4, nTextYOffset, 0, NULL, buf, strlen(buf), NULL);
			}

			SetTextAlign(hdc, TA_TOP | TA_LEFT);

			const sint64 period = (mpProfiler->mPerfFreq << mDisplayResolution) >> 6;
			const sint64 startTime	= mpProfiler->mSnapshotTime - period;

			for(i=0; i<nChannels; ++i) {
				const Channel& chan = mpProfiler->mChannelArrayToPaint[i];

				if (!chan.mpName)
					continue;

				RECT rText = { 0, y, maxwidth, y + nChannelHt};

				ExtTextOutA(hdc, 0, y + nTextYOffset, ETO_CLIPPED, &rText, chan.mpName, strlen(chan.mpName), NULL);

				for(vdfastvector<Event>::const_iterator it(chan.mEventList.begin()), itEnd(chan.mEventList.end()); it!=itEnd; ++it) {
					const Event& ev = *it;

					if ((sint64)(ev.mEndTime - startTime) < 0)
						continue;

					const int x1 = left + std::max<int>(0, (int)((ev.mStartTime - startTime) * width / period));
					const int x2 = std::max<int>(x1+1, left + (int)((ev.mEndTime - startTime) * width / period));

					SetBkColor(hdc, ev.mColor);

					RECT rBack = { x1, y, x2, y+nChannelHt };
					ExtTextOutA(hdc, x1, y + nTextYOffset, ETO_OPAQUE | ETO_CLIPPED, &rBack, ev.mpName, strlen(ev.mpName), NULL);
				}

				y += nChannelHt;
			}

			char buf[1024];
			uint32 counterCount = mpProfiler->mCounterArray.size();
			const VDRTProfiler::Counter *ctr = mpProfiler->mCounterArrayToPaint.data();

			int xbreak = rClient.right * 2 / 3;
			for(uint32 i=0; i<counterCount; ++i, ++ctr) {
				ExtTextOutA(hdc, 0, y, 0, NULL, ctr->mpName, strlen(ctr->mpName), NULL);

				switch(ctr->mType) {
					case VDRTProfiler::kCounterTypeUint32:
						sprintf(buf, "%u (%d)", ctr->mData.u32, (sint32)ctr->mData.u32 - (sint32)ctr->mDataLast.u32);
						break;
					case VDRTProfiler::kCounterTypeDouble:
						sprintf(buf, "%g (%g)", ctr->mData.d, ctr->mData.d - ctr->mDataLast.d);
						break;
					default:
						buf[0] = 0;
				}

				ExtTextOutA(hdc, xbreak, y, 0, NULL, buf, strlen(buf), NULL);

				y += mFontHeight;
			}
		}

		if (hOldFont)
			SelectObject(hdc, mhfont);

		EndPaint(mhwnd, &ps);
	}
}

void VDRTProfileDisplay::OnTimer() {
	if (mpProfiler) {
		mpProfiler->Swap();
		InvalidateRect(mhwnd, NULL, TRUE);
		UpdateWindow(mhwnd);
	}
}

void VDRTProfileDisplay::OnSetFont(HFONT hfont, bool bRedraw) {
	mhfont = hfont;
	if (bRedraw)
		InvalidateRect(mhwnd, NULL, TRUE);

	// stuff in some reasonable defaults if we fail measuring font metrics
	mFontHeight				= 16;
	mFontAscent				= 12;
	mFontInternalLeading	= 0;

	// measure font metrics
	if (HDC hdc = GetDC(mhwnd)) {
		HGDIOBJ hOldFont = 0;
		if (mhfont)
			hOldFont = SelectObject(hdc, mhfont);

		TEXTMETRICW tm;
		if (GetTextMetricsW(hdc, &tm)) {
			mFontHeight				= tm.tmHeight;
			mFontAscent				= tm.tmAscent;
			mFontInternalLeading	= tm.tmInternalLeading;
		}

		if (hOldFont)
			SelectObject(hdc, mhfont);
	}
}

bool VDRTProfileDisplay::OnKey(INT wParam) {
	if (wParam == VK_LEFT) {
		if (mDisplayResolution > 0) {
			--mDisplayResolution;
			InvalidateRect(mhwnd, NULL, TRUE);
		}
	} else if (wParam == VK_RIGHT) {
		if (mDisplayResolution < 6) {
			++mDisplayResolution;
			InvalidateRect(mhwnd, NULL, TRUE);
		}
	}
	return false;
}



///////////////////////////////////////////////////////////////////////////
//
//	test harness
//
///////////////////////////////////////////////////////////////////////////

#if 0

namespace {
	struct TestHarness {
		TestHarness() {
			RegisterRTProfileDisplayControl();
			HWND foo = CreateWindowW(RTPROFILEDISPLAYCONTROLCLASS, L"Profile window", WS_OVERLAPPEDWINDOW|WS_VISIBLE, 0, 0, 400, 300, NULL, NULL, (HINSTANCE)GetModuleHandleW(nullptr), 0);
			SendMessageW(foo, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

			VDRTProfiler mProfiler;
			IVDRTProfileDisplay *pDisp = VDGetIRTProfileDisplayControl(foo);

			mProfiler.InitChannel(0, "Test1");
			mProfiler.InitChannel(1, "Test2");

			pDisp->SetProfiler(&mProfiler);

			bool state[2]={false};

			for(;;) {
				MSG msg;
				while(PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
					TranslateMessage(&msg);
					DispatchMessageW(&msg);
				}
				static DWORD dwLastTime = 0;
				DWORD dwCurTime = GetTickCount()/10;

				if (dwCurTime == dwLastTime)
					Sleep(1);
				else {
					dwLastTime = dwCurTime;

					if ((rand() & 255) < 100) {
						int ch = rand() & 1;

						if (state[ch]) {
							mProfiler.EndEvent(ch);
							state[ch] = false;
						} else {
							mProfiler.BeginEvent(ch, 0xffe0e0, "Event");
							state[ch] = true;
						}
					}
				}
			}
		}
	} g_RTProfileDisplayTestHarness;
}

#endif

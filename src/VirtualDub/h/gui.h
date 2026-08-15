// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2018-2020 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_GUI_H
#define f_GUI_H

#include <windows.h>
#include <vd2/system/list.h>
#include <vd2/Dita/interface.h>
#include "LogWindow.h"

#define IDC_CAPTURE_WINDOW		(500)
#define IDC_STATUS_WINDOW		(501)

class IVDPositionControl;
class VDTimeline;

class ModelessDlgNode : public ListNode2<ModelessDlgNode> {
public:
	HWND hdlg       = NULL;
	HACCEL mhAccel  = NULL;
	bool hook       = false;
	bool edit_thunk = false;

	ModelessDlgNode() = default;
	ModelessDlgNode(HWND _hdlg, HACCEL hAccel = NULL) : hdlg(_hdlg), mhAccel(hAccel) {}
};

void guiOpenDebug();

bool guiDlgMessageLoop(HWND hDlg, int *errorCode = NULL);
bool guiCheckDialogs(LPMSG pMsg);
void guiAddModelessDialog(ModelessDlgNode *pmdn);
void VDInstallModelessDialogHookW32();
void VDDeinstallModelessDialogHookW32();

struct VDUISavedWindowPlacement {
	sint32 mLeft;
	sint32 mTop;
	sint32 mRight;
	sint32 mBottom;
	uint8 mbMaximized;
	uint8 mPad[3];
};

void VDUISaveWindowPlacementW32(HWND hwnd, const char *name);
void VDUIRestoreWindowPlacementW32(HWND hwnd, const char *name, int nCmdShow);
bool VDUIGetWindowPlacementW32(VDUISavedWindowPlacement& sp, const char *name);
void VDUIDeleteWindowPlacementW32(const char *name);
void VDUISaveListViewColumnsW32(HWND hwnd, const char *name);
void VDUIRestoreListViewColumnsW32(HWND hwnd, const char *name);

void VDUISetListViewColumnsW32(HWND hwnd, const float *relwidths, int count);

void VDSetDialogDefaultIcons(HWND hdlg);

void guiSetStatus(const wchar_t* format, int nPart, ...);
void guiSetStatus(const char *format, int nPart, ...);
void guiSetTitle(HWND hWnd, UINT uID, ...);
void guiMenuHelp(HWND hwnd, WPARAM wParam, WPARAM part, const UINT *iTranslator);
void guiOffsetDlgItem(HWND hdlg, UINT id, LONG xDelta, LONG yDelta);
void guiResizeDlgItem(HWND hdlg, UINT id, LONG x, LONG y, LONG dx, LONG dy);
void guiSubclassWindow(HWND hwnd, WNDPROC newproc);

void guiPositionInitFromStream(IVDPositionControl *pc);
void VDTranslatePositionCommand(HWND hwnd, WPARAM wParam, LPARAM lParam);
bool VDHandleTimelineCommand(IVDPositionControl *pc, VDTimeline *pTimeline, UINT cmd);
VDPosition guiPositionHandleCommand(WPARAM wParam, IVDPositionControl *pc);
VDPosition guiPositionHandleNotify(LPARAM lParam, IVDPositionControl *pc);
void guiPositionBlit(HWND hWndClipping, VDPosition lFrame, int w=0, int h=0);

bool guiChooseColor(HWND hwnd, COLORREF& rgbOld);


enum {
	REPOS_NOMOVE		= 0,
	REPOS_MOVERIGHT		= 1,
	REPOS_MOVEDOWN		= 2,
	REPOS_SIZERIGHT		= 4,
	REPOS_SIZEDOWN		= 8
};

struct ReposItem {
	UINT uiCtlID;
	int fReposOpts;
};

void guiReposInit(HWND hwnd, struct ReposItem *lpri, POINT *lppt);
void guiReposResize(HWND hwnd, struct ReposItem *lpri, POINT *lppt);

HDWP guiDeferWindowPos(HDWP hdwp, HWND hwnd, HWND hwndInsertAfter, int x, int y, int dx, int dy, UINT flags);
void guiEndDeferWindowPos(HDWP hdwp);
int guiMessageBoxF(HWND hwnd, LPCSTR lpCaption, UINT uType, const char* format, ...);
int guiMessageBoxF(HWND hwnd, LPCWSTR lpCaption, UINT uType, const wchar_t* format, ...);

void ticks_to_str(char *dst, size_t bufsize, uint32 ticks);
void ticks_to_str(wchar_t *dst, size_t bufsize, uint32 ticks);
void size_to_str(char *dst, size_t bufsize, sint64 bytes);
void size_to_str(wchar_t *dst, size_t bufsize, sint64 bytes);

int guiListboxInsertSortedString(HWND, const char*);
int guiListboxInsertSortedString(HWND, const wchar_t*);

///////////////////////////////////////////////////////////////////////////////

class VDAutoLogDisplay {
public:
	VDAutoLogDisplay();
	~VDAutoLogDisplay();

	void Post(VDGUIHandle hParent);

protected:
	VDAutoLogger		mLogger;

	static INT_PTR CALLBACK DlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam);
};

///////////////////////////////////////////////////////////////////////////////

class VDDialogBase : public IVDUICallback {
protected:
	int GetValue(uint32 id) const {
		IVDUIWindow *pWin = mpBase->GetControl(id);
		return pWin ? pWin->GetValue() : 0;
	}

	void SetValue(uint32 id, int value) {
		IVDUIWindow *pWin = mpBase->GetControl(id);
		if (pWin)
			pWin->SetValue(value);
	}

	const VDStringW GetCaption(uint32 id) const {
		IVDUIWindow *pWin = mpBase->GetControl(id);
		return pWin ? pWin->GetCaption() : VDStringW();
	}

	void SetCaption(uint32 id, const wchar_t *s) {
		IVDUIWindow *pWin = mpBase->GetControl(id);
		if (pWin)
			pWin->SetCaption(s);
	}

	IVDUIBase *mpBase;
};

class VDDialogBaseW32 {
public:
	bool IsActive() { return mhdlg != 0; }
	HWND	GetHandle() const { return mhdlg; }

	LRESULT ActivateDialog(VDGUIHandle hParent);
	bool CreateModeless(VDGUIHandle hParent);
	void DestroyModeless();
protected:
	VDDialogBaseW32(UINT dlgid);
	~VDDialogBaseW32();

	static INT_PTR CALLBACK StaticDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	virtual INT_PTR DlgProc(UINT msg, WPARAM wParam, LPARAM lParam) = 0;
	void End(LRESULT res);

	virtual bool PreNCDestroy() { return false; }

	HWND		mhdlg;
	LPCWSTR		mpszDialogName;
};

#endif

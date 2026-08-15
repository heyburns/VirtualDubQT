// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2011 Avery Lee
// Copyright (C) 2015-2018 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <vd2/system/date.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/VDLib/Dialog.h>
#include <vd2/VDLib/UIProxies.h>
#include "script.h"
#include "project.h"
#include "oshelper.h"
#include "resource.h"

extern const wchar_t g_szError[];
extern const wchar_t g_szWarning[];
extern VDProject *g_project;

///////////////////////////////////////////////////////////////////////////

class VDUIDialogAutoRecover : public VDDialogFrameW32 {
public:
	VDUIDialogAutoRecover();
	~VDUIDialogAutoRecover();

	const wchar_t *GetRecoveryPath() const {
		return mRecoveryPath.empty() ? NULL : mRecoveryPath.c_str();
	}

	bool Scan(const wchar_t *path);

protected:
	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);

	void OnItemSelectionChanged(VDUIProxyListView *source, int index);

	void UpdateEnables();

	struct FileItem : public vdrefcounted<IVDUIListViewVirtualItem> {
		VDStringW mFullPath;
		VDStringW mDate;
		VDStringW mTime;

		void GetText(int subItem, VDStringW& s) const;
	};

	struct FileItemPred {
		bool operator()(const FileItem *x, const FileItem *y) const {
			return x->mFullPath.comparei(y->mFullPath) < 0;
		}
	};

	typedef vdfastvector<FileItem *> FileItems;
	FileItems mFileItems;

	VDUIProxyListView mList;
	VDDelegate mDelItemSelectionChanged;

	VDStringW	mRecoveryPath;
};

void VDUIDialogAutoRecover::FileItem::GetText(int subItem, VDStringW& s) const {
	switch(subItem) {
		case 0:
			s = VDFileSplitPath(mFullPath.c_str());
			break;

		case 1:
			s = mDate;
			break;

		case 2:
			s = mTime;
			break;
	}
}

VDUIDialogAutoRecover::VDUIDialogAutoRecover()
	: VDDialogFrameW32(IDD_AUTORECOVER)
{
	mList.OnItemSelectionChanged() += mDelItemSelectionChanged.Bind(this, &VDUIDialogAutoRecover::OnItemSelectionChanged);
}

VDUIDialogAutoRecover::~VDUIDialogAutoRecover() {
	while(!mFileItems.empty()) {
		FileItem *fi = mFileItems.back();
		fi->Release();
		mFileItems.pop_back();
	}
}

bool VDUIDialogAutoRecover::Scan(const wchar_t *path) {
	VDDirectoryIterator it(VDMakePath(path, L"VirtualDub_AutoSave_*.vdproject").c_str());

	while(it.Next()) {
		if (it.IsDirectory())
			continue;

		// attempt to open the file; if we get a sharing violation, the
		// process is still live
		VDStringW path(it.GetFullPath());
		VDFile f;
		if (f.openNT(path.c_str())) {
			f.close();

			vdrefptr<FileItem> fi(new FileItem);
			fi->mFullPath.swap(path);

			VDExpandedDate ed(VDGetLocalDate(it.GetLastWriteDate()));
			VDAppendLocalDateString(fi->mDate, ed);
			VDAppendLocalTimeString(fi->mTime, ed);
			mFileItems.push_back(fi);
			fi.release();
		}
	}

	return !mFileItems.empty();
}

bool VDUIDialogAutoRecover::OnLoaded() {
	AddProxy(&mList, IDC_LIST);

	mList.SetFullRowSelectEnabled(true);
	mList.InsertColumn(0, L"Filename", 0);
	mList.InsertColumn(1, L"Date", 0);
	mList.InsertColumn(2, L"Time", 0);

	std::sort(mFileItems.begin(), mFileItems.end(), FileItemPred());

	for(FileItems::const_iterator it(mFileItems.begin()), itEnd(mFileItems.end()); it != itEnd; ++it) {
		FileItem *item = *it;

		mList.InsertVirtualItem(-1, item);
	}

	mList.AutoSizeColumns();

	UpdateEnables();

	SetFocusToControl(IDC_LIST);
	return true;
}

void VDUIDialogAutoRecover::OnDestroy() {
	mList.Clear();
}

void VDUIDialogAutoRecover::OnDataExchange(bool write) {
	if (write) {
		FileItem *fi = static_cast<FileItem *>(mList.GetSelectedItem());

		if (fi)
			mRecoveryPath = fi->mFullPath;
		else
			mRecoveryPath.clear();
	}
}

bool VDUIDialogAutoRecover::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case IDC_DELETE:
			if (FileItem *fi = static_cast<FileItem *>(mList.GetSelectedItem())) {
				if (Confirm(L"Are you sure you want to delete this recovery file?", g_szWarning)) {
					try {
						VDProject::DeleteProject(fi->mFullPath);
					} catch(const MyError& e) {
						e.post(mhdlg, g_szError);
					}

					mList.DeleteItem(mList.GetSelectedIndex());
				}
			}
			return true;

		case IDC_DELETE_ALL:
			if (Confirm(L"Are you sure you want to delete ALL recovery files?", g_szWarning)) {
				mList.Clear();

				while(!mFileItems.empty()) {
					FileItem *fi = mFileItems.back();

					try {
						VDProject::DeleteProject(fi->mFullPath);
					} catch(const MyError&) {
					}

					fi->Release();

					mFileItems.pop_back();
				}
				End(false);
			}

			return true;
	}

	return false;
}

void VDUIDialogAutoRecover::OnItemSelectionChanged(VDUIProxyListView *source, int index) {
	UpdateEnables();
}

void VDUIDialogAutoRecover::UpdateEnables() {
	bool selPresent = mList.GetSelectedIndex() >= 0;

	EnableControl(IDC_DELETE, selPresent);
	EnableControl(IDOK, selPresent);
}

///////////////////////////////////////////////////////////////////////////

VDStringW VDUIGetAutoRecoverPath() {
	return VDStringW(VDGetDataPath());
}

void VDUICheckForAutoRecoverFiles(VDGUIHandle h) {
	VDUIDialogAutoRecover dlg;

	bool filesAvailable = false;

	try {
		filesAvailable = dlg.Scan(VDUIGetAutoRecoverPath().c_str());
	} catch(const MyError&) {
		// just eat the error
	}

	if (!filesAvailable)
		return;

	if (!dlg.ShowDialog(h))
		return;

	const wchar_t *path = dlg.GetRecoveryPath();

	if (!path)
		return;

	try {
		g_project->OpenProject(path, true);

		try {
			VDProject::DeleteProject(VDStringW(path));
		} catch(const MyError&) {
			// eat the error
		}
	} catch(const MyError& e) {
		MyError e2;
		e2.setf("The following error occurred while processing the recovery file. The recovery file will be preserved in case of another attempt.\n\n%s", e.gets());

		e2.post((VDZHWND)h, g_szError);
	}
}

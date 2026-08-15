// VirtualDub - Video processing and capture application
//
// Copyright (C) 2016-2019 Anton Shekhovtsov
// Copyright (C) 2025-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <vd2/plugin/vdtool.h>
#include <vd2/system/filesys.h>
#include "projectui.h"
#include "JobControl.h"
#include "plugins.h"
#include "inputfile.h"
#include "resource.h"
#include "tool.h"

extern VDProject *g_project;
extern vdrefptr<VDProjectUI> g_projectui;
extern wchar_t g_szInputAVIFile[MAX_PATH];
extern VDJobQueue g_VDJobQueue;
extern bool g_consoleMode;

extern vdfastvector<VDAccelToCommandEntry> kCommandList;

#define MYWM_DEFERRED_FILECOMMAND (WM_USER + 103)

void ResetCommandList();
bool FiltersEditorGotoFrame(VDPosition pos, VDPosition time);

class VDToolTimeline: public IVDTimeline {
public:
	int64 GetTimelinePos() {
		return g_project->GetCurrentFrame();
	}

	void GetSelection(int64& start, int64& end) {
		start = g_project->GetSelectionStartFrame();
		end = g_project->GetSelectionEndFrame();
	}

	int GetSubsetCount() {
		FrameSubset& s = g_project->GetTimeline().GetSubset();
		FrameSubset::iterator p;
		int count = 0;
		for(p=s.begin(); p!=s.end(); p++) count++;
		return count;
	}

	void GetSubsetRange(int i, int64& start, int64& end) {
		FrameSubset& s = g_project->GetTimeline().GetSubset();
		FrameSubset::iterator p;
		int count = 0;
		for(p=s.begin(); p!=s.end(); p++){
			if (count==i) {
				start = p->start;
				end = p->start+p->len;
				break;
			}
			count++;
		}
	}

	void SetTimelinePos(int64 pos) {
		VDFraction fr = g_projectui->GetTimelineFrameRate();
		VDPosition time = -1;
		if (fr.getLo() && fr.getHi())
			time = fr.scale64ir(pos*1000000);

		if (FiltersEditorGotoFrame(pos,time))
			return;
		g_project->MoveToFrame(pos);
	}
};

class VDToolCallbacks: public IVDToolCallbacks {
public:
	void* object;
	VDToolTimeline timeline;

	VDToolCallbacks(){ object=0; }

	size_t GetFileName(wchar_t* buf, size_t n) {
		VDStringW filename(VDGetFullPath(g_szInputAVIFile));
		if (filename.length()>n+1) {
			buf[0] = 0;
			return filename.length();
		}
		wcscpy(buf,filename.c_str());
		return filename.length();
	}

	void SetFileName(const wchar_t* s, const wchar_t* driverName, void* userData) {
		FileNameCommand* cmd = new FileNameCommand;
		cmd->fileName = s;
		cmd->object = object;
		cmd->userData = userData;
		if (driverName) {
			cmd->driverName = driverName;
		}
		PostMessageW(g_projectui->GetHwnd(), MYWM_DEFERRED_FILECOMMAND, 0, (LPARAM)cmd);
	}

	void Reopen(void* userData) {
		Reopen(0, 0, userData);
	}

	void Reopen(const wchar_t* fileName, const wchar_t* driverName, void* userData) {
		FileNameCommand* cmd = new FileNameCommand;
		cmd->object = object;
		cmd->userData = userData;
		cmd->reopen = true;
		if (fileName) {
			cmd->fileName = fileName;
		}
		if (driverName) {
			cmd->fileName = driverName;
		}
		PostMessageW(g_projectui->GetHwnd(), MYWM_DEFERRED_FILECOMMAND, 0, (LPARAM)cmd);
	}

	IVDTimeline* GetTimeline() {
		return &timeline;
	}
};

struct VDTool {
	vdrefptr<IVDXTool> object;
	int version;
	int command_first;
	int command_last;
	vdvector<VDStringA> strings;
	VDXToolContext context;
	VDToolCallbacks callbacks;

	VDTool() {
		context.mAPIVersion = kVDXPlugin_ToolAPIVersion;
		context.mpCallbacks = &callbacks;
		callbacks.object = this;
	}
};

std::vector<VDTool*> g_VDTools;

void VDShutdownTools() {
	for(auto it(g_VDTools.cbegin()), itEnd(g_VDTools.cend()); it!=itEnd; ++it) {
		VDTool *p = *it;
		delete p;
	}
	g_VDTools.clear();
}

void VDInitTools() {
	VDShutdownTools();

	std::vector<VDPluginDescription *> plugins;
	VDEnumeratePluginDescriptions(plugins, kVDXPluginType_Tool);

	int command = ID_PLUGIN_TOOL;
	ResetCommandList();

	while(!plugins.empty()) {
		VDPluginDescription *desc = plugins.back();
		const VDPluginInfo *info = VDLockPlugin(desc);
		const VDXToolDefinition* def = static_cast<const VDXToolDefinition *>(info->mpTypeSpecificInfo);
		vdrefptr<IVDXTool> object;
		VDTool* tool = new VDTool;
		def->mpCreate(&tool->context, ~object);
		tool->object = object;
		tool->version = info->mTypeAPIVersionUsed;
		tool->command_first = command;
		tool->command_last = -1;
		{for(int id=0; ; id++) {
			char name[256];
			if (!object->GetCommandId(id,name,sizeof(name))) break;

			tool->command_last = command;
			VDStringA s(name);
			tool->strings.push_back(s);
			VDAccelToCommandEntry cmd;
			cmd.mId = command;
			cmd.mpName = tool->strings.back().c_str();
			kCommandList.push_back(cmd);

			command++;
		}}

		g_VDTools.push_back(tool);
		plugins.pop_back();
	}

	if (g_projectui)
		g_projectui->UpdateAccelMain();
}

void VDToolInsertMenu(HMENU menu, int pos) {
	for(auto it(g_VDTools.cbegin()), itEnd(g_VDTools.cend()); it!=itEnd; ++it) {
		VDTool *p = *it;
		{for(int id=0; ; id++) {
			char name[256];
			bool enabled = true;
			if (!p->object->GetMenuInfo(id, name, sizeof(name), &enabled)) {
				break;
			}

			MENUITEMINFOA mii = {0};
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
			mii.fType = MFT_STRING;
			mii.fState = enabled ? 0 : MFS_DISABLED;
			mii.wID	= p->command_first-id;
			mii.dwTypeData	= name;
			InsertMenuItemA(menu, pos, TRUE, &mii);
			pos++;
		}}
	}
}

void VDToolExecuteCommand(int id, HWND parent) {
	for(auto it(g_VDTools.cbegin()), itEnd(g_VDTools.cend()); it!=itEnd; ++it) {
		VDTool *p = *it;
		if (id >= p->command_first && id <= p->command_last) {
			p->object->ExecuteMenu(id-p->command_first,(VDXHWND)parent);
			break;
		}
	}
}

bool VDToolCatchError(FileNameCommand* cmd, const MyError& e) {
	VDTool* tool = (VDTool*)cmd->object;
	return tool->object->HandleError(VDTextWToA(e.c_str()).c_str(), 0, cmd->userData);
}

bool VDCheckToolsDialogs(LPMSG pMsg) {
	for(auto it(g_VDTools.cbegin()), itEnd(g_VDTools.cend()); it!=itEnd; ++it) {
		VDTool *p = *it;
		if (p->object->TranslateMessage(*pMsg)) {
			return true;
		}
	}
	return false;
}

void VDToolsHandleFileOpen(const wchar_t* fname, IVDInputDriver *pDriver) {
	if (g_VDJobQueue.IsRunInProgress())
		return;

	const wchar_t* driver_name = pDriver->GetSignatureName();
	HWND parent = g_projectui->GetHwnd();

	for(auto it(g_VDTools.cbegin()), itEnd(g_VDTools.cend()); it!=itEnd; ++it) {
		VDTool *p = *it;
		p->object->HandleFileOpen(fname, driver_name, (VDXHWND)parent);
	}
}

bool VDToolsHandleFileOpenError(const wchar_t* fname, const wchar_t* driver_name, const MyError& e, int line) {
	if (g_VDJobQueue.IsRunInProgress())
		return false;

	if (g_consoleMode)
		return false;

	HWND parent = g_projectui->GetHwnd();

	for(auto it(g_VDTools.cbegin()), itEnd(g_VDTools.cend()); it!=itEnd; ++it) {
		VDTool *p = *it;
		if (p->version < 2) {
			continue;
		}
		if (p->object->HandleFileOpenError(fname, driver_name, (VDXHWND)parent, VDTextWToU8(e.c_str()).c_str(), line)) {
			return true;
		}
	}

	return false;
}

void VDToolsAttach(HWND hwnd) {
	for(auto it(g_VDTools.cbegin()), itEnd(g_VDTools.cend()); it!=itEnd; ++it) {
		VDTool *p = *it;
		p->object->Attach((VDXHWND)hwnd);
	}
}

void VDToolsDetach(HWND hwnd) {
	for(auto it(g_VDTools.cbegin()), itEnd(g_VDTools.cend()); it!=itEnd; ++it) {
		VDTool *p = *it;
		p->object->Detach((VDXHWND)hwnd);
	}
}

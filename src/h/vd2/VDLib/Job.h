// VirtualDub - Video processing and capture application
//
// Copyright (C) 2013 Avery Lee
// Copyright (C) 2015-2019 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDLIB_JOB_H
#define f_VD2_VDLIB_JOB_H

#include <vd2/system/VDString.h>
#include <vd2/system/log.h>

class VDJob;

class IVDJobQueue {
public:
	virtual bool IsLocal(const VDJob *job) const = 0;
	virtual void Refresh(VDJob *job) = 0;
	virtual void Run(VDJob *job) = 0;
	virtual void Reload(VDJob *job) = 0;
};

class VDJob {
public:
	enum {
		kStateWaiting		= 0,
		kStateInProgress	= 1,
		kStateCompleted		= 2,
		kStatePostponed		= 3,
		kStateAborted		= 4,
		kStateError			= 5,
		kStateAborting		= 6,
		kStateStarting		= 7,
		kStateCount			= 8
	};

	IVDJobQueue* mpJobQueue = nullptr;

	uint32		mCreationRevision = 0;
	uint32		mChangeRevision   = 0;
	uint64		mId        = 0;
	uint64		mDateStart = 0;	///< Same units as NT FILETIME.
	uint64		mDateEnd   = 0;	///< Same units as NT FILETIME.

	typedef VDAutoLogger::tEntries tLogEntries;
	tLogEntries	mLogEntries;

	/////
	VDJob() = default;
	~VDJob() = default;

	bool operator==(const VDJob& job) const;

	bool	IsLocal() const { return !mpJobQueue || mpJobQueue->IsLocal(this); }

	const char *	GetName() const				{ return mName.c_str(); }
	void			SetName(const char *name)	{ mName = name; }

	const char *	GetProjectSubdir() const			{ return mProjectSubdir.c_str(); }
	void			SetProjectSubdir(const char *name)	{ mProjectSubdir = name; }

	const char *	GetProjectDir() const			{ return mProjectDir.c_str(); }
	void			SetProjectDir(const char *name)	{ mProjectDir = name; }

	const wchar_t*	GetInputFile() const				{ return mInputFile.c_str(); }
	void			SetInputFile(const wchar_t* file)	{ mInputFile = file; }

	const wchar_t*	GetOutputFile() const				{ return mOutputFile.c_str(); }
	void			SetOutputFile(const wchar_t* file)	{ mOutputFile = file; }

	const wchar_t*	GetError() const					{ return mError.c_str(); }
	void			SetError(const wchar_t* err)		{ mError = err; }

	const wchar_t*	GetRunnerName() const			{ return mRunnerName.c_str(); }
	uint64			GetRunnerId() const				{ return mRunnerId; }

	bool	IsRunning() const { return mState == kStateInProgress || mState == kStateAborting; }
	bool	IsModified() const { return mbModified; }
	void	SetModified(bool mod) { mbModified = mod; }

	int		GetState() const { return mState; }
	void	SetState(int state);

	uint64	GetId() const { return mId; }

	void	SetRunner(uint64 id, const wchar_t* name);

	bool	IsReloadMarkerPresent() const { return mbContainsReloadMarker; }

	void	SetScript(const void *script, uint32 len, int line, bool reloadable);
	const char *GetScript() const { return mScript.c_str(); }
	int GetScriptLine() const { return mScriptLine; }

	void Refresh();
	void Run();
	void Reload();

	bool Merge(const VDJob& src);

	VDStringA	ToString() const;

	uint64		mRunnerId = 0;
	VDStringW	mRunnerName;
protected:
	VDStringA	mName;
	VDStringW	mInputFile;
	VDStringW	mOutputFile;
	VDStringW	mError;
	VDStringA	mScript;
	int mScriptLine = -1;
	VDStringA	mProjectSubdir;
	VDStringA	mProjectDir;
	int			mState = VDJob::kStateWaiting;
	bool		mbContainsReloadMarker = false;
	bool		mbModified = false;
};

#endif

// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2019 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_FILESYS_H
#define f_VD2_SYSTEM_FILESYS_H

#include <ctype.h>
#include <vector>

#include <vd2/system/date.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>

// VDFileSplitPath returns a pointer to the first character of the filename,
// or the beginning of the string if the path only contains one component.

const char *VDFileSplitFirstDir(const char *s);
const wchar_t *VDFileSplitFirstDir(const wchar_t *s);

static inline char *VDFileSplitFirstDir(char *s) {
	return const_cast<char *>(VDFileSplitFirstDir(const_cast<const char *>(s)));
}

static inline wchar_t *VDFileSplitFirstDir(wchar_t *s) {
	return const_cast<wchar_t *>(VDFileSplitFirstDir(const_cast<const wchar_t *>(s)));
}

const char *VDFileSplitPath(const char *);
const wchar_t *VDFileSplitPath(const wchar_t *);

static inline char *VDFileSplitPath(char *s) {
	return const_cast<char *>(VDFileSplitPath(const_cast<const char *>(s)));
}

static inline wchar_t *VDFileSplitPath(wchar_t *s) {
	return const_cast<wchar_t *>(VDFileSplitPath(const_cast<const wchar_t *>(s)));
}

VDStringA VDFileSplitPathLeft(const VDStringA&);
VDStringA VDFileSplitPathRight(const VDStringA&);
VDStringW VDFileSplitPathLeft(const VDStringW&);
VDStringW VDFileSplitPathRight(const VDStringW&);

// VDSplitRoot returns a pointer to the second component of the filename,
// or the beginning of the string if there is no second component.

const char *VDFileSplitRoot(const char *);
const wchar_t *VDFileSplitRoot(const wchar_t *);

static inline char *VDFileSplitRoot(char *s) {
	return const_cast<char *>(VDFileSplitRoot(const_cast<const char *>(s)));
}

static inline wchar_t *VDFileSplitRoot(wchar_t *s) {
	return const_cast<wchar_t *>(VDFileSplitRoot(const_cast<const wchar_t *>(s)));
}

VDStringA VDFileSplitRoot(const VDStringA&);
VDStringW VDFileSplitRoot(const VDStringW&);

// VDSplitExtension returns a pointer to the extension, including the period.
// The ending null terminator is returned if there is no extension.

const char *VDFileSplitExt(const char *);
const wchar_t *VDFileSplitExt(const wchar_t *);

static inline char *VDFileSplitExt(char *s) {
	return const_cast<char *>(VDFileSplitExt(const_cast<const char *>(s)));
}

static inline wchar_t *VDFileSplitExt(wchar_t *s) {
	return const_cast<wchar_t *>(VDFileSplitExt(const_cast<const wchar_t *>(s)));
}

VDStringA VDFileSplitExtLeft(const VDStringA&);
VDStringW VDFileSplitExtLeft(const VDStringW&);
VDStringA VDFileSplitExtRight(const VDStringA&);
VDStringW VDFileSplitExtRight(const VDStringW&);

/////////////////////////////////////////////////////////////////////////////

/// Perform a case-insensitive wildcard match against a filename; returns
/// true if the pattern matches, false otherwise. '?' matches any single
/// character, and '*' matches zero or more characters.
///
/// NOTE: This is not guaranteed or intended to perfectly match the
/// underlying OS wildcard mechanism. In particular, we don't try to
/// emulate MSDOS or Windows goofiness.
bool VDFileWildMatch(const char *pattern, const char *path);
bool VDFileWildMatch(const wchar_t *pattern, const wchar_t *path);

/////////////////////////////////////////////////////////////////////////////

inline bool VDIsPathSeparator(const char c) {
	return c == ':' || c == '/' || c == '\\';
}

inline bool VDIsPathSeparator(const wchar_t c) {
	return c == L':' || c == L'/' || c == L'\\';
}

class VDParsedPath {
public:
	VDParsedPath();
	VDParsedPath(const wchar_t *path);

	bool IsRelative() const { return mbIsRelative; }
	VDStringW ToString() const;

	const wchar_t *GetRoot() const { return mRoot.c_str(); }
	size_t GetComponentCount() const { return mComponents.size(); }
	const wchar_t *GetComponent(size_t i) const { return mComponents[i].c_str(); }
	const wchar_t *GetStream() const { return mStream.c_str(); }

	void SetRoot() { mbIsRelative = true; }
	void SetRoot(const wchar_t *s) { mRoot = s; mbIsRelative = mRoot.empty() || mRoot.back() == L':'; }
	void SetStream(const wchar_t *s) { mStream = s; }

	void RemoveLastComponent() { if (!mComponents.empty()) mComponents.pop_back(); }
	void AddComponent(const wchar_t *s) { mComponents.push_back_as(s); }

protected:
	bool		mbIsRelative;
	VDStringW	mRoot;
	VDStringW	mStream;

	typedef vdvector<VDStringW> Components;
	Components	mComponents;
};

/// Given a valid path, return the same path in canonical form. This
/// collapses redundant backslashes, removes any on the end, and evaluates
/// any ..\ and .\ sections. It is useful for comparing paths.
VDStringW VDFileGetCanonicalPath(const wchar_t *path);

/// Given a base path, attempt to convert a path to a relative path. The
/// empty string is returned if the conversion fails. Both paths must be
/// absolute paths; conversion fails if either is relative.
///
/// Note that this conversion will work even if the path to convert is above
/// the base path; ..\ sections will be added as needed as long as the
/// allowAscent flag is set.
VDStringW VDFileGetRelativePath(const wchar_t *basePath, const wchar_t *pathToConvert, bool allowAscent);

/// Returns true if the given path is a relative path.
bool VDFileIsRelativePath(const wchar_t *path);

/// Resolves a possibly relatively path with a given base path. If the path
/// is absolute, the base path is ignored.
VDStringW VDFileResolvePath(const wchar_t *basePath, const wchar_t *pathToResolve);

/////////////////////////////////////////////////////////////////////////////

sint64 VDGetDiskFreeSpace(const wchar_t *path);
void VDCreateDirectory(const wchar_t *path);
void VDRemoveDirectory(const wchar_t *path);

bool VDRemoveFile(const wchar_t *path);

void VDMoveFile(const wchar_t *srcPath, const wchar_t *dstPath);

bool VDDoesPathExist(const wchar_t *fileName);
VDStringW VDIncrementPath(const VDStringW& fileName);
VDStringW VDAutoIncrementPath(const VDStringW& fileName);
bool VDIsValidFileName(const wchar_t *fileName);

uint64 VDFileGetLastWriteTime(const wchar_t *path);
VDStringW VDFileGetRootPath(const wchar_t *partialPath);
VDStringW VDGetFullPath(const wchar_t *partialPath);

VDStringW VDGetLongPath(const wchar_t *path);

VDStringW VDMakePath(const wchar_t *base, const wchar_t *file);
bool VDFileIsPathEqual(const wchar_t *path1, const wchar_t *path2);
void VDFileFixDirPath(VDStringW& path);
VDStringW VDGetLocalModulePath();
VDStringW VDGetProgramPath();
VDStringW VDGetProgramFilePath();
VDStringW VDGetSystemPath();

void VDGetRootPaths(vdvector<VDStringW>& paths);
VDStringW VDGetRootVolumeLabel(const wchar_t *rootPath);

/////////////////////////////////////////////////////////////////////////////

enum VDFileAttributes {
	kVDFileAttr_ReadOnly	= 0x01,
	kVDFileAttr_System		= 0x02,
	kVDFileAttr_Hidden		= 0x04,
	kVDFileAttr_Archive		= 0x08,
	kVDFileAttr_Directory	= 0x10,
	kVDFileAttr_Invalid		= 0xFFFFFFFFU
};

uint32 VDFileGetAttributes(const wchar_t *path);
void VDFileSetAttributes(const wchar_t *path, uint32 attrsToChange, uint32 newAttrs);

/////////////////////////////////////////////////////////////////////////////

class VDDirectoryIterator {
	VDDirectoryIterator(const VDDirectoryIterator&);
	VDDirectoryIterator& operator=(VDDirectoryIterator&);
public:
	VDDirectoryIterator(const wchar_t *path);
	~VDDirectoryIterator();

	bool Next();

	// checks for . and .. directories
	bool IsDotDirectory() const;

	bool IsDirectory() const {
		return mbDirectory;
	}

	const wchar_t *GetName() const {
		return mFilename.c_str();
	}

	const VDStringW GetFullPath() const {
		return mBasePath + mFilename;
	}

	const sint64 GetSize() const {
		return mFileSize;
	}

	VDDate GetLastWriteDate() const {
		return mLastWriteDate;
	}

	uint32 GetAttributes() const {
		return mAttributes;
	}

protected:
	void* mpHandle        = nullptr;
	bool mbSearchComplete = false;

	VDStringW	mSearchPath;
	VDStringW	mBasePath;

	VDStringW	mFilename;
	sint64		mFileSize   = 0;
	bool		mbDirectory = false;
	uint32		mAttributes = 0;

	VDDate		mLastWriteDate = {};
};

#endif

// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2019 Anton Shekhovtsov
// Copyright (C) 2023-2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <vd2/system/VDString.h>
#include <vd2/system/filesys.h>
#include <vd2/system/Error.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/strutil.h>

template<class T, class U>
static inline T splitimpL(const T& string, const U *s) {
	const U *p = string.c_str();
	return T(p, s - p);
}

template<class T, class U>
static inline T splitimpR(const T& string, const U *s) {
	return T(s);
}

const char *VDFileSplitFirstDir(const char *s) {
	const char *start = s;
	while(*s++)
		if (s[-1] == ':' || s[-1] == '\\' || s[-1] == '/')
			return s;
	return start;
}

const wchar_t *VDFileSplitFirstDir(const wchar_t *s) {
	const wchar_t *start = s;
	while(*s++)
		if (s[-1] == L':' || s[-1] == L'\\' || s[-1] == L'/')
			return s;
	return start;
}

const char *VDFileSplitPath(const char *s) {
	const char *lastsep = s;
	while(*s++)
		if (s[-1] == ':' || s[-1] == '\\' || s[-1] == '/')
			lastsep = s;
	return lastsep;
}

const wchar_t *VDFileSplitPath(const wchar_t *s) {
	const wchar_t *lastsep = s;
	while(*s++)
		if (s[-1] == L':' || s[-1] == L'\\' || s[-1] == L'/')
			lastsep = s;
	return lastsep;
}

VDStringA VDFileSplitPathLeft (const VDStringA& s) { return splitimpL(s, VDFileSplitPath(s.c_str())); }
VDStringW VDFileSplitPathLeft (const VDStringW& s) { return splitimpL(s, VDFileSplitPath(s.c_str())); }
VDStringA VDFileSplitPathRight(const VDStringA& s) { return splitimpR(s, VDFileSplitPath(s.c_str())); }
VDStringW VDFileSplitPathRight(const VDStringW& s) { return splitimpR(s, VDFileSplitPath(s.c_str())); }

const char *VDFileSplitRoot(const char *s) {
	if (s[0] == '/') return s + 1;
	return s;
}

const wchar_t *VDFileSplitRoot(const wchar_t *s) {
	if (s[0] == L'/') return s + 1;
	return s;
}

VDStringA VDFileSplitRoot(const VDStringA& s) { return splitimpL(s, VDFileSplitRoot(s.c_str())); }
VDStringW VDFileSplitRoot(const VDStringW& s) { return splitimpL(s, VDFileSplitRoot(s.c_str())); }

const char *VDFileSplitExt(const char *s) {
	const char *t = s;
	while(*t) ++t;
	const char *const end = t;
	while(t>s) {
		--t;
		if (*t == '.') return t;
		if (*t == ':' || *t == '\\' || *t == '/') break;
	}
	return end;
}

const wchar_t *VDFileSplitExt(const wchar_t *s) {
	const wchar_t *t = s;
	while(*t) ++t;
	const wchar_t *const end = t;
	while(t>s) {
		--t;
		if (*t == L'.') return t;
		if (*t == L':' || *t == L'\\' || *t == L'/') break;
	}
	return end;
}

VDStringA VDFileSplitExtLeft (const VDStringA& s) { return splitimpL(s, VDFileSplitExt(s.c_str())); }
VDStringW VDFileSplitExtLeft (const VDStringW& s) { return splitimpL(s, VDFileSplitExt(s.c_str())); }
VDStringA VDFileSplitExtRight(const VDStringA& s) { return splitimpR(s, VDFileSplitExt(s.c_str())); }
VDStringW VDFileSplitExtRight(const VDStringW& s) { return splitimpR(s, VDFileSplitExt(s.c_str())); }

bool VDFileWildMatch(const char *pattern, const char *path) {
	bool star = false;
	int i = 0;
	for(;;) {
		char c = (char)tolower((unsigned char)pattern[i]);
		if (c == '*') {
			star = true;
			pattern += i+1;
			if (!*pattern) return true;
			path += i;
			i = 0;
			continue;
		}
		char d = (char)tolower((unsigned char)path[i]);
		++i;
		if (c == '?') {
			if (!d) return false;
		} else if (c != d) {
			if (!star || !d || !i) return false;
			++path;
			i = 0;
			continue;
		}
		if (!c) return true;
	}
}

bool VDFileWildMatch(const wchar_t *pattern, const wchar_t *path) {
	bool star = false;
	int i = 0;
	for(;;) {
		wchar_t c = towlower(pattern[i]);
		if (c == L'*') {
			star = true;
			pattern += i+1;
			if (!*pattern) return true;
			path += i;
			i = 0;
			continue;
		}
		wchar_t d = towlower(path[i]);
		++i;
		if (c == L'?') {
			if (!d) return false;
		} else if (c != d) {
			if (!star || !d || !i) return false;
			++path;
			i = 0;
			continue;
		}
		if (!c) return true;
	}
}

VDParsedPath::VDParsedPath() : mbIsRelative(true) {}

VDParsedPath::VDParsedPath(const wchar_t *path) : mbIsRelative(true) {
	if (path && path[0] == L'/') {
		mRoot = L"/";
		mbIsRelative = false;
	}
}

VDStringW VDParsedPath::ToString() const {
	VDStringW s(mRoot);
	bool first = true;
	for(Components::const_iterator it(mComponents.begin()), itEnd(mComponents.end()); it != itEnd; ++it) {
		if (!first) s += L'/';
		else first = false;
		s.append(*it);
	}
	if (s.empty()) s = L".";
	return s;
}

VDStringW VDFileGetCanonicalPath(const wchar_t *path) {
	if (!path) return VDStringW();
	QString qp = QString::fromWCharArray(path);
	return VDStringW((const wchar_t*)QDir::cleanPath(qp).utf16());
}

VDStringW VDFileGetRelativePath(const wchar_t *basePath, const wchar_t *pathToConvert, bool allowAscent) {
	(void)allowAscent;
	if (!basePath || !pathToConvert) return VDStringW();
	QDir baseDir(QString::fromWCharArray(basePath));
	QString rel = baseDir.relativeFilePath(QString::fromWCharArray(pathToConvert));
	return VDStringW((const wchar_t*)rel.utf16());
}

bool VDFileIsRelativePath(const wchar_t *path) {
	if (!path || !*path) return true;
	return path[0] != L'/';
}

VDStringW VDFileResolvePath(const wchar_t *basePath, const wchar_t *pathToResolve) {
	if (VDFileIsRelativePath(pathToResolve))
		return VDMakePath(basePath, pathToResolve);
	return VDStringW(pathToResolve);
}

sint64 VDGetDiskFreeSpace(const wchar_t *path) {
	VDStringA pathA = VDTextWToA(path);
	struct statvfs stat;
	if (statvfs(pathA.c_str(), &stat) == 0) {
		return (sint64)stat.f_bavail * stat.f_frsize;
	}
	return -1;
}

bool VDDoesPathExist(const wchar_t *fileName) {
	if (!fileName) return false;
	VDStringA pathA = VDTextWToA(fileName);
	return ::access(pathA.c_str(), F_OK) == 0;
}

bool VDIsValidFileName(const wchar_t *fileName) {
	if (!fileName) return false;
	return true;
}

VDStringW VDIncrementPath(const VDStringW& fileName) {
	VDStringW name(fileName);
	const wchar_t *s = name.c_str();
	int pos = VDFileSplitExt(s) - s;
	while(--pos >= 0) {
		if (iswdigit(name[pos])) {
			if (name[pos] == L'9')
				name[pos] = L'0';
			else {
				++name[pos];
				return name;
			}
		} else break;
	}
	name.insert(name.begin() + (pos + 1), L'1');
	return name;
}

VDStringW VDAutoIncrementPath(const VDStringW& fileName) {
	VDStringW name(fileName);
	for(;;) {
		if (!VDDoesPathExist(name.c_str()))
			return name;
		name = VDIncrementPath(name);
	}
}

void VDCreateDirectory(const wchar_t *path) {
	VDStringA pathA = VDTextWToA(path);
	QDir().mkdir(QString::fromUtf8(pathA.c_str()));
}

void VDRemoveDirectory(const wchar_t *path) {
	VDStringA pathA = VDTextWToA(path);
	QDir().rmdir(QString::fromUtf8(pathA.c_str()));
}

bool VDRemoveFile(const wchar_t *path) {
	VDStringA pathA = VDTextWToA(path);
	return ::unlink(pathA.c_str()) == 0;
}

void VDMoveFile(const wchar_t *srcPath, const wchar_t *dstPath) {
	VDStringA srcA = VDTextWToA(srcPath);
	VDStringA dstA = VDTextWToA(dstPath);
	if (::rename(srcA.c_str(), dstA.c_str()) != 0) {
		throw MyError("Cannot move file");
	}
}

uint64 VDFileGetLastWriteTime(const wchar_t *path) {
	VDStringA pathA = VDTextWToA(path);
	struct stat st;
	if (::stat(pathA.c_str(), &st) == 0) {
		return (uint64)st.st_mtime;
	}
	return 0;
}

VDStringW VDFileGetRootPath(const wchar_t *partialPath) {
	(void)partialPath;
	return VDStringW(L"/");
}

VDStringW VDGetFullPath(const wchar_t *partialPath) {
	if (!partialPath) return VDStringW();
	QFileInfo fi(QString::fromWCharArray(partialPath));
	QString abs = fi.absoluteFilePath();
	return VDStringW((const wchar_t*)abs.utf16());
}

VDStringW VDGetLongPath(const wchar_t *s) {
	return VDGetFullPath(s);
}

VDStringW VDMakePath(const wchar_t *base, const wchar_t *file) {
	if (!base || !*base) return VDStringW(file);
	VDStringW result(base);
	wchar_t c = result.back();
	if (c != L'/' && c != L'\\')
		result += L'/';
	result.append(file);
	return result;
}

bool VDFileIsPathEqual(const wchar_t *path1, const wchar_t *path2) {
	if (!path1 || !path2) return false;
	return wcscmp(path1, path2) == 0;
}

void VDFileFixDirPath(VDStringW& path) {
	if (!path.empty() && path.back() != L'/')
		path += L'/';
}

VDStringW VDGetLocalModulePath() {
	QString p = QCoreApplication::applicationDirPath();
	return VDStringW((const wchar_t*)p.utf16());
}

VDStringW VDGetProgramPath() {
	return VDGetLocalModulePath();
}

VDStringW VDGetProgramFilePath() {
	QString p = QCoreApplication::applicationFilePath();
	return VDStringW((const wchar_t*)p.utf16());
}

VDStringW VDGetSystemPath() {
	return VDStringW(L"/usr/lib");
}

void VDGetRootPaths(vdvector<VDStringW>& paths) {
	paths.clear();
	paths.push_back(VDStringW(L"/"));
}

VDStringW VDGetRootVolumeLabel(const wchar_t *rootPath) {
	(void)rootPath;
	return VDStringW(L"Root");
}

uint32 VDFileGetAttributes(const wchar_t *path) {
	VDStringA pathA = VDTextWToA(path);
	struct stat st;
	if (::stat(pathA.c_str(), &st) == 0) {
		uint32 attrs = 0;
		if (S_ISDIR(st.st_mode)) attrs |= kVDFileAttr_Directory;
		if (!(st.st_mode & S_IWUSR)) attrs |= kVDFileAttr_ReadOnly;
		return attrs;
	}
	return kVDFileAttr_Invalid;
}

void VDFileSetAttributes(const wchar_t *path, uint32 attrsToChange, uint32 newAttrs) {
	(void)path; (void)attrsToChange; (void)newAttrs;
}

VDDirectoryIterator::VDDirectoryIterator(const wchar_t *path) {
	VDStringA pathA = VDTextWToA(path);
	mpHandle = opendir(pathA.c_str());
	mBasePath = path;
	VDFileFixDirPath(mBasePath);
}

VDDirectoryIterator::~VDDirectoryIterator() {
	if (mpHandle) {
		closedir((DIR*)mpHandle);
		mpHandle = nullptr;
	}
}

bool VDDirectoryIterator::Next() {
	if (!mpHandle) return false;
	struct dirent *entry = readdir((DIR*)mpHandle);
	if (!entry) {
		mbSearchComplete = true;
		return false;
	}
	VDStringA nameA(entry->d_name);
	VDStringW nameW = VDTextAToW(nameA.c_str());
	mFilename = nameW;
	mbDirectory = (entry->d_type == DT_DIR);
	return true;
}

bool VDDirectoryIterator::IsDotDirectory() const {
	return mFilename == L"." || mFilename == L"..";
}

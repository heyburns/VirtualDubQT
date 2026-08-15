// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2005 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include "stdafx.h"
#include <vd2/system/cmdline.h>
#include <vd2/system/VDString.h>

VDCommandLine::VDCommandLine(const wchar_t *s) {
	Init(s);
}

void VDCommandLine::Init(const wchar_t *s) {
	mTokens.clear();
	mLine.clear();

	for(;;) {
		while(iswspace(*s))
			++s;

		if (!*s)
			break;

		Token te = { (int)mLine.size(), *s == L'/' };

		if (te.mbIsSwitch) {
			mLine.push_back(L'/');
			++s;
		}

		mTokens.push_back(te);

		// special case for /?
		if (te.mbIsSwitch && *s == L'?') {
			mLine.push_back(L'?');
			++s;
		}

		while(*s && *s != L' ' && *s != L'/') {
			if (te.mbIsSwitch) {
				if (!isalnum((unsigned char)*s)) {
					if (*s == L':')
						++s;
					break;
				}

				mLine.push_back(*s++);
			} else if (*s == L'"') {
				++s;
				while(*s && *s != L'"')
					mLine.push_back(*s++);

				if (*s) {
					++s;

					if (*s == ',') {
						++s;
						break;
					}
				}
			} else
				mLine.push_back(*s++);
		}

		mLine.push_back(0);
	}
}

void VDCommandLine::InitAlt(const wchar_t *s) {
	mTokens.clear();
	mLine.clear();

	for(;;) {
		while(*s == L' ' || *s == L'\t')
			++s;

		if (!*s)
			break;

		Token te = { (int)mLine.size(), *s == L'/' };

		if (te.mbIsSwitch) {
			mLine.push_back(L'/');
			++s;
		}

		mTokens.push_back(te);

		// special case for /?
		if (te.mbIsSwitch && *s == L'?') {
			mLine.push_back(L'?');
			++s;
		}

		bool inquote = false;
		for(;;) {
			wchar_t c = *s;

			if (!c)
				break;

			if (!inquote && (c == L' ' || c == L'\t' || c == L'/'))
				break;

			if (te.mbIsSwitch) {
				if (!isalnum((unsigned char)*s)) {
					if (*s == L':')
						++s;
					break;
				}

				mLine.push_back(c);
				++s;
			} else {
				if (c == L'\\') {
					uint32 backslashes = 0;

					do {
						++backslashes;
						c = *++s;
					} while(c == L'\\');

					bool postquote = false;
					if (c == L'"') {
						if (backslashes & 1)
							postquote = true;
						else
							inquote = !inquote;

						backslashes >>= 1;
						++s;
					}

					while(backslashes--)
						mLine.push_back(L'\\');

					if (postquote)
						mLine.push_back(L'"');
				} else {
					if (c == L'"')
						inquote = !inquote;
					else
						mLine.push_back(c);

					++s;
				}
			}
		}

		mLine.push_back(0);
	}
}

uint32 VDCommandLine::GetCount() const {
	return mTokens.size();
}

const wchar_t *VDCommandLine::operator[](int index) const {
	return (uint32)index < mTokens.size() ? mLine.data() + mTokens[index].mTokenIndex : NULL;
}

const VDStringSpanW VDCommandLine::operator()(int index) const {
	if ((uint32)index >= mTokens.size())
		return VDStringSpanW();

	const wchar_t *s = mLine.data() + mTokens[index].mTokenIndex;

	return VDStringSpanW(s);
}

bool VDCommandLine::GetNextArgument(VDCommandLineIterator& it, const wchar_t *& token, bool& isSwitch) const {
	int count = (int)mTokens.size();

	if (it.mIndex >= count)
		return false;

	token = mLine.data() + mTokens[it.mIndex].mTokenIndex;
	isSwitch = mTokens[it.mIndex].mbIsSwitch;

	++it.mIndex;
	return true;
}

bool VDCommandLine::GetNextNonSwitchArgument(VDCommandLineIterator& it, const wchar_t *& token) const {
	int count = (int)mTokens.size();

	if (it.mIndex >= count)
		return false;

	if (mTokens[it.mIndex].mbIsSwitch)
		return false;

	token = mLine.data() + mTokens[it.mIndex++].mTokenIndex;
	return true;
}

bool VDCommandLine::GetNextSwitchArgument(VDCommandLineIterator& it, const wchar_t *& token) const {
	int count = (int)mTokens.size();

	if (it.mIndex >= count)
		return false;

	if (!mTokens[it.mIndex].mbIsSwitch)
		return false;

	token = mLine.data() + mTokens[it.mIndex++].mTokenIndex;
	return true;
}

bool VDCommandLine::FindAndRemoveSwitch(const wchar_t *name) {
	int count = (int)mTokens.size();

	for(int i=1; i<count; ++i) {
		if (mTokens[i].mbIsSwitch && !_wcsicmp(name, mLine.data() + mTokens[i].mTokenIndex + 1)) {
			mTokens.erase(mTokens.begin() + i);
			return true;
		}
	}

	return false;
}

bool VDCommandLine::FindAndRemoveSwitch(const wchar_t *name, const wchar_t *& token) {
	int count = (int)mTokens.size();
	size_t namelen = wcslen(name);

	for(int i=1; i<count; ++i) {
		if (!mTokens[i].mbIsSwitch)
			continue;
		
		const wchar_t *s = mLine.data() + mTokens[i].mTokenIndex + 1;

		if (!_wcsnicmp(name, s, namelen)) {
			token = s+namelen;	// null term

			mTokens.erase(mTokens.begin() + i);

			if (i < count-1 && !mTokens[i].mbIsSwitch) {
				token = mLine.data() + mTokens[i].mTokenIndex;
				mTokens.erase(mTokens.begin() + i);
			}
			return true;
		}
	}

	return false;
}

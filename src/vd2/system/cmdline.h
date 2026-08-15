// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2005 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_CMDLINE_H
#define f_VD2_SYSTEM_CMDLINE_H

#include <vd2/system/vdstl.h>

class VDStringSpanW;

class VDCommandLineIterator {
	friend class VDCommandLine;
public:
	VDCommandLineIterator() : mIndex(1) {}

private:
	int mIndex;
};

class VDCommandLine {
public:
	VDCommandLine() = default;
	VDCommandLine(const wchar_t *s);
	~VDCommandLine() = default;

	void Init(const wchar_t *s);

	// This splits the cmdline using rules that are closer to Visual C++'s:
	// - 2N+1 backslashes followed by a quote inserts a literal quote.
	// - 2N backslashes followed by a quote toggles the quote state.
	// - Outside of a quote, spaces, tabs, and forward slashes delimit parameters.
	//
	// We still have special switch processing:
	// - A parameter starting with a forward slash followed by a ? or an alphanumeric
	//   char is a switch. A switch is terminated by a non-alphanumeric character or
	//   a colon.
	void InitAlt(const wchar_t *s);

	uint32 GetCount() const;
	const wchar_t *operator[](int index) const;
	const VDStringSpanW operator()(int index) const;

	bool GetNextArgument(VDCommandLineIterator& index, const wchar_t *& token, bool& isSwitch) const;
	bool GetNextNonSwitchArgument(VDCommandLineIterator& index, const wchar_t *& token) const;
	bool GetNextSwitchArgument(VDCommandLineIterator& index, const wchar_t *& token) const;
	bool FindAndRemoveSwitch(const wchar_t *name);
	bool FindAndRemoveSwitch(const wchar_t *name, const wchar_t *& token);

protected:
	void RemoveArgument(int index);

	vdfastvector<wchar_t>	mLine;

	struct Token {
		int mTokenIndex;
		bool mbIsSwitch;
	};

	vdfastvector<Token>	mTokens;
};

#endif

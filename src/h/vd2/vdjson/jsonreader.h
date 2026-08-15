// VirtualDub - Video processing and capture application
// JSON I/O library
//
// Copyright (C) 1998-2010 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDJSON_JSONREADER_H
#define f_VD2_VDJSON_JSONREADER_H

#include <vd2/system/vdstl.h>

class VDJSONNameTable;
class VDJSONDocument;
struct VDJSONValue;
class VDStringW;

class VDJSONReader {
public:
	VDJSONReader();
	~VDJSONReader();

	bool Parse(const void *buf, size_t len, VDJSONDocument& doc);
	void GetErrorLocation(int& line, int& offset) const;

protected:
	bool ParseDocument();
	bool ParseObject(VDJSONValue& obj);
	bool ParseArray(VDJSONValue& arr);
	bool ParseValue(VDJSONValue& val);
	bool ParseString();
	bool Expect(wchar_t c);

	void ClearNameBuffer();
	bool AddNameChar(wchar_t c);
	bool EndName();
	bool IsNameEqual(const wchar_t *s) const;

	static bool IsWhitespaceChar(wchar_t c);

	wchar_t GetNonWhitespaceChar();
	void UngetChar();
	wchar_t GetChar();
	wchar_t GetCharSlow();

	uint32 GetTokenForName();

	void SetError();

	VDJSONNameTable *mpNameTable;
	VDJSONDocument *mpDocument;

	wchar_t *mNameBuffer;
	int mNameBufferIndex;
	int mNameBufferLength;

	enum { kInputBufferSize = 256 };
	const wchar_t *mpInputBase;
	const wchar_t *mpInputNext;
	const wchar_t *mpInputEnd;
	uint32 mInputLine;
	uint32 mInputChar;
	uint32 mInputLineNext;
	uint32 mInputCharNext;

	wchar_t mInputBuffer[kInputBufferSize];

	bool mbPendingCR;
	bool mbUTF16Mode;
	bool mbUTF32Mode;
	bool mbBigEndian;
	const uint8 *mpSrc;
	const uint8 *mpSrcEnd;
	bool mbSrcError;
	uint32 mErrorLine;
	uint32 mErrorChar;

	vdfastvector<VDJSONValue> mArrayStack;
};

#endif

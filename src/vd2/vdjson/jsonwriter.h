// VirtualDub - Video processing and capture application
// JSON I/O library
//
// Copyright (C) 1998-2010 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_VDJSON_JSONWRITER_H
#define f_VD2_VDJSON_JSONWRITER_H

#include <vd2/system/vdstl.h>

struct VDJSONValue;
class VDJSONNameTable;

class IVDJSONWriterOutput {
public:
	virtual void WriteChars(const wchar_t *src, uint32 len) = 0;
};

class VDJSONWriter {
public:
	VDJSONWriter();
	~VDJSONWriter();

	void Begin(IVDJSONWriterOutput *output);
	void End();

	void OpenArray();
	void OpenObject();
	void Close();
	void WriteMemberName(const wchar_t *name);
	void WriteMemberName(const wchar_t *name, size_t len);
	void WriteNull();
	void WriteBool(bool value);
	void WriteInt(sint64 value);
	void WriteReal(double value);
	void WriteString(const wchar_t *s);
	void WriteString(const wchar_t *s, size_t len);

protected:
	void BeginValue();
	void WriteRawString(const wchar_t *s, size_t len);
	void Write(const wchar_t *s, size_t len);
	void WriteLine();
	void WriteIndent();

	bool mbFirstItem;
	bool mbArrayMode;

	IVDJSONWriterOutput *mpOutput;
	vdfastvector<uint8> mStack;
};

void VDJSONWriteValue(VDJSONWriter& writer, const VDJSONValue& value, const VDJSONNameTable& nameTable);

#endif

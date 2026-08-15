// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2010 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_REGISTRYMEMORY_H
#define f_VD2_SYSTEM_REGISTRYMEMORY_H

#include <vd2/system/registry.h>
#include <vd2/system/thread.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>

class VDRegistryProviderMemory : public IVDRegistryProvider {
public:
	VDRegistryProviderMemory();
	~VDRegistryProviderMemory();

	void *GetMachineKey();
	void *GetUserKey();
	void *CreateKey(void *key, const char *path, bool write);
	void CloseKey(void *key);

	bool SetBool(void *key, const char *name, bool);
	bool SetInt(void *key, const char *name, int);
	bool SetString(void *key, const char *name, const char *str);
	bool SetString(void *key, const char *name, const wchar_t *str);
	bool SetBinary(void *key, const char *name, const char *data, int len);

	Type GetType(void *key, const char *name);
	bool GetBool(void *key, const char *name, bool& val);
	bool GetInt(void *key, const char *name, int& val);
	bool GetString(void *key, const char *name, VDStringA& s);
	bool GetString(void *key, const char *name, VDStringW& s);

	int GetBinaryLength(void *key, const char *name);
	bool GetBinary(void *key, const char *name, char *buf, int maxlen);

	bool RemoveValue(void *key, const char *name);
	bool RemoveKey(void *key, const char *name);

	void *EnumKeysBegin(void *key);
	const char *EnumKeysNext(void *enumerator);
	void EnumKeysClose(void *enumerator);

	void *EnumValuesBegin(void *key);
	const char *EnumValuesNext(void *enumerator);
	void EnumValuesClose(void *enumerator);

protected:
	class Key;
	class Value;
	struct Enumerator;

	Key *mpUserKey;
	Key *mpMachineKey;

	VDCriticalSection mMutex;
};

#endif

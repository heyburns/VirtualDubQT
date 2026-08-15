// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2011 Avery Lee
// Copyright (C) 2019 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <vd2/system/file.h>
#include <vd2/system/registry.h>

void VDLoadRegistry(const wchar_t *path) {
	VDTextInputFile ini(path);

	vdautoptr<VDRegistryKey> key;
	VDStringA token;
	VDStringW strvalue;
	vdfastvector<char> binvalue;

	while(const char *s = ini.GetNextLine()) {
		while(*s == ' ' || *s == '\t')
			++s;

		if (!*s || *s == ';')
			continue;

		if (*s == '[') {
			key = NULL;

			++s;
			const char *end = strchr(s, ']');
			if (!end)
				continue;

			if (!strncmp(s, "Shared\\", 7)) {
				token.assign(s + 7, end);
				key = new VDRegistryKey(token.c_str(), true, true);
			} else if (!strncmp(s, "User\\", 5)) {
				token.assign(s + 5, end);
				key = new VDRegistryKey(token.c_str(), false, true);
			}

			continue;
		}

		if (!key)
			continue;

		// expect lines of form:
		//
		//	"key" = <int-value>
		//	"key" = "<string-value>"
		//	"key" = [<binary-value>]

		if (*s != '"')
			continue;

		++s;
		const char *nameStart = s;
		while(*s != '"')
			++s;

		if (!*s)
			continue;

		token.assign(nameStart, s);
		++s;

		while(*s == ' ' || *s == '\t')
			++s;

		if (*s != '=')
			continue;
		++s;

		while(*s == ' ' || *s == '\t')
			++s;

		static const uint8 kUnhexTab[32]={
			0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
			0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0
		};

		if (*s == '-' || (*s >= '0' && *s <= '9')) {
			// integer
			long v = strtol(s, NULL, 0);

			key->setInt(token.c_str(), (int)v);
		} else if (*s == '"') {
			// string
			VDStringW wstr = VDTextU8ToW(s);
			const wchar_t* ws = wstr.c_str();
			++ws;

			strvalue.clear();
			for(;;) {
				wchar_t c = *ws++;

				if (!c || c == '"')
					break;

				if (c == '\\') {
					c = *ws++;

					if (!c)
						break;

					switch(c) {
						case 'n':	c = '\n'; break;
						case 't':	c = '\t'; break;
						case 'v':	c = '\v'; break;
						case 'b':	c = '\b'; break;
						case 'r':	c = '\r'; break;
						case 'f':	c = '\f'; break;
						case 'a':	c = '\a'; break;
						case '\\':	break;
						case '?':	break;
						case '\'':	break;
						case '"':	break;
						case 'x':
							{
								c = *ws++;
								if (!isxdigit((uint8)c))
									goto stop;

								uint32 v = 0;
								do {
									v = (v << 4) + kUnhexTab[c & 0x1f];

									c = *ws++;
								} while(isxdigit((uint8)c));

								--ws;

								strvalue.push_back((wchar_t)v);
							}
							continue;

						default:
							goto stop;

					}
				}

				strvalue.push_back(c);
			}

stop:
			key->setString(token.c_str(), strvalue.c_str());
		} else if (*s == '[') {
			binvalue.clear();

			++s;
			for(;;) {
				uint8 a = s[0];
				if (!isxdigit(a))
					break;

				uint8 b = s[1];
				if (!isxdigit(b))
					break;

				binvalue.push_back(kUnhexTab[b & 0x1f] + (kUnhexTab[a & 0x1f] << 4));

				if (s[2] != ' ')
					break;

				s += 3;
			}

			key->setBinary(token.c_str(), binvalue.data(), binvalue.size());
		}
	}
}

void ATUISaveRegistryPath(VDTextOutputStream& os, VDStringA& path, bool global) {
	VDRegistryKey key(path.c_str(), global, false);
	if (!key.isReady())
		return;

	size_t baseLen = path.size();

	VDRegistryValueIterator valIt(key);
	VDStringW strval;
	vdfastvector<uint8> binval;

	bool wroteGroup = false;
	while(const char *name = valIt.Next()) {
		if (!wroteGroup) {
			wroteGroup = true;

			os.PutLine();
			os.FormatLine("[%s%s]", global ? "Shared" : "User", path.c_str());
		}

		VDRegistryKey::Type type = key.getValueType(name);
		switch(type) {
			case VDRegistryKey::kTypeInt:
				os.FormatLine("\"%s\" = %d", name, key.getInt(name));
				break;

			case VDRegistryKey::kTypeString:
				if (key.getString(name, strval)) {
					VDStringA encstr = VDEncodeString(strval);
					os.FormatLine("\"%s\" = \"%s\"", name, encstr.c_str());
				}
				break;

			case VDRegistryKey::kTypeBinary:
				{
					int len = key.getBinaryLength(name);

					if (len >= 0) {
						binval.resize(len);

						if (key.getBinary(name, (char *)binval.data(), len)) {
							os.Format("\"%s\" = ", name);
							for(int i=0; i<len; ++i)
								os.Format("%c%02X", i ? ' ' : '[', (uint8)binval[i]);
							os.PutLine("]");
						}
					}
				}
				break;
		}
	}

	VDRegistryKeyIterator keyIt(key);
	while(const char *name = keyIt.Next()) {
		path += '\\';
		path.append(name);

		ATUISaveRegistryPath(os, path, global);

		path.resize(baseLen);
	}
}

void VDSaveRegistry(const wchar_t *fnpath) {
	VDFileStream fs(fnpath, nsVDFile::kWrite | nsVDFile::kDenyRead | nsVDFile::kCreateAlways);
	VDTextOutputStream os(&fs);

	os.PutLine("; VirtualDub2.5 settings file. EDIT AT YOUR OWN RISK.");

	VDStringA path;
	ATUISaveRegistryPath(os, path, false);
	ATUISaveRegistryPath(os, path, true);
}

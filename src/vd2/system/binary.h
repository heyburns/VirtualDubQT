// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_BINARY_H
#define f_VD2_SYSTEM_BINARY_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>
#include <cstring>

#define VDMAKEFOURCC(byte1, byte2, byte3, byte4) (((uint8)byte1) + (((uint8)byte2) << 8) + (((uint8)byte3) << 16) + (((uint8)byte4) << 24))

#ifdef _MSC_VER
	#include <vd2/system/win32/intrin.h>

	inline uint16 VDSwizzleU16(uint16 value) { return (uint16)_byteswap_ushort((unsigned short)value); }
	inline sint16 VDSwizzleS16(sint16 value) { return (sint16)_byteswap_ushort((unsigned short)value); }
	inline uint32 VDSwizzleU32(uint32 value) { return (uint32)_byteswap_ulong((unsigned long)value); }
	inline sint32 VDSwizzleS32(sint32 value) { return (sint32)_byteswap_ulong((unsigned long)value); }
	inline uint64 VDSwizzleU64(uint64 value) { return (uint64)_byteswap_uint64((unsigned __int64)value); }
	inline sint64 VDSwizzleS64(sint64 value) { return (sint64)_byteswap_uint64((unsigned __int64)value); }

	inline uint32 VDRotateLeftU32(uint32 value, int bits) { return (uint32)_rotl((unsigned int)value, bits); }
	inline uint32 VDRotateRightU32(uint32 value, int bits) { return (uint32)_rotr((unsigned int)value, bits); }
#else
	inline uint16 VDSwizzleU16(uint16 value) {
		return (value >> 8) + (value << 8);
	}

	inline sint16 VDSwizzleS16(sint16 value) {
		return (sint16)(((uint16)value >> 8) + ((uint16)value << 8));
	}

	inline uint32 VDSwizzleU32(uint32 value) {
		return (value >> 24) + (value << 24) + ((value&0xff00)<<8) + ((value&0xff0000)>>8);
	}

	inline sint32 VDSwizzleS32(sint32 value) {
		return (sint32)(((uint32)value >> 24) + ((uint32)value << 24) + (((uint32)value&0xff00)<<8) + (((uint32)value&0xff0000)>>8));
	}

	inline uint64 VDSwizzleU64(uint64 value) {
		return	((value & 0xFF00000000000000) >> 56) +
				((value & 0x00FF000000000000) >> 40) +
				((value & 0x0000FF0000000000) >> 24) +
				((value & 0x000000FF00000000) >>  8) +
				((value & 0x00000000FF000000) <<  8) +
				((value & 0x0000000000FF0000) << 24) +
				((value & 0x000000000000FF00) << 40) +
				((value & 0x00000000000000FF) << 56);
	}

	inline sint64 VDSwizzleS64(sint64 value) {
		return (sint64)((((uint64)value & 0xFF00000000000000) >> 56) +
						(((uint64)value & 0x00FF000000000000) >> 40) +
						(((uint64)value & 0x0000FF0000000000) >> 24) +
						(((uint64)value & 0x000000FF00000000) >>  8) +
						(((uint64)value & 0x00000000FF000000) <<  8) +
						(((uint64)value & 0x0000000000FF0000) << 24) +
						(((uint64)value & 0x000000000000FF00) << 40) +
						(((uint64)value & 0x00000000000000FF) << 56));
	}
#endif

template<class T>
inline T VDReadUnalignedValue(const void *p) {
	T value;
	std::memcpy(&value, p, sizeof value);
	return value;
}

template<class T>
inline void VDWriteUnalignedValue(void *p, T value) {
	std::memcpy(p, &value, sizeof value);
}

inline uint16 VDReadUnalignedU16(const void *p) { return VDReadUnalignedValue<uint16>(p); }
inline sint16 VDReadUnalignedS16(const void *p) { return VDReadUnalignedValue<sint16>(p); }
inline uint32 VDReadUnalignedU32(const void *p) { return VDReadUnalignedValue<uint32>(p); }
inline sint32 VDReadUnalignedS32(const void *p) { return VDReadUnalignedValue<sint32>(p); }
inline uint64 VDReadUnalignedU64(const void *p) { return VDReadUnalignedValue<uint64>(p); }
inline sint64 VDReadUnalignedS64(const void *p) { return VDReadUnalignedValue<sint64>(p); }
inline float VDReadUnalignedF(const void *p) { return VDReadUnalignedValue<float>(p); }
inline double VDReadUnalignedD(const void *p) { return VDReadUnalignedValue<double>(p); }

inline uint16 VDReadUnalignedLEU16(const void *p) { return VDReadUnalignedValue<uint16>(p); }
inline sint16 VDReadUnalignedLES16(const void *p) { return VDReadUnalignedValue<sint16>(p); }
inline uint32 VDReadUnalignedLEU32(const void *p) { return VDReadUnalignedValue<uint32>(p); }
inline sint32 VDReadUnalignedLES32(const void *p) { return VDReadUnalignedValue<sint32>(p); }
inline uint64 VDReadUnalignedLEU64(const void *p) { return VDReadUnalignedValue<uint64>(p); }
inline sint64 VDReadUnalignedLES64(const void *p) { return VDReadUnalignedValue<sint64>(p); }
inline float VDReadUnalignedLEF(const void *p) { return VDReadUnalignedValue<float>(p); }
inline double VDReadUnalignedLED(const void *p) { return VDReadUnalignedValue<double>(p); }

inline uint16 VDReadUnalignedBEU16(const void *p) { return VDSwizzleU16(VDReadUnalignedValue<uint16>(p)); }
inline sint16 VDReadUnalignedBES16(const void *p) { return VDSwizzleS16(VDReadUnalignedValue<sint16>(p)); }
inline uint32 VDReadUnalignedBEU32(const void *p) { return VDSwizzleU32(VDReadUnalignedValue<uint32>(p)); }
inline sint32 VDReadUnalignedBES32(const void *p) { return VDSwizzleS32(VDReadUnalignedValue<sint32>(p)); }
inline uint64 VDReadUnalignedBEU64(const void *p) { return VDSwizzleU64(VDReadUnalignedValue<uint64>(p)); }
inline sint64 VDReadUnalignedBES64(const void *p) { return VDSwizzleS64(VDReadUnalignedValue<sint64>(p)); }
inline float VDReadUnalignedBEF(const void *p) {
	const uint32 bits = VDSwizzleU32(VDReadUnalignedValue<uint32>(p));
	float value;
	std::memcpy(&value, &bits, sizeof value);
	return value;
}
inline double VDReadUnalignedBED(const void *p) {
	const uint64 bits = VDSwizzleU64(VDReadUnalignedValue<uint64>(p));
	double value;
	std::memcpy(&value, &bits, sizeof value);
	return value;
}

inline void VDWriteUnalignedU16  (void *p, uint16 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedS16  (void *p, sint16 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedU32  (void *p, uint32 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedS32  (void *p, sint32 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedU64  (void *p, uint64 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedS64  (void *p, sint64 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedF    (void *p, float  v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedD    (void *p, double v) { VDWriteUnalignedValue(p, v); }

inline void VDWriteUnalignedLEU16(void *p, uint16 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedLES16(void *p, sint16 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedLEU32(void *p, uint32 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedLES32(void *p, sint32 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedLEU64(void *p, uint64 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedLES64(void *p, sint64 v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedLEF  (void *p, float  v) { VDWriteUnalignedValue(p, v); }
inline void VDWriteUnalignedLED  (void *p, double v) { VDWriteUnalignedValue(p, v); }

inline void VDWriteUnalignedBEU16(void *p, uint16 v) { VDWriteUnalignedValue(p, VDSwizzleU16(v)); }
inline void VDWriteUnalignedBES16(void *p, sint16 v) { VDWriteUnalignedValue(p, VDSwizzleS16(v)); }
inline void VDWriteUnalignedBEU32(void *p, uint32 v) { VDWriteUnalignedValue(p, VDSwizzleU32(v)); }
inline void VDWriteUnalignedBES32(void *p, sint32 v) { VDWriteUnalignedValue(p, VDSwizzleS32(v)); }
inline void VDWriteUnalignedBEU64(void *p, uint64 v) { VDWriteUnalignedValue(p, VDSwizzleU64(v)); }
inline void VDWriteUnalignedBES64(void *p, sint64 v) { VDWriteUnalignedValue(p, VDSwizzleS64(v)); }
inline void VDWriteUnalignedBEF(void *p, float v) {
	uint32 bits;
	std::memcpy(&bits, &v, sizeof bits);
	VDWriteUnalignedValue(p, VDSwizzleU32(bits));
}
inline void VDWriteUnalignedBED(void *p, double v) {
	uint64 bits;
	std::memcpy(&bits, &v, sizeof bits);
	VDWriteUnalignedValue(p, VDSwizzleU64(bits));
}

#define VDFromLE8(x)	(x)
#define VDFromLE16(x)	(x)
#define VDFromLE32(x)	(x)
#define VDFromBE8(x)	VDSwizzleU8(x)
#define VDFromBE16(x)	VDSwizzleU16(x)
#define VDFromBE32(x)	VDSwizzleU32(x)

#define VDToLE8(x)		(x)
#define VDToLE16(x)		(x)
#define VDToLE32(x)		(x)
#define VDToBE8(x)		VDSwizzleU8(x)
#define VDToBE16(x)		VDSwizzleU16(x)
#define VDToBE32(x)		VDSwizzleU32(x)

#endif

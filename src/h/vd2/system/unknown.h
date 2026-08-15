// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_UNKNOWN_H
#define f_VD2_SYSTEM_UNKNOWN_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>

///////////////////////////////////////////////////////////////////////////
//	IVDUnknown
///	Base interface for runtime type discovery.
class IVDUnknown {
public:
	/// Attempt to cast to another type. Returns NULL if interface is unsupported.
	virtual void *AsInterface(uint32 id) = 0;

	inline const void *AsInterface(uint32 id) const {
		return const_cast<IVDUnknown *>(this)->AsInterface(id);
	}
};

///////////////////////////////////////////////////////////////////////////
//	IVDUnknown
///	Base interface for runtime type discovery with reference counting.
class IVDRefUnknown : public IVDUnknown {
public:
	virtual int AddRef() = 0;	///< Add strong reference to object. Returns new reference count (debug builds only).
	virtual int Release() = 0;	///< Remove strong refence from object, and destroy it if the refcount drops to zero. Returns zero if object was destroyed.
};

template<class T>
inline uint32 vdpoly_id_from_ptr(T *p) {
	return T::kTypeID;
}

///////////////////////////////////////////////////////////////////////////
//	vdpoly_cast
///	Performs a runtime polymorphic cast on an IUnknown-based object.
///
///	\param	pUnk	Pointer to cast. May be NULL.
///
///	Attempts to cast a pointer to a different type using the
///	\c AsInterface() method. The destination type must support the
/// \c kTypeID convention for returning the type ID.
/// 
template<class T>
T vdpoly_cast(IVDUnknown *pUnk) {
	return pUnk ? (T)pUnk->AsInterface(vdpoly_id_from_ptr(T(NULL))) : NULL;
}

#endif

// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_THUNK_H
#define f_VD2_SYSTEM_THUNK_H

#ifdef _MSC_VER
	#pragma once
#endif

bool VDInitThunkAllocator();
void VDShutdownThunkAllocator();

void *VDAllocateThunkMemory(size_t len);
void VDFreeThunkMemory(void *p, size_t len);
void VDSetThunkMemory(void *p, const void *src, size_t len);
void VDFlushThunkMemory(void *p, size_t len);

class VDFunctionThunk;

VDFunctionThunk *VDCreateFunctionThunkFromMethod(void *method, void *pThis, size_t argbytes, bool stdcall_thunk);
void VDDestroyFunctionThunk(VDFunctionThunk *pFnThunk);

///////////////////////////////////////////////////////////////////////////////

template<typename T> struct VDMetaSizeofArg { enum { value = (sizeof(T) + sizeof(void *) - 1) & ~(sizeof(void *) - 1) }; };

// This doesn't work for references. Sadly, these seem to get stripped during template matching.
template<class T, class R>
char (&VDMetaGetMethodArgBytes(R (T::*method)()))[1];

template<class T, class R, class A1>
char (&VDMetaGetMethodArgBytes(R (T::*method)(A1)))[1 + VDMetaSizeofArg<A1>::value];

template<class T, class R, class A1, class A2>
char (&VDMetaGetMethodArgBytes(R (T::*method)(A1, A2)))[1 + VDMetaSizeofArg<A1>::value + VDMetaSizeofArg<A2>::value];

template<class T, class R, class A1, class A2, class A3>
char (&VDMetaGetMethodArgBytes(R (T::*method)(A1, A2, A3)))[1 + VDMetaSizeofArg<A1>::value + VDMetaSizeofArg<A2>::value + VDMetaSizeofArg<A3>::value];

template<class T, class R, class A1, class A2, class A3, class A4>
char (&VDMetaGetMethodArgBytes(R (T::*method)(A1, A2, A3, A4)))[1 + VDMetaSizeofArg<A1>::value + VDMetaSizeofArg<A2>::value + VDMetaSizeofArg<A3>::value + VDMetaSizeofArg<A4>::value];

template<class T, class R, class A1, class A2, class A3, class A4, class A5>
char (&VDMetaGetMethodArgBytes(R (T::*method)(A1, A2, A3, A4)))[1 + VDMetaSizeofArg<A1>::value + VDMetaSizeofArg<A2>::value + VDMetaSizeofArg<A3>::value + VDMetaSizeofArg<A4>::value + VDMetaSizeofArg<A5>::value];

///////////////////////////////////////////////////////////////////////////////

template<class T, class T_Method>
VDFunctionThunk *VDCreateFunctionThunkFromMethod(T *pThis, T_Method method, bool stdcall_thunk) {
	return VDCreateFunctionThunkFromMethod(*(void **)&method, pThis, sizeof VDMetaGetMethodArgBytes(method) - 1, stdcall_thunk);
}

#endif

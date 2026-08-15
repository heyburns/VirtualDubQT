// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_PROTSCOPE_H
#define f_VD2_SYSTEM_PROTSCOPE_H

#ifdef _MSC_VER
	#pragma once
#endif

///////////////////////////////////////////////////////////////////////////
//
// Protected scope macros
//
// These macros allow you to define a scope which is known to the crash
// handler -- that is, if the application crashes within a protected scope
// the handler will report the scope information in the crash output.
//

class VDProtectedAutoScope;

typedef VDProtectedAutoScope *(*tpVDGetProtectedScopeLink)();
typedef void (*tpVDSetProtectedScopeLink)(VDProtectedAutoScope *);

extern tpVDGetProtectedScopeLink g_pVDGetProtectedScopeLink;
extern tpVDSetProtectedScopeLink g_pVDSetProtectedScopeLink;


class IVDProtectedScopeOutput {
public:
	virtual void write(const char *s) = 0;
	virtual void writef(const char *s, ...) = 0;
};

class VDProtectedAutoScope {
public:
	VDProtectedAutoScope(const char *file, int line, const char *action) : mpFile(file), mLine(line), mpAction(action), mpLink(g_pVDGetProtectedScopeLink()) {
		// Note that the assignment to g_protectedScopeLink cannot occur here, as the
		// derived class has not been constructed yet.  Uninitialized objects in
		// the debugging chain are *bad*.
	}

	~VDProtectedAutoScope() {
		g_pVDSetProtectedScopeLink(mpLink);
	}

	operator bool() const { return false; }

	virtual void Write(IVDProtectedScopeOutput& out) {
		out.write(mpAction);
	}

	VDProtectedAutoScope *mpLink;
	const char *const mpFile;
	const int mLine;
	const char *const mpAction;
};

class VDProtectedAutoScopeData0 {
public:
	VDProtectedAutoScopeData0(const char *file, int line, const char *action) : mpFile(file), mLine(line), mpAction(action) {}
	const char *const mpFile;
	const int mLine;
	const char *const mpAction;
};

template<class T1>
class VDProtectedAutoScopeData1 {
public:
	VDProtectedAutoScopeData1(const char *file, int line, const char *action, const T1 a1) : mpFile(file), mLine(line), mpAction(action), mArg1(a1) {}
	const char *const mpFile;
	const int mLine;
	const char *const mpAction;
	const T1 mArg1;
};

template<class T1, class T2>
class VDProtectedAutoScopeData2 {
public:
	VDProtectedAutoScopeData2(const char *file, int line, const char *action, const T1 a1, const T2 a2) : mpFile(file), mLine(line), mpAction(action), mArg1(a1), mArg2(a2) {}
	const char *const mpFile;
	const int mLine;
	const char *const mpAction;
	const T1 mArg1;
	const T2 mArg2;
};

template<class T1, class T2, class T3>
class VDProtectedAutoScopeData3 {
public:
	VDProtectedAutoScopeData3(const char *file, int line, const char *action, const T1 a1, const T2 a2, const T3 a3) : mpFile(file), mLine(line), mpAction(action), mArg1(a1), mArg2(a2), mArg3(a3) {}
	const char *const mpFile;
	const int mLine;
	const char *const mpAction;
	const T1 mArg1;
	const T2 mArg2;
	const T3 mArg3;
};

template<class T1, class T2, class T3, class T4>
class VDProtectedAutoScopeData4 {
public:
	VDProtectedAutoScopeData4(const char *file, int line, const char *action, const T1 a1, const T2 a2, const T3 a3, const T4 a4) : mpFile(file), mLine(line), mpAction(action), mArg1(a1), mArg2(a2), mArg3(a3), mArg4(a4) {}
	const char *const mpFile;
	const int mLine;
	const char *const mpAction;
	const T1 mArg1;
	const T2 mArg2;
	const T3 mArg3;
	const T4 mArg4;
};

class VDProtectedAutoScope0 : public VDProtectedAutoScope {
public:
	VDProtectedAutoScope0(const VDProtectedAutoScopeData0& data) : VDProtectedAutoScope(data.mpFile, data.mLine, data.mpAction) {
		g_pVDSetProtectedScopeLink(this);
	}
};

template<class T1>
class VDProtectedAutoScope1 : public VDProtectedAutoScope {
public:
	VDProtectedAutoScope1(const VDProtectedAutoScopeData1<T1>& data) : VDProtectedAutoScope(data.mpFile, data.mLine, data.mpAction), mArg1(data.mArg1) {
		g_pVDSetProtectedScopeLink(this);
	}

	virtual void Write(IVDProtectedScopeOutput& out) {
		out.writef(mpAction, mArg1);
	}

	const T1 mArg1;
};

template<class T1, class T2>
class VDProtectedAutoScope2 : public VDProtectedAutoScope {
public:
	VDProtectedAutoScope2(const VDProtectedAutoScopeData2<T1,T2>& data) : VDProtectedAutoScope(data.mpFile, data.mLine, data.mpAction), mArg1(data.mArg1), mArg2(data.mArg2) {
		g_pVDSetProtectedScopeLink(this);
	}

	virtual void Write(IVDProtectedScopeOutput& out) {
		out.writef(mpAction, mArg1, mArg2);
	}

	const T1 mArg1;
	const T2 mArg2;
};

template<class T1, class T2, class T3>
class VDProtectedAutoScope3 : public VDProtectedAutoScope {
public:
	VDProtectedAutoScope3(const VDProtectedAutoScopeData3<T1,T2,T3>& data) : VDProtectedAutoScope(data.mpFile, data.mLine, data.mpAction), mArg1(data.mArg1), mArg2(data.mArg2), mArg3(data.mArg3) {
		g_pVDSetProtectedScopeLink(this);
	}

	virtual void Write(IVDProtectedScopeOutput& out) {
		out.writef(mpAction, mArg1, mArg2, mArg3);
	}

	const T1 mArg1;
	const T2 mArg2;
	const T3 mArg3;
};

template<class T1, class T2, class T3, class T4>
class VDProtectedAutoScope4 : public VDProtectedAutoScope {
public:
	VDProtectedAutoScope4(const VDProtectedAutoScopeData4<T1,T2,T3,T4>& data) : VDProtectedAutoScope(data.mpFile, data.mLine, data.mpAction), mArg1(data.mArg1), mArg2(data.mArg2), mArg3(data.mArg3), mArg4(data.mArg4) {
		g_pVDSetProtectedScopeLink(this);
	}

	virtual void Write(IVDProtectedScopeOutput& out) {
		out.writef(mpAction, mArg1, mArg2, mArg3, mArg4);
	}

	const T1 mArg1;
	const T2 mArg2;
	const T3 mArg3;
	const T4 mArg4;
};


#define vdprotected(action) vdobjectscope(VDProtectedAutoScope0 autoscope = VDProtectedAutoScopeData0(__FILE__, __LINE__, action))
#define vdprotected1(actionf, type1, arg1) vdobjectscope(VDProtectedAutoScope1<type1> autoscope = VDProtectedAutoScopeData1<type1>(__FILE__, __LINE__, actionf, arg1))

// @&#(* preprocessor doesn't view template brackets as escaping commas, so we have a slight
// problem....

#define vdprotected2(actionf, type1, arg1, type2, arg2) switch(VDProtectedAutoScope2<type1, type2> autoscope = VDProtectedAutoScopeData2<type1, type2>(__FILE__, __LINE__, actionf, arg1, arg2)) case 0: default:
#define vdprotected3(actionf, type1, arg1, type2, arg2, type3, arg3) switch(VDProtectedAutoScope3<type1, type2, type3> autoscope = VDProtectedAutoScopeData3<type1, type2, type3>(__FILE__, __LINE__, actionf, arg1, arg2, arg3)) case 0: default:
#define vdprotected4(actionf, type1, arg1, type2, arg2, type3, arg3, type4, arg4) switch(VDProtectedAutoScope4<type1, type2, type3, type4> autoscope = VDProtectedAutoScopeData4<type1, type2, type3, type4>(__FILE__, __LINE__, actionf, arg1, arg2, arg3, arg4)) case 0: default:

#endif

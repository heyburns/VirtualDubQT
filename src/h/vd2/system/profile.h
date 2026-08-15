// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2016-2018 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_VD2_SYSTEM_PROFILE_H
#define f_VD2_SYSTEM_PROFILE_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <vector>

class VDRTProfiler;

void VDInitProfilingSystem();
void VDDeinitProfilingSystem();
VDRTProfiler *VDGetRTProfiler();

//
//	VDRTProfiler		Real-time profiler
//
//	This class forms the base for a very simple real-time profiler: threads
//	record events in channels, and periodically, someone swaps the active
//	recording array with a second array, and draws the sampled events off
//	that array.  In VirtualDub, this is done via RTProfileDisplay.  Events
//	are sampled via the high-performance counter in Win32, but clients need
//	never know this fact.
//
//	All methods in VDRTProfiler are thread-safe.  However, it is assumed
//	that only one client will be calling Swap() and accessing the Paint
//	channel set.  Swap() should be called from rather low-level code as
//	it may introduce deadlocks otherwise.
//
//	Strings passed to VDRTProfiler must be constant data in the main EXE.
//	No dynamic strings or DLLs.  The reason is that there is an
//	indefinite delay between a call to FreeChannel() and the last time
//	data from that channel is displayed.
//
//	Channels are not restricted to a particular thread; it is permissible
//	to allocate a channel in one thread and use it in another.  However,
//	channels must not be simultaneously used by two threads -- that will
//	generate interesting output.
//
class VDRTProfiler {
public:
	enum CounterType {
		kCounterTypeUint32,
		kCounterTypeDouble
	};

public:
	VDRTProfiler();
	~VDRTProfiler() = default;

	void BeginCollection();
	void EndCollection();
	void Swap();

	bool IsEnabled() const { return mbEnableCollection; }

	int AllocChannel(const char *name);
	void FreeChannel(int ch);
	void BeginEvent(int channel, uint32 color, const char *name);
	void EndEvent(int channel);

	void RegisterCounterD(const char *name, const double *val);
	void RegisterCounterU32(const char *name, const uint32 *val);
	void RegisterCounter(const char *name, const void *val, CounterType type);
	void UnregisterCounter(void *p);

public:
	struct Event {
		uint64		mStartTime;
		uint64		mEndTime;			// only last 32 bits of counter
		uint32		mColor;
		const char *mpName;
	};

	struct Channel {
		const char			*mpName;
		bool				mbEventPending;
		vdfastvector<Event>	mEventList;
	};

	struct Counter {
		const char			*mpName;
		const void			*mpData;
		CounterType			mType;
		union {
			uint32 u32;
			double d;
		} mData, mDataLast;
	};

	struct CounterByNamePred;

	typedef std::vector<Channel> tChannels;
	typedef vdfastvector<Counter> Counters;

	VDCriticalSection		mLock;
	tChannels				mChannelArray;
	tChannels				mChannelArrayToPaint;
	Counters				mCounterArray;
	Counters				mCounterArrayToPaint;
	uint64					mPerfFreq;
	uint64					mSnapshotTime = 0;

	volatile bool			mbEnableCollection = false;
};

//
//	VDRTProfileChannel
//
//	This helper simply makes channel acquisition easier.  It automatically
//	stubs out if no profiler is available.  However, it's still advisable
//	not to call this from your inner loop!
//
class VDRTProfileChannel {
public:
	VDRTProfileChannel(const char *name)
		: mpProfiler(VDGetRTProfiler())
		, mProfileChannel(mpProfiler ? mpProfiler->AllocChannel(name) : 0)
	{
	}
	~VDRTProfileChannel() {
		if (mpProfiler)
			mpProfiler->FreeChannel(mProfileChannel);
	}

	void Begin(uint32 color, const char *name) {
		if (mpProfiler)
			mpProfiler->BeginEvent(mProfileChannel, color, name);
	}

	void End() {
		if (mpProfiler)
			mpProfiler->EndEvent(mProfileChannel);
	}

protected:
	VDRTProfiler *const mpProfiler;
	int mProfileChannel;
};

///////////////////////////////////////////////////////////////////////////

class IVDEventProfiler {
public:
	enum {
		sample_cpu = 0,
		sample_count = 1,
	};

	virtual void InsertSample(int name, uint64 data) = 0;
	virtual void BeginScope(const char *name, uintptr *cache, uint32 data, uint32 flags) = 0;
	virtual void BeginDynamicScope(const char *name, uintptr *cache, uint32 data, uint32 flags) = 0;
	virtual void SetComment(uintptr scopeId, const char* data) = 0;
	virtual void EndScope() = 0;
	virtual void ExitThread() = 0;
};

extern IVDEventProfiler *g_pVDEventProfiler;

class VDProfileEventAutoEndScope {
public:
	~VDProfileEventAutoEndScope() {
		if (g_pVDEventProfiler)
			g_pVDEventProfiler->EndScope();
	}
};

struct VDProfileCachedEvent;

enum {
	vdprofiler_flag_wait=1,
	vdprofiler_flag_loop=2,
	vdprofiler_flag_profile=4,
	vdprofiler_flag_mode=8,
	vdprofiler_flag_event=16,
};

typedef uintptr VDProfileEventCache;

#define VDPROFILEBEGINDYNAMIC(cache, dynLabel) if (true) { if (g_pVDEventProfiler) g_pVDEventProfiler->BeginDynamicScope((dynLabel), &(cache), 0, 0); } else ((void)0)
#define VDPROFILEBEGINDYNAMICEX(cache, dynLabel, data) if (true) { if (g_pVDEventProfiler) g_pVDEventProfiler->BeginDynamicScope((dynLabel), &(cache), data, 0); } else ((void)0)
#define VDPROFILEBEGIN(label) if (true) { static uintptr sCache = NULL; if (g_pVDEventProfiler) g_pVDEventProfiler->BeginScope((label), &sCache, 0, 0); } else ((void)0)
#define VDPROFILEBEGINEX(label, data) if (true) { static uintptr sCache = NULL; if (g_pVDEventProfiler) g_pVDEventProfiler->BeginScope((label), &sCache, data, 0); } else ((void)0)
#define VDPROFILEBEGINEX2(label, data, flags) if (true) { static uintptr sCache = NULL; if (g_pVDEventProfiler) g_pVDEventProfiler->BeginScope((label), &sCache, data, flags); } else ((void)0)
#define VDPROFILEBEGINEX3(label, data, flags, comment) if (true) { static uintptr sCache = NULL; if (g_pVDEventProfiler){ g_pVDEventProfiler->BeginScope((label), &sCache, data, flags); if(comment) g_pVDEventProfiler->SetComment(sCache, comment); } } else ((void)0)
#define VDPROFILEEND() if (g_pVDEventProfiler) g_pVDEventProfiler->EndScope(); else ((void)0)
#define VDPROFILEAUTOEND() VDProfileEventAutoEndScope _autoEndScope
#define VDPROFILESCOPE(label) VDPROFILEBEGIN(label); VDPROFILEAUTOEND()

#endif


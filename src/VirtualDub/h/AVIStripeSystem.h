// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_AVISTRIPESYSTEM_H
#define f_AVISTRIPESYSTEM_H

#include <vfw.h>

#include "VirtualDub.h"
#include "AVIReadHandler.h"

#pragma warning(push)
#pragma warning(disable: 4200)	// nonstandard extension used

class AVIStripe {
public:
	enum {
		FLAG_INDEX		= 1,
		FLAG_VIDEO		= 2,
		FLAG_AUDIO		= 4,

		MODE_MASTER		= FLAG_INDEX | FLAG_AUDIO,	// contains index + *all* audio
		MODE_INDEX		= FLAG_INDEX,	// contains index only
		MODE_VIDEO		= FLAG_VIDEO,	// contains video segments
		MODE_AUDIO		= FLAG_AUDIO,	// contains audio segments
		MODE_BOTH		= FLAG_VIDEO | FLAG_AUDIO,	// contains a/v segments
	};

	long	lBufferSize, lChunkSize;
	int		iNameLen;
	char	cStripeMode;
	signed char	scPriority;
	char	szName[];

	void *operator new(size_t stSize, int iNameBytes);
	void operator delete(void *p);
	void operator delete(void *p, int iNameBytes);		// for exception unwinding

	BOOL isIndex() { return !!(cStripeMode & FLAG_INDEX); }
	BOOL isVideo() { return !!(cStripeMode & FLAG_VIDEO); }
	BOOL isAudio() { return !!(cStripeMode & FLAG_AUDIO); }
};

#pragma warning(pop)


class AVIStripeSystem {
private:
	int nStripes;
	AVIStripe **stripe;

	void _construct(int nStripes);
	void _destruct();
public:
	AVIStripeSystem(int nStripes);
	AVIStripeSystem(const wchar_t* szFile);
	~AVIStripeSystem();

	void		Save(const wchar_t* szFile); // hmm, not implemented

	int			getStripeCount();
	AVIStripe *	getStripeInfo(int nStripe);
};

////////////////////////////////////

class AVIStripeIndexEntry {
public:
	long	lSampleFirst;
	long	lSampleCount;
	long	lStripe;
	long	lStripeSample;
};

#define DEFINETEST(type) bool inline operator##type(long lSample, AVIStripeIndexEntry& asie)

DEFINETEST(< ) { return lSample <  asie.lSampleFirst; }
DEFINETEST(>=) { return lSample >= asie.lSampleFirst; }
DEFINETEST(> ) { return lSample >= asie.lSampleFirst + asie.lSampleCount; }
DEFINETEST(<=) { return lSample <  asie.lSampleFirst + asie.lSampleCount; }

DEFINETEST(==)	{
					long t = lSample - asie.lSampleFirst;

					return t>=0 && t<asie.lSampleCount;
				}

DEFINETEST(!=)	{
					long t = lSample - asie.lSampleFirst;

					return t<0 || t>=asie.lSampleCount;
				}

#undef DEFINETEST

////////////////////////////////////

class AVIStripeIndexLookup {
private:
	AVIStripeIndexEntry *index_table;
	long index_table_size;

public:
	AVIStripeIndexLookup(IAVIReadStream *pAVIIndex);
	~AVIStripeIndexLookup();

	AVIStripeIndexEntry *lookup(long sample);
};

#endif

// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2007 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_AVIREADINDEX_H
#define f_VD2_AVIREADINDEX_H

#ifdef _MSC_VER
	#pragma once
#endif

#ifndef f_VD2_SYSTEM_VDTYPES_H
	#include <vd2/system/vdtypes.h>
#endif

#ifndef f_VD2_SYSTEM_VDSTL_H
	#include <vd2/system/vdstl.h>
#endif

class VDAVIReadIndexIterator {
	friend class VDAVIReadIndex;

	uint32	mSectorIndex;
	uint32	mSectorLimit;
	uint32	mChunkIndex;
	uint32	mChunkOffset;
};

class VDAVIReadIndex {
public:
	VDAVIReadIndex();
	~VDAVIReadIndex();

	void	Init(uint32 sampleSize);

	bool	IsVBR() const;
	sint64	GetSampleCount() const;
	uint32	GetChunkCount() const;
	sint64	GetByteCount() const;

	bool	IsKey(sint64 samplePos) const;
	sint64	PrevKey(sint64 samplePos) const;
	sint64	NextKey(sint64 samplePos) const;
	sint64	NearestKey(sint64 samplePos) const;

	void	FindSampleRange(VDAVIReadIndexIterator& it, sint64 startPos, uint32 maxSamples) const;
	void	GetFirstSampleRange(VDAVIReadIndexIterator& it) const;
	bool	GetNextSampleRange(VDAVIReadIndexIterator& it, sint64& chunkPos, uint32& offset, uint32& byteSize) const;

	void	Clear();
	void	AddChunk(sint64 bytePos, uint32 sizeAndKeyFrameFlag);
	void	Append(const VDAVIReadIndex& src, sint64 bytePosOffset);
	void	Finalize();

protected:
	enum {
		kBlockSizeBits	= 10,
		kBlockSize		= 1 << kBlockSizeBits,
		kBlockMask		= kBlockSize - 1
	};

	struct SectorEntry {
		sint64	mByteOffset;
		sint64	mSampleOffset;
		uint32	mChunkOffset;
		bool	mbOneSamplePerChunk;
	};

	struct IndexEntry {
		uint32	mByteOffset;
		uint32	mSampleOffset;
		uint32	mSizeAndKeyFrameFlag;
		uint16	mPrevKeyDistance;
		uint16	mUnused0;
	};

	uint32 FindSectorIndexByChunk(uint32 chunk) const;
	uint32 FindSectorIndexBySample(sint64 sample) const;
	IndexEntry *FindChunk(sint64 sample, uint32 sectorIndex, uint32& sampleOffsetOut, uint32& index) const;

protected:
	sint64	mByteCount;
	sint64	mSampleCount;
	uint32	mSampleSize;
	uint32	mSectorCount;
	uint32	mChunkCount;
	uint32	mBlockOffset;
	uint32	mPrevKey;
	bool	mbFinalized;
	bool	mbVBR;

	typedef vdfastvector<SectorEntry> Sectors;
	Sectors mSectors;

	typedef vdfastvector<IndexEntry *> Index;
	Index mIndex;
};

#endif

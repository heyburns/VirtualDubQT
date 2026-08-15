// VirtualDub - Video processing and capture application
// Graphics support library
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2015-2018 Anton Shekhovtsov
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_KASUMI_UBERBLIT_INTERLACE_H
#define f_VD2_KASUMI_UBERBLIT_INTERLACE_H

#include "uberblit_base.h"

class VDPixmapGen_SplitFields : public IVDPixmapGen {
public:
	void AddWindowRequest(int minDY, int maxDY) {
		mpSrc->AddWindowRequest(minDY*2, maxDY*2+1);
	}

	void TransformPixmapInfo(const FilterModPixmapInfo& src, FilterModPixmapInfo& dst) {
		mpSrc->TransformPixmapInfo(src,dst);
	}

	void Start() {
		mpSrc->Start();
	}

	sint32 GetWidth(int) const { return mWidth; }
	sint32 GetHeight(int idx) const { return mHeight[idx]; }

	bool IsStateful() const {
		return false;
	}

	uint32 GetType(uint32 output) const {
		return mpSrc->GetType(mSrcIndex);
	}

	const void *GetRow(sint32 y, uint32 index) {
		return mpSrc->GetRow(y+y+index, mSrcIndex);
	}

	void ProcessRow(void *dst, sint32 y) {
		memcpy(dst, GetRow(y, 0), mBpr);
	}

	void Init(IVDPixmapGen *src, uint32 srcindex, uint32 bpr) {
		mpSrc = src;
		mSrcIndex = srcindex;
		mBpr = bpr;
		mWidth = src->GetWidth(srcindex);

		uint32 h = src->GetHeight(srcindex);
		mHeight[0] = (h + 1) >> 1;
		mHeight[1] = h >> 1;
	}

	virtual IVDPixmapGen* dump_src(int index){
		if(index==0) return mpSrc;
		return 0; 
	}

	virtual const char* dump_name(){ return "SplitFields"; }

protected:
	IVDPixmapGen *mpSrc;
	uint32 mSrcIndex;
	sint32 mWidth;
	sint32 mHeight[2];
	uint32 mBpr;
};

class VDPixmapGen_MergeFields : public IVDPixmapGen {
public:
	void AddWindowRequest(int minDY, int maxDY) {
		mpSrc[0]->AddWindowRequest(minDY >> 1, maxDY >> 1);
		mpSrc[1]->AddWindowRequest(minDY >> 1, maxDY >> 1);
	}

	void TransformPixmapInfo(const FilterModPixmapInfo& src, FilterModPixmapInfo& dst) {
		mpSrc[0]->TransformPixmapInfo(src,dst);
		mpSrc[1]->TransformPixmapInfo(src,dst);
	}

	void Start() {
		mpSrc[0]->Start();
		mpSrc[1]->Start();
	}

	sint32 GetWidth(int) const { return mWidth; }
	sint32 GetHeight(int) const { return mHeight; }

	bool IsStateful() const {
		return false;
	}

	uint32 GetType(uint32 output) const {
		return mpSrc[0]->GetType(mSrcIndex[0]);
	}

	const void *GetRow(sint32 y, uint32 index) {
		int srcIndex = y & 1;
		return mpSrc[srcIndex]->GetRow(y >> 1, mSrcIndex[srcIndex]);
	}

	void ProcessRow(void *dst, sint32 y) {
		memcpy(dst, GetRow(y, 0), mBpr);
	}

	void Init(IVDPixmapGen *src1, uint32 srcindex1, IVDPixmapGen *src2, uint32 srcindex2, uint32 w, uint32 h, uint32 bpr) {
		mpSrc[0] = src1;
		mpSrc[1] = src2;
		mSrcIndex[0] = srcindex1;
		mSrcIndex[1] = srcindex2;

		mWidth = w;
		mHeight = h;
		mBpr = bpr;
	}

	virtual IVDPixmapGen* dump_src(int index){
		if(index==0) return mpSrc[0];
		if(index==1) return mpSrc[1];
		return 0; 
	}

	virtual const char* dump_name(){ return "MergeFields"; }

protected:
	IVDPixmapGen *mpSrc[2];
	uint32 mSrcIndex[2];
	sint32 mWidth;
	sint32 mHeight;
	uint32 mBpr;
};

#endif

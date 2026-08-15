// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#ifndef f_ZIP_H
#define f_ZIP_H

// Rest in peace, Phil Katz.

#include <vd2/system/vdtypes.h>
#include <vd2/system/file.h>
#include <vd2/system/file.h>
#include <vd2/system/VDString.h>
#include <string.h>
#include <vector>

class VDDeflateBitReader {
public:
	VDDeflateBitReader() = default;

	void init(IVDStream *pSrc, uint64 limit) {
		mpSrc = pSrc;
		mBytesLeft = limit;
		refill();
		consume(0);
	}

	IVDStream *stream() const {
		return mpSrc;
	}

	unsigned long peek() const {
		return accum;
	}

	bool consume(unsigned n) {
//		printf("%08lx/%d\n", accum << ((-bits)&7), bits);
		bits -= n;

		if ((int)bits < 0)
			return false;

		accum >>= n;

		while(bits <= 24 && (mBufferPt || refill())) {
			accum += mBuffer[kBufferSize + mBufferPt++] << bits;
			bits += 8;
		}

		return true;
	}

	bool refill();

	bool getbit() {
		unsigned rv = accum;

		consume(1);

		return (rv&1) != 0;
	}

	unsigned getbits(unsigned n) {
		unsigned rv = accum & ((1<<n)-1);

		consume(n);

		return rv;
	}

	bool empty() const {
		return bits != 0;
	}

	unsigned avail() const {
		return bits;
	}

	unsigned bitsleft() const {
		return bits + (mBytesLeftLimited<<3);
	}

	unsigned bytesleft() const {
		return (bits>>3) + mBytesLeftLimited;
	}

	void align() {
		consume(bits&7);
	}

	void readbytes(void *dst, unsigned len);

protected:
	enum { kBigAvailThreshold = 16777216 };
	enum { kBufferSize = 256 };

	unsigned long accum      = 0;
	unsigned      bits       = 0;
	int           mBufferPt  = 0;      // counts from -256 to 0
	uint64        mBytesLeft = 0;
	unsigned      mBytesLeftLimited = 0;

	IVDStream* mpSrc = nullptr;
	uint8	mBuffer[kBufferSize];
};

class VDCRCChecker {
public:
	enum {
		kCRC32		= 0xEDB88320		// CRC-32 used by PKZIP, PNG (x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + 1)
	};

	VDCRCChecker() = default;
	VDCRCChecker(uint32 crc) { Init(crc); }

	void Init(uint32 crc);
	void Process(const void *src, sint32 len);

	uint32 CRC() const { return ~mValue; }
	uint32 CRC(uint32 crc, const void *src, sint32 len);

protected:
	uint32	mValue;
	uint32	mTable[256];
};

class VDZipStream : public IVDStream {
public:
	VDZipStream();
	VDZipStream(IVDStream *pSrc, uint64 limit, bool bStored);
	~VDZipStream() = default;

	void	Init(IVDStream *pSrc, uint64 limit, bool bStored);
	void	EnableCRC(uint32 crc = VDCRCChecker::kCRC32) { mCRCChecker.Init(crc); mbCRCEnabled = true; }
	uint32	CRC() { return mCRCChecker.CRC(); }

	const wchar_t *GetNameForError();

	sint64	Pos();
	void	Read(void *buffer, sint32 bytes);
	sint32	ReadData(void *buffer, sint32 bytes);
	void	Write(const void *buffer, sint32 bytes);

protected:
	bool	ParseBlockHeader();
	bool	Inflate();

	VDDeflateBitReader mBits;					// critical -- make this first!
	uint32	mReadPt, mWritePt, mBufferLevel;

	enum {
		kNoBlock,
		kStoredBlock,
		kDeflatedBlock
	} mBlockType;

	uint32	mStoredBytesLeft;
	bool	mbNoMoreBlocks;
	bool	mbCRCEnabled;

	sint64	mPos;
	uint8	mBuffer[65536];

	uint16	mCodeDecode[32768];
	uint8	mCodeLengths[288 + 32];
	uint16	mDistDecode[32768];

	VDCRCChecker	mCRCChecker;
};

class VDZipArchive {
public:
	struct FileInfo {
		VDStringA	mFileName;
		uint32		mCompressedSize;
		uint32		mUncompressedSize;
		uint32		mCRC32;
		bool		mbPacked;
	};

	VDZipArchive() = default;
	~VDZipArchive() = default;

	void Init(IVDRandomAccessStream *pSrc);

	sint32			GetFileCount();
	const FileInfo&	GetFileInfo(sint32 idx);
	IVDStream		*OpenRawStream(sint32 idx);

protected:
	struct FileInfoInternal : public FileInfo {
		uint32		mDataStart;
	};

	std::vector<FileInfoInternal>	mDirectory;
	IVDRandomAccessStream			*mpStream;
};

class VDGUnzipStream : public VDZipStream {
public:
	VDGUnzipStream() = default;
	VDGUnzipStream(IVDStream *pSrc, uint64 limit);
	~VDGUnzipStream() = default;

	void Init(IVDStream *pSrc, uint64 limit);

	const char *GetFilename() const { return mFilename.c_str(); }

protected:
	VDStringA mFilename;
};

#endif

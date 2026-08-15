// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2002 Avery Lee
// Copyright (C) 2016-2019 Anton Shekhovtsov
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_IMAGE_H
#define f_IMAGE_H

#include <vd2/system/vdtypes.h>

struct VDPixmap;
class VDPixmapBuffer;

bool DecodeBMPHeader(const void *pBuffer, long cbBuffer, int& w, int& h, bool& bHasAlpha);
void DecodeBMP(const void *pBuffer, long cbBuffer, const VDPixmap& vb);

bool DecodeTGAHeader(const void *pBuffer, long cbBuffer, int& w, int& h, int& format, bool& bHasAlpha);
void DecodeTGA(const void *pBuffer, long cbBuffer, const VDPixmap& vb);

bool VDIsJPEGHeader(const void *pv, uint32 len);
bool VDIsMayaIFFHeader(const void *pv, uint32 len);

void DecodeImage(const void *pBuffer, long cbBuffer, VDPixmapBuffer& vb, int desired_format, bool& bHasAlpha);
void DecodeImage(const wchar_t* pszFile, VDPixmapBuffer& vb, int desired_format, bool& bHasAlpha);

#endif

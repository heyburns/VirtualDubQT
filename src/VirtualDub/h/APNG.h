// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2016 Anton Shekhovtsov
//
// Animated PNG support by Max Stepin
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "../../src/zlib/zlib.h"

#define PNG_ZBUF_SIZE               32768

#define PNG_COLOR_MASK_PALETTE      1
#define PNG_COLOR_MASK_COLOR        2
#define PNG_COLOR_MASK_ALPHA        4

#define PNG_COLOR_TYPE_GRAY         0
#define PNG_COLOR_TYPE_PALETTE      (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_PALETTE)
#define PNG_COLOR_TYPE_RGB          (PNG_COLOR_MASK_COLOR)
#define PNG_COLOR_TYPE_RGB_ALPHA    (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_ALPHA)
#define PNG_COLOR_TYPE_GRAY_ALPHA   (PNG_COLOR_MASK_ALPHA)

#define PNG_COMPRESSION_TYPE_BASE   0
#define PNG_FILTER_TYPE_BASE        0

#define PNG_INTERLACE_NONE          0
#define PNG_INTERLACE_ADAM7         1

#define PNG_DISPOSE_OP_NONE         0
#define PNG_DISPOSE_OP_BACKGROUND   1
#define PNG_DISPOSE_OP_PREVIOUS     2

#define PNG_BLEND_OP_SOURCE         0
#define PNG_BLEND_OP_OVER           1

#define PNG_FILTER_VALUE_NONE       0
#define PNG_FILTER_VALUE_SUB        1
#define PNG_FILTER_VALUE_UP         2
#define PNG_FILTER_VALUE_AVG        3
#define PNG_FILTER_VALUE_PAETH      4

extern "C" {

struct png_struct_def
{
	z_stream zstream1;
	z_stream zstream2;
	uint8  * zbuf1;
	uint8  * zbuf2;
	size_t   zbuf_size;

	uint32   width;
	uint32   height;
	uint32   rowbytes;
	uint32   idat_size;
	uint32   pixel_depth;
	uint32   bpp;

	uint32   next_seq_num;
	uint32   num_frames_written;

	uint8  * big_row_buf;
	uint8  * row_buf;
	uint8  * prev_row;
	uint8  * sub_row;
	uint8  * up_row;
	uint8  * avg_row;
	uint8  * paeth_row;

	uint8    color_type;
	uint8    bit_depth;
};

typedef struct png_struct_def png_struct;
typedef png_struct FAR * png_structp;

}

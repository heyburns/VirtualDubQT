/*
		This program is free software; you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation.

		This program is distributed in the hope that it will be useful,
		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with this program; if not, write to the Free Software
		Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
// original IT0051 by thejam79
// add YV12 mode by minamina 2003/05/01
//
// Borrowed from the author of IT.dll, whose name I
// could not determine. Modified for YV12 by Donald Graft.
// RGB32 Added by Klaus Post
// Converted to generic planar, and now using exact coordinates - NOT character coordinates by Klaus Post
// --------------------------------------------------
// Mods by StainlessS:
// Rename to DDigit:-
// Minor mods, Bounds Checking, Comments Added, DDigit files split, by StainlessS (SJ)
// Fixed bug in RGB routines, (Used Height instead of Height-1, for top line).
// Added Text color by index,
// Fixed EXT ASCII bug, reading *char (signed) into int with sign extension and
// "out of font" access, eg print chr$(137) COPYRIGHT symbol caused gobbledygook
// hieroglyphics output. AND with 0xFF internally fixes errors propagated from external
// stubbs or user written routines.
// Font renamed so as to allow future inclusion with INFO.H routines, where doing comparison
// for speed etc, (never did bother to find out how that namespace stuff works).
//
// SJ, 25 March 2010
// Removed DDigitxxxx character printing functions,using internal character font index.
//   Assumes knowledge of internal font.
// Moved character -> font index conversion to DDigitIX_xxxx instead of in calling stubs,
//   Assumes knowledge of internal font Any client stubs  must call DDigitIX with
// EXT ASCII so no more using *s - ' ', Use just plain *s instead.
// Added built in vertical string printing functions, top down.
//
// SJ, 20 May 2010
// New colors chosen, better in YV12.
//   Completly changed whole lot. now single basic lo level function DDigitS() which
// does everything, pixel/character coords, horizontal/vertical string printing instead
// of single characters, also color by index. If additional color spaces added, then
// the mods should only need doing in the low level DDigitS() and not in any stubs.
// Like this much better. Also, YV12 routine partly ready for eg Y8 and other planar
// formats, if I knew what way to check for them.
// SJ, 17 Sept 2010, first Release of major mod. (Wrongly Date named DDigit_25_dll_20101017)
//
// 24 Sept 2010, Added optimizations suggested by IanB + 1 or 2 more.
// Moved some colorspace specific precalcs to function entry  point, size reduction with little
// impact on speed.
// FIXED, DDIGIT_CHARACTERS in DDIGIT.H (was 224 should be 224-32 ie 192)
//  DDigitTest plugin now considered standard inclusion in release, will reveal most problems
// if any modifications added.
//
// Added Speed test arg to DDigitTest,
//
// DDigitTest includes modified "Info_MOD.H", which appends "YV12" to YV12 routines and adds RGB24,
// Also, fixes for height-1 bug and ASCII sign extension bug.
// Will in future use DDigitTest_25_dll_DATE to release.
// 26 Sept 2010 Second release,
//
// SJ, 6 Mar 2011
//  Added, #define DDIGIT_VERSION to header in format: "v1.01 - 6 Mar 2011"
//  Changed to using the new Info.h of October 2010 in DDigitTest, renamed to Info_NEW.H
//  Speeded up YV12 somewhat, by thieving some of the mods from New Info.H.
//  Added control code parsing for formatting and color selection within print string.
//   '\n', Newline, positioning cursor 1 line down and at left edge of screen.
//   '\r', Newline Special, moves 1 line down and positions cursor at original X position.
//   '\b', Backspace, obvious, not sure how useful this will be.
//   '\f', Forward Space, again obvious, re-uses formfeed code for this. Again, maybe not so useful.
//   '\t', Tab, @ character positions, every 4 characters.(relative screen LHS).
//   '\a', Color selection control code. Followed by any one of '0123456789ABCDEF-!*'.
//         0 -> F select the colors 0 -> 15. (lower case also allowed).
//         '-' selects DDIGIT_DEFAULT.
//         '!' selects DDIGIT_HILITE.
//         '*' selects the on entry color.
//  Added optional 'ctrl' arg to DDigitS() which if set to false (default true) temporarily
//  switches OFF new control code functions. (Just for show full character set test really).
//  Added the rshift and dshift fixes for chroma alignment problem introduced in the
//  xSubS, ySubS mod.
//  Simplified the multiply by 7/8 on YUV background pixels, reduction of one subtract operation.
// 6 March 2011 third release v1.01 ,
//
// SJ, 11 Mar 2011
//  Fixed dshift bug introduced for Planar in v1.01.
// 11 March 2011 Fouth release v1.02 ,
//
// SJ, 25 May 2011
//  Added binary saving of 4KB if not using extended ASCII:-
//  #define	DDIGIT_ENABLE_EXTENDED_ASCII	// Enable full character set (incl 128 to 223)
//											// Comment out saves 4kb (ASCII 32-127 only)
//  Added Defines:-
//  Lowest and Highest valid ASCII character CODES in font.
//  Lowest code = 32 (SPACE), Highest = 127 (ASCII only) or 223 (With Extended ASCII enabled)
//  DDIGIT_CHARACTERS is defined as 96 (ASCII only) or 192 (EXTENDED ASCII).
//  #define DDIGIT_CHAR_MIN	32     // SPACE
//  #define DDIGIT_CHAR_MAX	(DDIGIT_CHAR_MIN+DDIGIT_CHARACTERS-1)
//  Error specifying color index, no longer returns without doing anything, converts
//  to default print color instead
//  General mods including dshift (again).
//  Added DDigit DISCRETE Colorspace Dependant renderers.
//  Final check before release of Info.h at SourceForge, new version with EXT ASCII fix applied,
//  renamed to Info_MOD.H and now using that (Version of 17 April 2011), minor alterations only.
//  DDigitTest cleaned up (a bit) and Synthesized Formatted Printing for Info.h routines added
//  to Demo.
//  Now including InfoF.H with routines & stubs for Formatted print via standard new Info.H.
// 2nd June 2011, Fifth release v1.03
// 20th Apr 2013, Sixth release v1.04
//  Background multiplier now selectable, via define DDIGIT_BACKGROUNDFADE in header.
//   Options: mult by 7/8, 3/4 5/8, 1/2 or OPAQUE BLACK background.
//  Added compile for v2.6 plugin stuff.
// 05 June 2013, v1.05. Extended CMAP to 32. YV411 Luma ONLY. New color control codes added.
//   '\a', Color selection control code. Followed by any one of '0123456789ABCDEFGHIJKLMNOPQRSTUV-!*'.
//         0 -> V select the colors 0 -> 31. (lower case also allowed).
//         '-' selects DDIGIT_DEFAULT.
//         '!' selects DDIGIT_HILITE.
//         '*' selects the on entry color.
// 10 July 2013, Added Length Arg.
//    Switched ON YV411 chroma render, although looks better luma only (uncomment #define DDIGIT_SKIP_YV411_CHROMA)

// --------------------------------------------------
// DDIGIT @ http://forum.doom9.org/showthread.php?t=156888
// --------------------------------------------------

#include "DDigit.h"

// SJ, Name Change, ASCII numeric index comments added (real tedious to count)
const unsigned short DDigitFont[][DDIGIT_CHAR_HEIGHT] = {
	//STARTCHAR space 32
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ! 33
	{
		0x0000,0x0000,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0000,0x0c00,0x0c00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR " 34
	{
		0x0000,0x0000,0x3300,0x3300,
		0x3300,0x1200,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR # 35
	{
		0x0000,0x0000,0x0000,0x0d80,
		0x0d80,0x0d80,0x3fc0,0x1b00,
		0x1b00,0x1b00,0x7f80,0x3600,
		0x3600,0x3600,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR $ 36
	{
		0x0000,0x0000,0x0c00,0x3f00,
		0x6d80,0x6c00,0x6c00,0x6c00,
		0x3f00,0x0d80,0x0d80,0x0d80,
		0x6d80,0x3f00,0x0c00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR % 37
	{
		0x0000,0x0000,0x0000,0x3980,
		0x6d80,0x6f00,0x3b00,0x0600,
		0x0600,0x0c00,0x0c00,0x1b80,
		0x1ec0,0x36c0,0x3380,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR & 38
	{
		0x0000,0x0000,0x1c00,0x3600,
		0x3600,0x3600,0x3c00,0x1800,
		0x3800,0x6c00,0x66c0,0x6380,
		0x6300,0x7780,0x3cc0,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ' 39
	{
		0x0000,0x0000,0x0f00,0x0e00,
		0x1800,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ( 40
	{
		0x0000,0x0000,0x0300,0x0600,
		0x0c00,0x0c00,0x1800,0x1800,
		0x1800,0x1800,0x1800,0x0c00,
		0x0c00,0x0600,0x0300,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ) 41
	{
		0x0000,0x0000,0x3000,0x1800,
		0x0c00,0x0c00,0x0600,0x0600,
		0x0600,0x0600,0x0600,0x0c00,
		0x0c00,0x1800,0x3000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR * 42
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x3300,0x3300,0x1e00,
		0x7f80,0x1e00,0x3300,0x3300,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR + 43
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0c00,0x0c00,0x0c00,
		0x7f80,0x0c00,0x0c00,0x0c00,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR , 44
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0e00,0x0e00,0x1800,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR - 45
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x7f80,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR . 46
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0e00,0x0e00,0x0e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR / 47
	{
		0x0000,0x0000,0x0000,0x0180,
		0x0180,0x0300,0x0300,0x0600,
		0x0600,0x0c00,0x0c00,0x1800,
		0x1800,0x3000,0x3000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 0 48
	{
		0x0000,0x0000,0x0c00,0x1e00,
		0x3300,0x3300,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x3300,
		0x3300,0x1e00,0x0c00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 1 49
	{
		0x0000,0x0000,0x0c00,0x1c00,
		0x3c00,0x6c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x7f80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 2 50
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6180,0x0180,0x0180,
		0x0300,0x0e00,0x1800,0x3000,
		0x6000,0x6000,0x7f80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 3 51
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6180,0x0180,0x0300,
		0x0e00,0x0300,0x0180,0x6180,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 4 52
	{
		0x0000,0x0000,0x0100,0x0300,
		0x0700,0x0f00,0x1b00,0x3300,
		0x6300,0x6300,0x7f80,0x0300,
		0x0300,0x0300,0x0300,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 5 53
	{
		0x0000,0x0000,0x7f80,0x6000,
		0x6000,0x6000,0x6000,0x6e00,
		0x7300,0x0180,0x0180,0x0180,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 6 54
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6100,0x6000,0x6000,0x6e00,
		0x7300,0x6180,0x6180,0x6180,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 7 55
	{
		0x0000,0x0000,0x7f80,0x0180,
		0x0180,0x0300,0x0300,0x0600,
		0x0600,0x0c00,0x0c00,0x1800,
		0x1800,0x3000,0x3000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 8 56
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6180,0x6180,0x3300,
		0x1e00,0x3300,0x6180,0x6180,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR 9 57
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6180,0x6180,0x6180,
		0x3380,0x1d80,0x0180,0x0180,
		0x2180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR : 58
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0e00,0x0e00,0x0000,
		0x0000,0x0000,0x0000,0x0e00,
		0x0e00,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ; 59
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0e00,0x0e00,0x0000,
		0x0000,0x0000,0x0000,0x0e00,
		0x0e00,0x1c00,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR < 60
	{
		0x0000,0x0000,0x0100,0x0300,
		0x0600,0x0c00,0x1800,0x3000,
		0x6000,0x3000,0x1800,0x0c00,
		0x0600,0x0300,0x0100,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR = 61
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x7f80,0x0000,
		0x0000,0x0000,0x0000,0x7f80,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR > 62
	{
		0x0000,0x0000,0x2000,0x3000,
		0x1800,0x0c00,0x0600,0x0300,
		0x0180,0x0300,0x0600,0x0c00,
		0x1800,0x3000,0x2000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ? 63
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6180,0x6180,0x0300,
		0x0600,0x0c00,0x0c00,0x0c00,
		0x0000,0x0c00,0x0c00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR @ 64
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6780,0x6f80,0x6d80,
		0x6d80,0x6d80,0x6f00,0x6600,
		0x6000,0x3180,0x1f00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR A 65
	{
		0x0000,0x0000,0x0c00,0x1e00,
		0x3300,0x3300,0x6180,0x6180,
		0x6180,0x7f80,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR B 66
	{
		0x0000,0x0000,0x7c00,0x6600,
		0x6300,0x6300,0x6300,0x6600,
		0x7e00,0x6300,0x6180,0x6180,
		0x6180,0x6300,0x7e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR C 67
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6000,0x6000,0x6000,
		0x6000,0x6000,0x6000,0x6000,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR D 68
	{
		0x0000,0x0000,0x7e00,0x6300,
		0x6180,0x6180,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x6180,
		0x6180,0x6300,0x7e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR E 69
	{
		0x0000,0x0000,0x7f80,0x6000,
		0x6000,0x6000,0x6000,0x6000,
		0x7e00,0x6000,0x6000,0x6000,
		0x6000,0x6000,0x7f80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR F 70
	{
		0x0000,0x0000,0x7f80,0x6000,
		0x6000,0x6000,0x6000,0x6000,
		0x7e00,0x6000,0x6000,0x6000,
		0x6000,0x6000,0x6000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR G 71
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6000,0x6000,0x6000,
		0x6780,0x6180,0x6180,0x6180,
		0x6180,0x3380,0x1e80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR H 72
	{
		0x0000,0x0000,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x6180,
		0x7f80,0x6180,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR I 73
	{
		0x0000,0x0000,0x7f80,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x7f80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR J 74
	{
		0x0000,0x0000,0x0f80,0x0180,
		0x0180,0x0180,0x0180,0x0180,
		0x0180,0x0180,0x0180,0x6180,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR K 75
	{
		0x0000,0x0000,0x6180,0x6180,
		0x6300,0x6300,0x6600,0x6600,
		0x7c00,0x6600,0x6600,0x6300,
		0x6300,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR L 76
	{
		0x0000,0x0000,0x6000,0x6000,
		0x6000,0x6000,0x6000,0x6000,
		0x6000,0x6000,0x6000,0x6000,
		0x6000,0x6000,0x7f80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR M 77
	{
		0x0000,0x0000,0x6180,0x6180,
		0x7380,0x7380,0x7f80,0x6d80,
		0x6d80,0x6d80,0x6d80,0x6180,
		0x6180,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR N 78
	{
		0x0000,0x0000,0x6180,0x7180,
		0x7180,0x7980,0x7980,0x6d80,
		0x6d80,0x6780,0x6780,0x6380,
		0x6380,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR O 79
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6180,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x6180,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR P 80
	{
		0x0000,0x0000,0x7e00,0x6300,
		0x6180,0x6180,0x6180,0x6180,
		0x6300,0x7e00,0x6000,0x6000,
		0x6000,0x6000,0x6000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR Q 81
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6180,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x6d80,
		0x6780,0x3300,0x1f00,0x0180,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR R 82
	{
		0x0000,0x0000,0x7e00,0x6300,
		0x6180,0x6180,0x6180,0x6180,
		0x6300,0x7e00,0x6600,0x6300,
		0x6300,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR S 83
	{
		0x0000,0x0000,0x1e00,0x3300,
		0x6180,0x6000,0x6000,0x3000,
		0x1e00,0x0300,0x0180,0x0180,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR T 84
	{
		0x0000,0x0000,0x7f80,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR U 85
	{
		0x0000,0x0000,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x6180,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR V 86
	{
		0x0000,0x0000,0x6180,0x6180,
		0x6180,0x6180,0x3300,0x3300,
		0x3300,0x1e00,0x1e00,0x1e00,
		0x0c00,0x0c00,0x0c00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR W 87
	{
		0x0000,0x0000,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x6d80,
		0x6d80,0x6d80,0x6d80,0x7380,
		0x7380,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR X 88
	{
		0x0000,0x0000,0x6180,0x6180,
		0x3300,0x3300,0x1e00,0x1e00,
		0x0c00,0x1e00,0x1e00,0x3300,
		0x3300,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR Y 89
	{
		0x0000,0x0000,0x6180,0x6180,
		0x3300,0x3300,0x1e00,0x1e00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR Z 90
	{
		0x0000,0x0000,0x7f80,0x0180,
		0x0180,0x0300,0x0600,0x0600,
		0x0c00,0x1800,0x1800,0x3000,
		0x6000,0x6000,0x7f80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR [ 91
	{
		0x0000,0x0000,0x3f00,0x3000,
		0x3000,0x3000,0x3000,0x3000,
		0x3000,0x3000,0x3000,0x3000,
		0x3000,0x3000,0x3f00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR BackSlash 92
	{
		0x0000,0x0000,0x0000,0x3000,
		0x3000,0x1800,0x1800,0x0c00,
		0x0c00,0x0600,0x0600,0x0300,
		0x0300,0x0180,0x0180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ] 93
	{
		0x0000,0x0000,0x3f00,0x0300,
		0x0300,0x0300,0x0300,0x0300,
		0x0300,0x0300,0x0300,0x0300,
		0x0300,0x0300,0x3f00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ^ 94
	{
		0x0000,0x0000,0x0c00,0x1e00,
		0x3300,0x6180,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR _ 95
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x7fc0,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ` 96
	{
		0x0000,0x0000,0x3c00,0x1c00,
		0x0600,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR a  97
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x1f00,
		0x3180,0x0180,0x3f80,0x6180,
		0x6180,0x6180,0x3e80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR b 98
	{
		0x0000,0x0000,0x6000,0x6000,
		0x6000,0x6000,0x6000,0x6e00,
		0x7300,0x6180,0x6180,0x6180,
		0x6180,0x7300,0x6e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR c 99
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x1f00,
		0x3180,0x6000,0x6000,0x6000,
		0x6000,0x3180,0x1f00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR d 100
	{
		0x0000,0x0000,0x0180,0x0180,
		0x0180,0x0180,0x0180,0x1d80,
		0x3380,0x6180,0x6180,0x6180,
		0x6180,0x3380,0x1d80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR e 101
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x1e00,
		0x3300,0x6180,0x7f80,0x6000,
		0x6000,0x3180,0x1f00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR f 102
	{
		0x0000,0x0000,0x0f00,0x1980,
		0x1980,0x1800,0x1800,0x1800,
		0x1800,0x7e00,0x1800,0x1800,
		0x1800,0x1800,0x1800,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR g 103
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x3e80,
		0x6380,0x6300,0x6300,0x6300,
		0x3e00,0x6000,0x3f00,0x6180,
		0x6180,0x6180,0x3f00,0x0000,
	},
	//STARTCHAR h 104
	{
		0x0000,0x0000,0x6000,0x6000,
		0x6000,0x6000,0x6000,0x6e00,
		0x7300,0x6180,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR i 105
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0c00,0x0c00,0x0000,0x3c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x7f80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR j 106
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0180,0x0180,0x0000,0x0780,
		0x0180,0x0180,0x0180,0x0180,
		0x0180,0x0180,0x0180,0x3180,
		0x3180,0x3180,0x1f00,0x0000,
	},
	//STARTCHAR k 107
	{
		0x0000,0x0000,0x6000,0x6000,
		0x6000,0x6000,0x6000,0x6300,
		0x6600,0x6c00,0x7800,0x7c00,
		0x6600,0x6300,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR l 108
	{
		0x0000,0x0000,0x3c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x7f80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR m 109
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x5b00,
		0x7f80,0x6d80,0x6d80,0x6d80,
		0x6d80,0x6d80,0x6d80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR n 110
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x6e00,
		0x7300,0x6180,0x6180,0x6180,
		0x6180,0x6180,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR o 111
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x1e00,
		0x3300,0x6180,0x6180,0x6180,
		0x6180,0x3300,0x1e00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR p 112
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x6e00,
		0x7300,0x6180,0x6180,0x6180,
		0x6180,0x7300,0x6e00,0x6000,
		0x6000,0x6000,0x6000,0x0000,
	},
	//STARTCHAR q 113
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x1d80,
		0x3380,0x6180,0x6180,0x6180,
		0x6180,0x3380,0x1d80,0x0180,
		0x0180,0x0180,0x0180,0x0000,
	},
	//STARTCHAR r 114
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x6f00,
		0x3980,0x3000,0x3000,0x3000,
		0x3000,0x3000,0x3000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR s 115
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x3f00,
		0x6180,0x6000,0x3f00,0x0180,
		0x0180,0x6180,0x3f00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR t 116
	{
		0x0000,0x0000,0x0000,0x0000,
		0x1800,0x1800,0x1800,0x7e00,
		0x1800,0x1800,0x1800,0x1800,
		0x1800,0x1980,0x0f00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR u 117
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x6180,
		0x6180,0x6180,0x6180,0x6180,
		0x6180,0x3380,0x1d80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR v 118
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x6180,
		0x6180,0x3300,0x3300,0x1e00,
		0x1e00,0x0c00,0x0c00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR w 119
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x6180,
		0x6180,0x6180,0x6d80,0x6d80,
		0x6d80,0x7f80,0x3300,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR x 120
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x6180,
		0x3300,0x1e00,0x0c00,0x0c00,
		0x1e00,0x3300,0x6180,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR y 121
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x6180,
		0x6180,0x6180,0x6180,0x6180,
		0x6180,0x3380,0x1d80,0x0180,
		0x6180,0x3300,0x1e00,0x0000,
	},
	//STARTCHAR z 122
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x3f80,
		0x0180,0x0300,0x0600,0x0c00,
		0x1800,0x3000,0x3f80,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR { 123
	{
		0x0000,0x0000,0x0780,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x7800,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0780,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR bar 124
	{
		0x0000,0x0000,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR } 125
	{
		0x0000,0x0000,0x7800,0x0c00,
		0x0c00,0x0c00,0x0c00,0x0c00,
		0x0780,0x0c00,0x0c00,0x0c00,
		0x0c00,0x0c00,0x7800,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR ~ 126
	{
		0x0000,0x0000,0x3980,0x6d80,
		0x6700,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	},
	//STARTCHAR C177 127
	{
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
		0x0000,0x0000,0x0000,0x0000,
	}
	#ifdef DDIGIT_ENABLE_EXTENDED_ASCII
		//STARTCHAR C240 128
		,{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR exclamdown 129
		{
			0x0000,0x0000,0x0c00,0x0c00,
			0x0000,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR cent 130
		{
			0x0000,0x0000,0x0000,0x0c00,
			0x0c00,0x1e00,0x3300,0x6100,
			0x6000,0x6000,0x6100,0x3300,
			0x1e00,0x0c00,0x0c00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR sterling 131
		{
			0x0000,0x0000,0x0000,0x0f00,
			0x1980,0x1980,0x1800,0x1800,
			0x7e00,0x1800,0x1800,0x1800,
			0x7c00,0x56c0,0x7380,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR currency 132
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x4040,0x2e80,0x1f00,
			0x3180,0x3180,0x3180,0x1f00,
			0x2e80,0x4040,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR yen 133
		{
			0x0000,0x0000,0x0000,0x0000,
			0x4080,0x6180,0x3300,0x1e00,
			0x3f00,0x0c00,0x3f00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR brokenbar 134
		{
			0x0000,0x0000,0x0c00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0000,
			0x0000,0x0000,0x0c00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR section 135
		{
			0x0000,0x0000,0x3e00,0x6300,
			0x6000,0x7000,0x7800,0x4c00,
			0x6600,0x3300,0x1900,0x0f00,
			0x0300,0x6300,0x3e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR dieresis 136
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR copyright 137
		{
			0x0000,0x0000,0x0000,0x0000,
			0x1e00,0x3300,0x6180,0x5e80,
			0x5280,0x5080,0x5280,0x5e80,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ordfeminine 138
		{
			0x0000,0x0000,0x1f00,0x2180,
			0x0180,0x3f80,0x6180,0x6180,
			0x3e80,0x0000,0x7f80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR guillmotleft 139
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0480,0x0d80,0x1b00,0x3600,
			0x6c00,0xd800,0x6c00,0x3600,
			0x1b00,0x0d80,0x0480,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR logicalnot 140
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x7f80,
			0x7f80,0x0180,0x0180,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR hyphen 141
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x3f00,
			0x3f00,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR registered 142
		{
			0x0000,0x0000,0x0000,0x0000,
			0x1e00,0x3300,0x6180,0x5e80,
			0x5280,0x5e80,0x5480,0x5680,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR macron 143
		{
			0x0000,0x0000,0x0000,0x7f00,
			0x7f00,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR degree 144
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x3300,0x1e00,0x0c00,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR plusminus 145
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0c00,0x0c00,0x7f80,0x7f80,
			0x0c00,0x0c00,0x0000,0x7f80,
			0x7f80,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR twosuperior 146
		{
			0x0000,0x0000,0x1c00,0x3600,
			0x0600,0x0c00,0x1800,0x3000,
			0x3e00,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR threesuperior 147
		{
			0x0000,0x0000,0x1c00,0x3600,
			0x0200,0x0e00,0x0200,0x3600,
			0x1c00,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR acute 148
		{
			0x0000,0x0000,0x0600,0x0c00,
			0x1800,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR mu 149
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x6300,0x6300,0x6300,0x6300,
			0x6300,0x7700,0x7d00,0x6000,
			0x6000,0x6000,0x0000,0x0000,
		},
		//STARTCHAR paragraph 150
		{
			0x0000,0x0000,0x1f80,0x3f80,
			0x7d80,0x7d80,0x7d80,0x3d80,
			0x1d80,0x0580,0x0580,0x0580,
			0x0580,0x0580,0x0580,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR periodcentered 151
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0e00,
			0x0e00,0x0e00,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR cedilla 152
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0c00,
			0x0400,0x1200,0x0c00,0x0000,
		},
		//STARTCHAR onesuperior 153
		{
			0x0000,0x0000,0x1800,0x3800,
			0x1800,0x1800,0x1800,0x3c00,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ordmasculine 154
		{
			0x0000,0x0000,0x1c00,0x3600,
			0x6300,0x6300,0x6300,0x3600,
			0x1c00,0x0000,0x7f00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR guillemotright 155
		{
			0x0000,0x0000,0x0000,0x0000,
			0x4800,0x6c00,0x3600,0x1b00,
			0x0d80,0x06c0,0x0d80,0x1b00,
			0x3600,0x6c00,0x4800,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR onequarter 156
		{
			0x0000,0x0000,0x2000,0x6000,
			0x2080,0x2100,0x7200,0x0400,
			0x0900,0x1300,0x2500,0x4f00,
			0x0100,0x0100,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR onehalf 157
		{
			0x0000,0x0000,0x2000,0x6000,
			0x2080,0x2100,0x7200,0x0400,
			0x0b00,0x1480,0x2080,0x4100,
			0x0200,0x0780,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR threequarters 158
		{
			0x0000,0x0000,0x7000,0x0800,
			0x3080,0x0900,0x7200,0x0400,
			0x0900,0x1300,0x2500,0x4f80,
			0x0100,0x0100,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR questiondown 159
		{
			0x0000,0x0000,0x0c00,0x0c00,
			0x0000,0x0c00,0x0c00,0x0c00,
			0x1800,0x3000,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Agrave 160
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x0c00,0x1e00,
			0x3300,0x6180,0x6180,0x7f80,
			0x6180,0x6180,0x6180,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Aacute 161
		{
			0x0000,0x0000,0x0300,0x0600,
			0x0c00,0x0000,0x0c00,0x1e00,
			0x3300,0x6180,0x6180,0x7f80,
			0x6180,0x6180,0x6180,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Acircumflex 162
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x0c00,0x1e00,
			0x3300,0x6180,0x6180,0x7f80,
			0x6180,0x6180,0x6180,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Atilde 163
		{
			0x0000,0x0000,0x1900,0x3f00,
			0x2600,0x0000,0x0c00,0x1e00,
			0x3300,0x6180,0x6180,0x7f80,
			0x6180,0x6180,0x6180,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Adieresis 164
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x6180,0x6180,0x7f80,
			0x6180,0x6180,0x6180,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Aring 165
		{
			0x0000,0x0000,0x0c00,0x1200,
			0x1200,0x0c00,0x0c00,0x1e00,
			0x3300,0x6180,0x6180,0x7f80,
			0x6180,0x6180,0x6180,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR AE 166
		{
			0x0000,0x0000,0x0f80,0x1e00,
			0x3600,0x3600,0x6600,0x6600,
			0x7f80,0x6600,0x6600,0x6600,
			0x6600,0x6600,0x6780,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Ccedilla 167
		{
			0x0000,0x0000,0x1e00,0x3300,
			0x6180,0x6000,0x6000,0x6000,
			0x6000,0x6000,0x6000,0x6000,
			0x6180,0x3300,0x1e00,0x0c00,
			0x0400,0x1200,0x0c00,0x0000,
		},
		//STARTCHAR Egrave 168
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x7f80,0x6000,
			0x6000,0x6000,0x7e00,0x6000,
			0x6000,0x6000,0x7f80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Eacute 169
		{
			0x0000,0x0000,0x0600,0x0c00,
			0x1800,0x0000,0x7f80,0x6000,
			0x6000,0x6000,0x7e00,0x6000,
			0x6000,0x6000,0x7f80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Ecircumflex 170
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x7f80,0x6000,
			0x6000,0x6000,0x7e00,0x6000,
			0x6000,0x6000,0x7f80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Edieresis 171
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x7f80,0x6000,
			0x6000,0x6000,0x7e00,0x6000,
			0x6000,0x6000,0x7f80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Igrave 172
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x3f00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x3f00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Iacute 173
		{
			0x0000,0x0000,0x0600,0x0c00,
			0x1800,0x0000,0x3f00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x3f00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Icircumflex 174
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x3f00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x3f00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Idieresis 175
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x3f00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x3f00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Eth 176
		{
			0x0000,0x0000,0x7e00,0x6300,
			0x6180,0x6180,0x6180,0x6180,
			0xf980,0x6180,0x6180,0x6180,
			0x6180,0x6300,0x7e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Ntilde 177
		{
			0x0000,0x0000,0x1900,0x3f00,
			0x2600,0x0000,0x4180,0x6180,
			0x7180,0x7980,0x7d80,0x6f80,
			0x6780,0x6380,0x6180,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Ograve 178
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x1e00,0x3300,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Oacute 179
		{
			0x0000,0x0000,0x0300,0x0600,
			0x0c00,0x0000,0x1e00,0x3300,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Ocircumflex 180
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x1e00,0x3300,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Otilde 181
		{
			0x0000,0x0000,0x1900,0x3f00,
			0x2600,0x0000,0x1e00,0x3300,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Odieresis 182
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x1e00,0x3300,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR multiply 183
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x2080,
			0x3180,0x1b00,0x0e00,0x0e00,
			0x1b00,0x3180,0x2080,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Oslash 184
		{
			0x0000,0x0080,0x1f00,0x3300,
			0x6380,0x6380,0x6580,0x6580,
			0x6580,0x6980,0x6980,0x6980,
			0x7180,0x3300,0x3e00,0x4000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Ugrave 185
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x6180,0x6180,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Uacute 186
		{
			0x0000,0x0000,0x0300,0x0600,
			0x0c00,0x0000,0x6180,0x6180,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Ucircumflex 187
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x6180,0x6180,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3380,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Udieresis 188
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x6180,0x6180,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Yacute 189
		{
			0x0000,0x0000,0x0300,0x0600,
			0x0c00,0x0000,0x4080,0x6180,
			0x3300,0x1e00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x0c00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR Thorn 190
		{
			0x0000,0x0000,0x0000,0x0000,
			0x3c00,0x1800,0x1f00,0x1980,
			0x1980,0x1980,0x1f00,0x1800,
			0x1800,0x1800,0x3c00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR germandbls 191
		{
			0x0000,0x0000,0x0000,0x1c00,
			0x3e00,0x7300,0x6300,0x6300,
			0x6600,0x6c00,0x6600,0x6300,
			0x6100,0x6300,0x6e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR agave 192
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x0000,0x3f00,
			0x6180,0x0180,0x3f80,0x6180,
			0x6180,0x6180,0x3e80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR aacute 193
		{
			0x0000,0x0000,0x0600,0x0c00,
			0x1800,0x0000,0x0000,0x3f00,
			0x6180,0x0180,0x3f80,0x6180,
			0x6180,0x6180,0x3e80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR acircumflex 194
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x0000,0x3f00,
			0x6180,0x0180,0x3f80,0x6180,
			0x6180,0x6180,0x3e80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR atilde 195
		{
			0x0000,0x0000,0x1900,0x3f00,
			0x2600,0x0000,0x0000,0x3f00,
			0x6180,0x0180,0x3f80,0x6180,
			0x6180,0x6180,0x3e80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR adieresis 196
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x0000,0x3f00,
			0x6180,0x0180,0x3f80,0x6180,
			0x6180,0x6180,0x3e80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR aring 197
		{
			0x0000,0x0000,0x0000,0x0c00,
			0x1200,0x1200,0x0c00,0x3f00,
			0x6180,0x0180,0x3f80,0x6180,
			0x6180,0x6180,0x3e80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ae 198
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x3b00,
			0x4d80,0x0d80,0x0f00,0x3c00,
			0x6c00,0x6c80,0x3700,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR 0xccedilla 199
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x1f00,
			0x3180,0x6000,0x6000,0x6000,
			0x6000,0x3180,0x1f00,0x0c00,
			0x0400,0x1200,0x0c00,0x0000,
		},
		//STARTCHAR egrave 200
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x0000,0x1e00,
			0x3300,0x6180,0x7f80,0x6000,
			0x6000,0x3180,0x1f00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR eacute 201
		{
			0x0000,0x0000,0x0600,0x0c00,
			0x1800,0x0000,0x0000,0x1e00,
			0x3300,0x6180,0x7f80,0x6000,
			0x6000,0x3180,0x1f00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ecircumflex 202
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x0000,0x1e00,
			0x3300,0x6180,0x7f80,0x6000,
			0x6000,0x3180,0x1f00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR edieresis 203
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x0000,0x1e00,
			0x3300,0x6180,0x7f80,0x6000,
			0x6000,0x3180,0x1f00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR igrave 204
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x0000,0x3c00,
			0x0c00,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x7f80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR iacute 205
		{
			0x0000,0x0000,0x0600,0x0c00,
			0x1800,0x0000,0x0000,0x3c00,
			0x0c00,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x7f80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR icircumflex 206
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x0000,0x3c00,
			0x0c00,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x7f80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR idieresis 207
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x0000,0x3c00,
			0x0c00,0x0c00,0x0c00,0x0c00,
			0x0c00,0x0c00,0x7f80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR eth 208
		{
			0x0000,0x0000,0x4400,0x6c00,
			0x3800,0x3800,0x6c00,0x4600,
			0x1f00,0x3380,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ntilde 209
		{
			0x0000,0x0000,0x1900,0x3f00,
			0x2600,0x0000,0x0000,0x6e00,
			0x7300,0x6180,0x6180,0x6180,
			0x6180,0x6180,0x6180,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ograve 210
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x0000,0x1e00,
			0x3300,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR oacute 211
		{
			0x0000,0x0000,0x0600,0x0c00,
			0x1800,0x0000,0x0000,0x1e00,
			0x3300,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ocircumflex 212
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x0000,0x1e00,
			0x3300,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR otilde 213
		{
			0x0000,0x0000,0x1900,0x3f00,
			0x2600,0x0000,0x0000,0x1e00,
			0x3300,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR odieresis 214
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x0000,0x1e00,
			0x3300,0x6180,0x6180,0x6180,
			0x6180,0x3300,0x1e00,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR divide 215
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0c00,0x0c00,0x0000,0x0000,
			0x7f80,0x7f80,0x0000,0x0000,
			0x0c00,0x0c00,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR oslash 216
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0080,0x1f00,
			0x3300,0x6580,0x6580,0x6980,
			0x6980,0x3300,0x3e00,0x4000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ugrave 217
		{
			0x0000,0x0000,0x3000,0x1800,
			0x0c00,0x0000,0x0000,0x6180,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3380,0x1d80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR uacute 218
		{
			0x0000,0x0000,0x0600,0x0c00,
			0x1800,0x0000,0x0000,0x6180,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3380,0x1d80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ucircumflex 219
		{
			0x0000,0x0000,0x0c00,0x1e00,
			0x3300,0x0000,0x0000,0x6180,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3380,0x1d80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR udieresis 220
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x0000,0x6180,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3380,0x1d80,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR yacute 221
		{
			0x0000,0x0000,0x0600,0x0c00,
			0x1800,0x0000,0x0000,0x0000,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3380,0x1d80,0x0180,
			0x6180,0x3300,0x1e00,0x0000,
		},
		//STARTCHAR thorn 222
		{
			0x0000,0x0000,0x0000,0x0000,
			0x0000,0x0000,0x0000,0x3800,
			0x1e00,0x1b00,0x1b00,0x1e00,
			0x1800,0x1800,0x3800,0x0000,
			0x0000,0x0000,0x0000,0x0000,
		},
		//STARTCHAR ydieresis 223
		{
			0x0000,0x0000,0x3300,0x3300,
			0x0000,0x0000,0x0000,0x0000,
			0x6180,0x6180,0x6180,0x6180,
			0x6180,0x3380,0x1d80,0x0180,
			0x6180,0x3300,0x1e00,0x0000,
		}
	#endif  // DDIGIT_ENABLE_EXTENDED_ASCII
};// Hi 10 bits active per short


#if (defined DDIGIT_ENABLE_MONOLITHIC_CODE || \
	defined DDIGIT_ENABLE_SUPPORT_RGB32 || \
	defined DDIGIT_ENABLE_SUPPORT_RGB24)

	const unsigned long DDigit_Cmap_RGB[32]={
		0XA9A9A9,0X1E90FF,0XFF4500,0XDA70D6,
		0X00FF00,0X7FFFD4,0XFFFF00,0XFFFFFF,
		0XC0C0C0,0X6495ED,0XFFA500,0XDDA0DD,
		0X7FFF00,0XB0E0E6,0XFFD700,0XDCDCDC,
		0X000000,0X111111,0X222222,0X333333,
		0X444444,0X555555,0X666666,0X777777,
		0X888888,0X999999,0XAAAAAA,0XBBBBBB,
		0XCCCCCC,0XDDDDDD,0XEEEEEE,0XFFFFFF
	};
#endif

#if (defined DDIGIT_ENABLE_MONOLITHIC_CODE || \
	defined DDIGIT_ENABLE_SUPPORT_PLANAR || \
	defined DDIGIT_ENABLE_SUPPORT_YUY2)

	const unsigned long DDigit_Cmap_YUV[32]={
		0XA18080,0X79C246,0X7446D7,0X959DA7,
		0X913622,0XC6804B,0XD21092,0XEB8080,
		0XB58080,0X8CAE64,0XA52AB3,0XAF9296,
		0XB1235A,0XC58A6A,0XBE1CA1,0XCD8080,
		0X108080,0X1F8080,0X2D8080,0X3C8080,
		0X4A8080,0X598080,0X688080,0X768080,
		0X858080,0X938080,0XA28080,0XB18080,
		0XBF8080,0XCE8080,0XDC8080,0XEB8080
	};
#endif


// Array of pointers to color Control Code Strings
const char * DIGIT_COLOR_CODES[DDIGIT_CMAP_NELS] = {
	DDIGIT_CC_DARKGRAY,
	DDIGIT_CC_DODGERBLUE,
	DDIGIT_CC_ORANGERED,
	DDIGIT_CC_ORCHID,
	DDIGIT_CC_LIME,
	DDIGIT_CC_AQUAMARINE,
	DDIGIT_CC_YELLOW,
	DDIGIT_CC_WHITE,
	DDIGIT_CC_SILVER,
	DDIGIT_CC_CORNFLOWERBLUE,
	DDIGIT_CC_ORANGE,
	DDIGIT_CC_PLUM,
	DDIGIT_CC_CHARTREUSE,
	DDIGIT_CC_POWDERBLUE,
	DDIGIT_CC_GOLD,
	DDIGIT_CC_GAINSBORO,
	DDIGIT_CC_Y_0,
	DDIGIT_CC_Y_1,
	DDIGIT_CC_Y_2,
	DDIGIT_CC_Y_3,
	DDIGIT_CC_Y_4,
	DDIGIT_CC_Y_5,
	DDIGIT_CC_Y_6,
	DDIGIT_CC_Y_7,
	DDIGIT_CC_Y_8,
	DDIGIT_CC_Y_9,
	DDIGIT_CC_Y_A,
	DDIGIT_CC_Y_B,
	DDIGIT_CC_Y_C,
	DDIGIT_CC_Y_D,
	DDIGIT_CC_Y_E,
	DDIGIT_CC_Y_F
};


#if ((!defined DDIGIT_BACKGROUNDFADE)|| (DDIGIT_BACKGROUNDFADE == 0))
	// Multiply background by 7/8
	#if(0)
		#define DD_BGF_Y(dat)	(dat)=(unsigned char) (((((dat)-16)*7)>>3)+16)
		#define DD_BGF_C(dat)	(dat)=(unsigned char) (((((dat)-128)*7)>>3)+128)
		#define DD_BGF_RGB(dat)	(dat)=(unsigned char) (((dat)*7)>>3)
	#else
		// (dat*7>>3)-(16*7>>3)+16    ==  (dat*7>>3)+2
		#define DD_BGF_Y(dat)	(dat)=(unsigned char) (((dat)*7>>3)+2)
		// (dat*7>>3)-(128*7>>3)+128  ==  (dat*7>>3)+16
		#define DD_BGF_C(dat)	(dat)=(unsigned char) (((dat)*7>>3)+16)
		#define DD_BGF_RGB(dat)	(dat)=(unsigned char) ((dat)*7>>3)
	#endif
#elif (DDIGIT_BACKGROUNDFADE == 1)
	// Multiply background by 3/4
	#if(0)
		#define DD_BGF_Y(dat)	(dat)=(unsigned char) (((((dat)-16)*3)>>2)+16)
		#define DD_BGF_C(dat)	(dat)=(unsigned char) (((((dat)-128)*3)>>2)+128)
		#define DD_BGF_RGB(dat)	(dat)=(unsigned char) (((dat)*3)>>2)
	#else
		// (dat*3>>2)-(16*3>>2)+16    == (dat*3>>2)+4
		#define DD_BGF_Y(dat)	(dat)=(unsigned char) ((((dat)*3)>>2)+4)
		// (dat*3>>2)-(128*3>>2)+128  == (dat*3>>2)+32
		#define DD_BGF_C(dat)	(dat)=(unsigned char) ((((dat)*3)>>2)+32)
		#define DD_BGF_RGB(dat)	(dat)=(unsigned char) (((dat)*3)>>2)
	#endif
#elif (DDIGIT_BACKGROUNDFADE == 2)
	// Multiply background by 5/8
	#if(0)
		#define DD_BGF_Y(dat)	(dat)=(unsigned char) (((((dat)-16)*5)>>3)+16)
		#define DD_BGF_C(dat)	(dat)=(unsigned char) (((((dat)-128)*5)>>3)+128)
		#define DD_BGF_RGB(dat)	(dat)=(unsigned char) (((dat)*5)>>3)
	#else
		// (dat*5>>3)-(16*5>>3)+16    == (dat*5>>3)+6
		#define DD_BGF_Y(dat)	(dat)=(unsigned char) ((((dat)*3)>>2)+4)
		// (dat*5>>3)-(128*5>>3)+128  == (dat*5>>3)+48
		#define DD_BGF_C(dat)	(dat)=(unsigned char) ((((dat)*3)>>2)+32)
		#define DD_BGF_RGB(dat)	(dat)=(unsigned char) (((dat)*3)>>2)
	#endif
#elif (DDIGIT_BACKGROUNDFADE == 3)
	// Multiply background by 1/2
	#if(0)
		#define DD_BGF_Y(dat)	(dat)=(unsigned char) ((((dat)-16)>>1)+16)
		#define DD_BGF_C(dat)	(dat)=(unsigned char) ((((dat)-128)>>1)+128)
		#define DD_BGF_RGB(dat)	(dat)=(unsigned char) ((dat)>>1)
	#else
		// (dat*1>>1)-(16*1>>1)+16    == (dat>>1)+8
		#define DD_BGF_Y(dat)	(dat)=(unsigned char) (((dat)>>1)+8)
		// (dat*1>>1)-(128*1>>1)+128  == (dat>>1)+64
		#define DD_BGF_C(dat)	(dat)=(unsigned char) (((dat)>>1)+64)
		#define DD_BGF_RGB(dat)	(dat)=(unsigned char) ((dat)>>1)
	#endif
#elif (DDIGIT_BACKGROUNDFADE == 4)
	// Black Opaque background
	#define DD_BGF_Y(dat)	(dat)=(unsigned char) (16)
	#define DD_BGF_C(dat)	(dat)=(unsigned char) (128)
	#define DD_BGF_RGB(dat)	(dat)=(unsigned char) (0)
#else
	#error Illegal DDIGIT_BACKGROUNDFADE in DDIGIT.H
#endif


const unsigned int CMapdef[2][2]={
	{DDIGIT_INDEX_TO_USE_AS_DEFAULT,DDIGIT_INDEX_TO_USE_AS_HILITE},
	{DDIGIT_Y_INDEX_TO_USE_AS_DEFAULT,DDIGIT_Y_INDEX_TO_USE_AS_HILITE}
};


#ifdef DDIGIT_ENABLE_MONOLITHIC_CODE

	// ----------------------------------------------------
	// ----------------------------------------------------
	// ----------------  MONOLITHIC CODE   ----------------
	// ----------------------------------------------------
	// ----------------------------------------------------


	void __stdcall DDigitS(const VideoInfo &vi,PVideoFrame &dst, int x, int y, int color, \
						  const bool pix,const bool vert,const char *s,bool ctrl,int length)
	{	// Print EXT ASCII string @ [x,y] pixel or character coords, by color[16] index
		// pix==true = pixel coords: vert==true = Vertical (Top Down)
		// ctrl=false, switches off control code parsing during full character set test.

		unsigned char * const Wptr	= dst->GetWritePtr();
		const int rowsize	= dst->GetRowSize();
		const int pitch		= dst->GetPitch();
		const int height	= dst->GetHeight();
		const int width		= vi.width;

		const int CharStepX = (vert) ? 0 : DDIGIT_CHAR_WIDTH;	// Vertical ?
		const int CharStepY = (vert) ? DDIGIT_CHAR_HEIGHT : 0;

		if(!pix) {						// Convert To Pixel coords
			x *= DDIGIT_CHAR_WIDTH;
			y *= DDIGIT_CHAR_HEIGHT;
		}

		const int in_x=x;				// Remember starting pixel X
		const int in_y=y;				// Remember starting pixel Y

		const bool	Planar	=	vi.IsPlanar();
		const bool	YUY2	=	vi.IsYUY2();
		const bool	RGB		=	vi.IsRGB();

		// Not initialized unless used.
		int	rowsizeUV;
		int pitchUV;
		int heightUV;
		int xSubS;
		int ySubS;
		unsigned char *WptrU;
		unsigned char *WptrV;
		unsigned int fontmask;
		int	step;				// Only used in RGB

		int cmapix=0;			// color cmap, 1 = luma defaults

		if(Planar) {
			if(rowsizeUV	= dst->GetRowSize(PLANAR_U)) {
				xSubS		= rowsize / rowsizeUV;			// Planar, .SubS = 1, 2 or 4
				fontmask	= (0xFFFF0000 >> xSubS) &0xFFFF;
				heightUV	= dst->GetHeight(PLANAR_U);
				ySubS		= height  / heightUV;
				WptrU		= dst->GetWritePtr(PLANAR_U);
				WptrV		= dst->GetWritePtr(PLANAR_V);
				pitchUV		= dst->GetPitch(PLANAR_U);
				#ifdef DDIGIT_SKIP_YV411_CHROMA
					if(DDIGIT_CHAR_WIDTH % xSubS !=0 || DDIGIT_CHAR_HEIGHT % ySubS !=0)
						cmapix=1;	// YV411 mono cmap defaults
				#endif
			} else {
				cmapix=1;		// Y8 mono cmap defaults
			}
		}

		if(color>=DDIGIT_CMAP_NELS || color<DDIGIT_HILITE)
			color=DDIGIT_DEFAULT;	// Ensure legal

		if(color == DDIGIT_DEFAULT)
			color=CMapdef[cmapix][0];				//	Default
		else if(color == DDIGIT_HILITE)
			color=CMapdef[cmapix][1];				//	Hilite

		int c1,c2,c3;
		if(RGB) {
			step=(vi.IsRGB24()) ? 3:4;
			c1=DDigit_Cmap_RGB[color];
		} else {
			c1=DDigit_Cmap_YUV[color];
		}

		c3=c1 & 0xFF;	c1>>=8U;
		c2=c1 & 0xFF;	c1>>=8U;
		c1 &= 0xFF;

		if(length<=0) {
			const char *fnd=s;
			for(;*fnd;++fnd);
			length=fnd-s;			// nul found, shorten length
		}

		int num;
		for (int s_ix=0;s_ix<length  ;++s_ix) {
			num=((unsigned char*)s)[s_ix];			// Cast to speed up ctrl code  detect
			#ifdef DDIGIT_ENABLE_SUPPORT_CONTROL_CODES
				if(num <= '\r' && num >= '\a' && (ctrl)) { // Control Character ?
					if(num == '\a') {				// Color control character ?
						int cc=s[s_ix+1];
						if(cc >= 'a' && cc <= 'v')
							cc -= ('a' - 'A');				// Upper Case
						char *colstr="0123456789ABCDEFGHIJKLMNOPQRSTUV-!*";
						char * ccp=colstr;
						while(*ccp && *ccp != cc)
							++ccp;
						if(*ccp) {
							++s_ix; // Color control code parsed with valid arg, skip ctrl code.('\a')
							cc=(ccp - colstr);
							if(cc>31) {
								if(cc==32) {							// '-' = ddigit default color
									cc = CMapdef[cmapix][0];			//	Default
								} else if(cc==33) {						// '!' = ddigit hilite
									cc = CMapdef[cmapix][1];			//	Hilite
								} else if(cc==34) {						// '*' = Original starting color
									cc=color;
								}
							}

							c1=(RGB) ? DDigit_Cmap_RGB[cc] : DDigit_Cmap_YUV[cc];
							c3=c1 & 0xFF;	c1>>=8U;
							c2=c1 & 0xFF;	c1>>=8U;
							c1 &= 0xFF;
							continue;
						}
					} else if (!vert) {					// Horizontal
						switch(num) {
						case '\n':						// NewLine
							x = 0;						// Screen left
							y += DDIGIT_CHAR_HEIGHT;	// Next line down
							continue;
						case '\r':						// 1 line down and back to original X position
							x = in_x;					// Below starting X input postion
							y += DDIGIT_CHAR_HEIGHT;	// Next line down
							continue;
						case '\t':						// Horizontal Tab
							x+= ((DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)-(x%(DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)));
							continue;
						case '\f':						// Forward Space
							x += DDIGIT_CHAR_WIDTH;		// 1 char right
							continue;
						case '\b':						// BackSpace
							x -= DDIGIT_CHAR_WIDTH;		// 1 char left
							continue;
						}
					} else {							// Vertical
						switch(num) {
						case '\n':						// NewLine
							y = 0;						// Screen Top
							x += DDIGIT_CHAR_WIDTH;		// Next line right
							continue;
						case '\r':						// 1 line right and back to original Y pos
							y = in_y;					// Right of starting Y input postion
							x += DDIGIT_CHAR_WIDTH;		// Next line right
							continue;
						case '\t':						// Horizontal Tab, do vertical instead
							y+= ((DDIGIT_TABCNT*DDIGIT_CHAR_HEIGHT)-(y%(DDIGIT_TABCNT*DDIGIT_CHAR_HEIGHT)));
							continue;
						case '\f':						// Forward Space, do down instead
							y += DDIGIT_CHAR_HEIGHT;	// 1 char Down
							continue;
						case '\b':						// BackSpace, do up instead
							y -= DDIGIT_CHAR_HEIGHT;	// 1 char UP
							continue;
						}
					}
				}
			#endif // DDIGIT_ENABLE_SUPPORT_CONTROL_CODES

			// Is there anything at all to do (for this character, REM both Horiz & Vert)?

			if(x<width &&  x> -DDIGIT_CHAR_WIDTH && y<height && y> -DDIGIT_CHAR_HEIGHT) {

				num = (num-' ') & 0xFF;		 // Conv font ix, Ensure +ve index
				if(num >= DDIGIT_CHARACTERS) // STRICT font range[0-191(or 95 if EXT ASCII disabled)]
					num=0;					 // Not in font, Convert to SPACE
				const unsigned short * FontNum = &DDigitFont[num][0];

				int xx=x, yy=y;			// MUST NOT change x,y (needed for next char in string)
				int txlft = 0, txrgt = DDIGIT_CHAR_WIDTH;
				if(xx < 0)					{txlft = -xx;	xx = 0;}
				else if(xx + DDIGIT_CHAR_WIDTH > width)	{txrgt = width - xx;}
				int tytop = 0, tybot = DDIGIT_CHAR_HEIGHT;
				if(yy < 0)					{tytop = -yy;	yy =0;}
				else if(yy + DDIGIT_CHAR_HEIGHT > height)	{tybot = height - yy;}

				if (Planar) {
				#ifdef DDIGIT_ENABLE_SUPPORT_PLANAR


					unsigned char *dpY=Wptr + xx + yy*pitch;
					for (int ty = tytop; ty < tybot;++ty,dpY += pitch) {
						unsigned char * rp = dpY;
						unsigned int fbm= FontNum[ty] << txlft;
						for (int tx = txlft; tx < txrgt; ++tx,++rp, fbm<<=1) {
							if (fbm & 0x8000) {		// Character Foreground SET Pixel ?
								rp[0] = c1; 			// Set Luma Foreground Pixel
							} else {
								DD_BGF_Y(rp[0]);		// fade background
							}
						}
					}
					if(rowsizeUV) {


						// Unfortunately, if the x start position is NOT exactly divisible by xSubS,
						// then the font bitmap for chroma is not in the correct position, and results
						// in 'washed out' characters with visible white edges. The
						// same thing happens if Y is not exactly a multiple of ySubS, however, this
						// does not produce as much of the 'washed out; appearance, perhaps just due
						// to the font itself (alignment). (Within the font, verticals [eg '|'] are two
						// luma pixels wide, whereas horizontals [eg '_'] are only 1 luma pixel tall).
						// We correct the chroma X problem by a right shift (rshift) of the font bitmap
						// and so the chroma is aligned with the luma pixels. For vertical, the solution
						// is a little more involved and that is corrected below by the dshift stuff.
						// The above fixes, seem to work but result in a 'swelling' of the characters
						// during the DDigitTest horizontal and vertical sliding tests. This is I believe
						// due to a pair of pixels being both within the same chroma sample and then
						// one in each of a pair of adjacent chroma samples when the X or Y offsets move
						// one pixel on. It is suggested that it may be best in YV12 to use only x (and
						// maybe y) character positions that are a multiple of 2 to avoid this 'swelling'.


						unsigned int rshift=xx & (xSubS-1);
						unsigned int dshift=yy & (ySubS-1);
						unsigned int yStep = ySubS-dshift;	// 1st step, thereafter ySubS

						const int SubSoff=(xx/xSubS) + ((yy/ySubS) * pitchUV);

						unsigned char * rpU =WptrU + SubSoff;
						unsigned char * rpV =WptrV + SubSoff;

						#ifdef DDIGIT_SKIP_YV411_CHROMA
							if(DDIGIT_CHAR_WIDTH % xSubS !=0 || DDIGIT_CHAR_HEIGHT % ySubS !=0) {
								// YV411 ... DO NOTHING
							} else {
						#endif
								for (int ty=tytop;ty<tybot;rpV+=pitchUV,rpU+=pitchUV,ty+=yStep,yStep=ySubS) {
									unsigned char * rpu =rpU;
									unsigned char * rpv =rpV;
									unsigned int fbm=0;

									// Read font data with dshift
									int me=ty+yStep;
									if(me>tybot)
										me=tybot;	// DONT read into next char font data, OR off screen
									for(int m=ty;m<me;++m)
										fbm |= FontNum[m];

									fbm<<=txlft;
									fbm>>=rshift;	// Fix X Chroma shift
									for (int tx = txlft; tx < txrgt; tx+=xSubS, ++rpu, ++rpv, fbm<<=xSubS) {

										if(fbm & fontmask) {
											rpu[0] = c2;
											rpv[0] = c3;
										} else {
											DD_BGF_C(rpu[0]);		// fade background
											DD_BGF_C(rpv[0]);
										}
									}
								}
						#ifdef DDIGIT_SKIP_YV411_CHROMA
							}
						#endif
				}
				#endif // DDIGIT_ENABLE_SUPPORT_PLANAR
				} else if (YUY2) {
				#ifdef DDIGIT_ENABLE_SUPPORT_YUY2
					unsigned char *dp = Wptr + xx*2 + yy*pitch;
					for (int ty = tytop; ty < tybot;++ty,dp += pitch) {
						unsigned char* rp = dp;
						unsigned short fbm= FontNum[ty] << txlft;
						for (int tx = txlft; tx < txrgt; ++tx, rp+=2,fbm<<=1) {
							if (fbm & 0x8000) {		// Character Foreground SET Pixel ?
								if (int(rp) & 2) {  // ODD [YUYV], Assume rp is dword aligned
									rp[0]	= c1;	// Hi Luma
									rp[-1]	= c2;	// u
									rp[1]	= c3;	// v
								} else {			// EVEN [YUYV]
									rp[0]	= c1;	// Hi Luma
									rp[1]	= c2;	// u
									rp[3]	= c3;	// v
								}
							} else {
								if (int(rp) & 2) {  // ODD [YUYV], Assume rp is dword aligned
									DD_BGF_Y(rp[0]);					// fade background
									DD_BGF_C(rp[-1]);
									DD_BGF_C(rp[1]);
								} else {								// EVEN [YUYV]
									DD_BGF_Y(rp[0]);					// fade background
									DD_BGF_C(rp[1]);
									DD_BGF_C(rp[3]);
								}
							}
						}
					}
				#endif // DDIGIT_ENABLE_SUPPORT_YUY2
				} else if (RGB) {
				#ifdef DDIGIT_ENABLE_SUPPORT_RGB
					unsigned char *dp= Wptr + xx*step + (height-1 - yy)*pitch;	// height-1 FIX

					for (int ty = tytop; ty < tybot; ++ty, dp -= pitch) {
						unsigned char* rp = dp;
						unsigned short fbm= FontNum[ty] << txlft;

						for (int tx = txlft; tx < txrgt; ++tx, rp+=step, fbm <<= 1) {
							if (fbm & 0x8000U) {	// Character Foreground SET Pixel ?
								rp[0] = c3;	// B
								rp[1] = c2; // G
								rp[2] = c1; // R
							} else {
								DD_BGF_RGB(rp[0]);
								DD_BGF_RGB(rp[1]);
								DD_BGF_RGB(rp[2]);
							}
						}
					}
				#endif // DDIGIT_ENABLE_SUPPORT_RGB
				}
			}
			x+=CharStepX;
			y+=CharStepY;
		} // End for
	}

	#ifdef DDIGIT_INCLUDE_DRAWSTRING_FUNCTIONS
		// Draw Strings at pixel Coords
		void __stdcall DrawString(const VideoInfo &vi,PVideoFrame &dst,int x,int y,int color,const char *s) {
			DDigitS(vi,dst, x, y, color ,true,false, s);// color is an index
		}

		void __stdcall DrawString(const VideoInfo &vi,PVideoFrame &dst, int x, int y,const char *s) {
			DDigitS(vi,dst,x,y,DDIGIT_DEFAULT,true,false,s); // No color, white
		}

		void __stdcall DrawString(const VideoInfo &vi,PVideoFrame &dst, int x, int y, bool hilite,const char *s) {
			DDigitS(vi,dst,x,y, (!hilite)?DDIGIT_DEFAULT:DDIGIT_HILITE,true,false,s); // Hi-liting
		}
	#endif // DDIGIT_INCLUDE_DRAWSTRING_FUNCTIONS

	#ifdef DDIGIT_INCLUDE_DRAWSTRING_VERTICAL
		// Draw Vertical Strings at pixel Coords
		void __stdcall DrawStringV(const VideoInfo &vi,PVideoFrame &dst,int x,int y,int color,const char *s) {
			DDigitS(vi,dst, x, y, color ,true,true, s);// color is an index
		}

		void __stdcall DrawStringV(const VideoInfo &vi,PVideoFrame &dst, int x, int y,const char *s) {
			DDigitS(vi,dst,x,y,DDIGIT_DEFAULT,true,true,s); // No color, white
		}

		void __stdcall DrawStringV(const VideoInfo &vi,PVideoFrame &dst, int x, int y, bool hilite,const char *s) {
			DDigitS(vi,dst,x,y, (!hilite)?DDIGIT_DEFAULT:DDIGIT_HILITE,true,true,s); // Hi-liting
		}
	#endif // DDIGIT_INCLUDE_DRAWSTRING_VERTICAL


	#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
		// Draw Strings at Character Coords
		void __stdcall DrawStr(const VideoInfo &vi,PVideoFrame &dst, int x, int y, int color, const char *s) {
			DDigitS(vi,dst, x, y, color ,false,false, s);// color is an index
		}

		void __stdcall DrawStr(const VideoInfo &vi,PVideoFrame &dst, int x, int y, const char *s) {
			DDigitS(vi,dst, x, y,DDIGIT_DEFAULT,false,false,s); // No color, white
		}

		void __stdcall DrawStr(const VideoInfo &vi,PVideoFrame &dst, int x, int y, bool hilite, const char *s) {
			DDigitS(vi,dst, x, y,(!hilite)?DDIGIT_DEFAULT:DDIGIT_HILITE,false,false,s); // Hi-liting
		}
	#endif		// DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS

	#ifdef DDIGIT_INCLUDE_DRAWSTR_VERTICAL
		// Draw Vertical Strings at Character Coords
		void __stdcall DrawStrV(const VideoInfo &vi,PVideoFrame &dst, int x, int y, int color, const char *s) {
			DDigitS(vi,dst, x, y, color ,false,true, s);// color is an index
		}

		void __stdcall DrawStrV(const VideoInfo &vi,PVideoFrame &dst, int x, int y, const char *s) {
			DDigitS(vi,dst, x , y,DDIGIT_DEFAULT,false,true,s);// No color, white
		}

		void __stdcall DrawStrV(const VideoInfo &vi,PVideoFrame &dst,int x,int y,bool hilite,const char *s) {
			DDigitS(vi,dst,x,y,(!hilite)?DDIGIT_DEFAULT:DDIGIT_HILITE,false,true,s); // Hi-liting
		}
	#endif	// DDIGIT_INCLUDE_DRAWSTR_VERTICAL
#endif // DDIGIT_ENABLE_MONOLITHIC_CODE

// ---------------------------------------------------------------------------------

#ifdef DDIGIT_ENABLE_DISCRETE_COLORSPACE_CODE

	// ----------------------------------------------------
	// ----------------------------------------------------
	// -------  DISCRETE, COLORSPACE DEPENDANT   ----------
	// ----------------------------------------------------
	// ----------------------------------------------------

	#ifdef DDIGIT_ENABLE_SUPPORT_PLANAR
		void DrawStringPlanar(PVideoFrame &dst, int x, int y, int color, const char *s, bool ctrl,int length)
		{
			int cmapix=0;								// color cmap, 1 = luma defaults

			int xSubS,ySubS,pitchUV;
			unsigned char *WptrU,*WptrV;
			unsigned int fontmask;
			unsigned char* Wptr			= dst->GetWritePtr();
			const int pitch		= dst->GetPitch();
			const int height	= dst->GetHeight();
			const int rowsize	= dst->GetRowSize();
			const int rowsizeUV = dst->GetRowSize(PLANAR_U);

			if(rowsizeUV) {
				xSubS = rowsize / rowsizeUV;
				fontmask = (0xFFFF0000 >> xSubS) &0xFFFF;
				ySubS = height / dst->GetHeight(PLANAR_U);
				pitchUV = dst->GetPitch(PLANAR_U);
				WptrU = dst->GetWritePtr(PLANAR_U);
				WptrV = dst->GetWritePtr(PLANAR_V);
				#ifdef DDIGIT_SKIP_YV411_CHROMA
					if(DDIGIT_CHAR_WIDTH % xSubS !=0 || DDIGIT_CHAR_HEIGHT % ySubS !=0)
						cmapix=1;	// YV411 mono cmap defaults
				#endif
			} else {
				cmapix=1;		// Y8 mono cmap defaults
			}

			if(color>=DDIGIT_CMAP_NELS || color<DDIGIT_HILITE)
				color=DDIGIT_DEFAULT;					// Ensure legal

			if(color == DDIGIT_DEFAULT)
				color=CMapdef[cmapix][0];				//	Default
			else if(color == DDIGIT_HILITE)
				color=CMapdef[cmapix][1];				//	Hilite

			const int in_x=x;

			int c1,c2,c3;

			c1=DDigit_Cmap_YUV[color];

			c3=c1 & 0xFF;	c1>>=8U;
			c2=c1 & 0xFF;	c1>>=8U;
			c1 &= 0xFF;

			if(length<=0) {
				const char *fnd=s;
				for(;*fnd;++fnd);
				length=fnd-s;			// nul found, shorten length
			}

			int num;
			for (int s_ix=0;s_ix<length  ;++s_ix) {
				num=((unsigned char*)s)[s_ix];			// Cast to speed up ctrl code  detect

				#ifdef DDIGIT_ENABLE_SUPPORT_CONTROL_CODES
					if(num <= '\r' && num >= '\a' && (ctrl)) { // Control Character ?
						if(num == '\a') {				// Color control character ?
							int cc=s[s_ix+1];
							if(cc >= 'a' && cc <= 'v')
								cc -= ('a' - 'A');				// Upper Case
							char *colstr="0123456789ABCDEFGHIJKLMNOPQRSTUV-!*";
							char * ccp=colstr;
							while(*ccp && *ccp != cc)
								++ccp;
							if(*ccp) {
								++s_ix; // Color control code parsed with valid arg, skip ctrl code.('\a')
								cc=(ccp - colstr);
								if(cc>31) {
									if(cc==32) {							// '-' = ddigit default color
										cc = CMapdef[cmapix][0];			//	Default
									} else if(cc==33) {						// '!' = ddigit hilite
										cc = CMapdef[cmapix][1];			//	Hilite
									} else if(cc==34) {						// '*' = Original starting color
										cc=color;
									}
								}

								c1=DDigit_Cmap_YUV[cc];
								c3=c1 & 0xFF;	c1>>=8U;
								c2=c1 & 0xFF;	c1>>=8U;
								c1 &= 0xFF;
								continue;
							}
						} else  {
							switch(num) {
							case '\n':						// NewLine
								x = 0;						// Screen left
								y += DDIGIT_CHAR_HEIGHT;	// Next line down
								continue;
							case '\r':						// 1 line down and back to original X position
								x = in_x;					// Below starting X input postion
								y += DDIGIT_CHAR_HEIGHT;	// Next line down
								continue;
							case '\t':						// Horizontal Tab
								x+= ((DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)-(x%(DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)));
								continue;
							case '\f':						// Forward Space
								x += DDIGIT_CHAR_WIDTH;		// 1 char right
								continue;
							case '\b':						// BackSpace
								x -= DDIGIT_CHAR_WIDTH;		// 1 char left
								continue;
							}
						}
					}
				#endif // DDIGIT_ENABLE_SUPPORT_CONTROL_CODES


				if(x< rowsize && x > -DDIGIT_CHAR_WIDTH && y < height && y > -DDIGIT_CHAR_HEIGHT) {
					int xx = x, yy = y;		// MUST NOT ALTER x,y here, needed again later.
					int txlft = 0, txrgt = DDIGIT_CHAR_WIDTH;
					if(xx < 0)					{txlft = -xx;	xx = 0;}
					else if(xx + DDIGIT_CHAR_WIDTH > rowsize)	{txrgt = rowsize - xx;}
					int tytop = 0, tybot = DDIGIT_CHAR_HEIGHT;
					if(yy < 0)					{tytop = -yy; yy = 0;}
					else if(yy + DDIGIT_CHAR_HEIGHT > height)	{tybot = height - yy;}
					unsigned char* dp = Wptr + xx + yy*pitch;

					num= (num-' ') & 0xFF;
					if (num  >= DDIGIT_CHARACTERS)	// STRICT, Index in font ?
						num = 0;					// Convert to SPACE
					const unsigned short * FontNum=&DDigitFont[num][0];

					for (int tyy = tytop; tyy < tybot; ++tyy,  dp += pitch) {
						unsigned char * rp = dp;
						unsigned int fbm=FontNum[tyy]<<txlft;
						for (int tx = txlft; tx < txrgt; ++tx, ++rp, fbm<<=1) {
							if (fbm & 0x8000) {	// Character Foreground SET Pixel ?
								rp[0]  = c1;
							} else {
								DD_BGF_Y(rp[0]);
							}
						}
					}
					if(rowsizeUV) { // Not Y8 ?
						unsigned int rshift=xx & (xSubS-1);
						unsigned int dshift=yy & (ySubS-1);
						unsigned int yStep = ySubS-dshift;	// 1st step, thereafter ySubS

						const int SubSoff=(xx/xSubS) + ((yy/ySubS) * pitchUV);

						unsigned char * rpU =WptrU + SubSoff;
						unsigned char * rpV =WptrV + SubSoff;

						#ifdef DDIGIT_SKIP_YV411_CHROMA
							if(DDIGIT_CHAR_WIDTH % xSubS !=0 || DDIGIT_CHAR_HEIGHT % ySubS !=0) {
								// YV411 ... DO NOTHING
							} else {
						#endif
								for (int ty=tytop;ty<tybot;rpV+=pitchUV,rpU+=pitchUV,ty+=yStep,yStep=ySubS) {
									unsigned char * rpu =rpU;
									unsigned char * rpv =rpV;
									unsigned int fbm=0;

									// Read font data with dshift
									int me=ty+yStep;
									if(me>tybot)
										me=tybot;	// DONT read into next char font data, OR off screen
									for(int m=ty;m<me;++m)
										fbm |= FontNum[m];

									fbm<<=txlft;
									fbm>>=rshift;	// Fix X Chroma shift
									for (int tx = txlft; tx < txrgt; tx+=xSubS, ++rpu, ++rpv, fbm<<=xSubS) {

										if(fbm & fontmask) {
											rpu[0] = c2;
											rpv[0] = c3;
										} else {
											DD_BGF_C(rpu[0]);				// fade background
											DD_BGF_C(rpv[0]);
										}
									}
								}
						#ifdef DDIGIT_SKIP_YV411_CHROMA
							}
						#endif

					}
				}
				x+= DDIGIT_CHAR_WIDTH;
			}
		}

		void DrawStringPlanar(PVideoFrame &dst, int x, int y, bool hilite, const char *s)
		{
			DrawStringPlanar(dst, x, y,(hilite)?DDIGIT_HILITE:DDIGIT_DEFAULT, s);
		}

		void DrawStringPlanar(PVideoFrame &dst, int x, int y, const char *s)
		{
			DrawStringPlanar(dst, x, y, DDIGIT_DEFAULT, s);
		}

		#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS

			void DrawStrPlanar(PVideoFrame &dst, int x, int y, int color, const char *s)
			{
				DrawStringPlanar(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT,color,s);
			}

			void DrawStrPlanar(PVideoFrame &dst, int x, int y, bool hilite, const char *s)
			{
				DrawStringPlanar(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT,(hilite)?DDIGIT_HILITE:DDIGIT_DEFAULT,s);
			}

			void DrawStrPlanar(PVideoFrame &dst, int x, int y, const char *s)
			{
				DrawStringPlanar(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT,DDIGIT_DEFAULT,s);
			}

		#endif // DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
	#endif // DDIGIT_ENABLE_SUPPORT_PLANAR

	#ifdef DDIGIT_ENABLE_SUPPORT_YUY2

		void DrawStringYUY2(PVideoFrame &dst, int x, int y, int color, const char *s,bool ctrl,int length)
		{
			int cmapix=0;								// color cmap, 1 = luma defaults

			const int height	= dst->GetHeight();
			const int wid		= dst->GetRowSize() / 2;
			const int pitch		= dst->GetPitch();
			unsigned char * Wptr =dst->GetWritePtr();

			if(color>=DDIGIT_CMAP_NELS || color<DDIGIT_HILITE)
				color=DDIGIT_DEFAULT;					// Ensure legal

			if(color == DDIGIT_DEFAULT)
				color=CMapdef[cmapix][0];				//	Default
			else if(color == DDIGIT_HILITE)
				color=CMapdef[cmapix][1];				//	Hilite

			const int in_x=x;

			int c1,c2,c3;

			c1=DDigit_Cmap_YUV[color];

			c3=c1 & 0xFF;	c1>>=8U;
			c2=c1 & 0xFF;	c1>>=8U;
			c1 &= 0xFF;

			if(length<=0) {
				const char *fnd=s;
				for(;*fnd;++fnd);
				length=fnd-s;			// nul found, shorten length
			}


			int num;

			for (int s_ix=0;s_ix<length  ;++s_ix) {
				num=((unsigned char*)s)[s_ix];			// Cast to speed up ctrl code  detect

				#ifdef DDIGIT_ENABLE_SUPPORT_CONTROL_CODES
					if(num <= '\r' && num >= '\a' && (ctrl)) { // Control Character ?
						if(num == '\a') {				// Color control character ?
							int cc=s[s_ix+1];
							if(cc >= 'a' && cc <= 'v')
								cc -= ('a' - 'A');				// Upper Case
							char *colstr="0123456789ABCDEFGHIJKLMNOPQRSTUV-!*";
							char * ccp=colstr;
							while(*ccp && *ccp != cc)
								++ccp;
							if(*ccp) {
								++s_ix; // Color control code parsed with valid arg, skip ctrl code.('\a')
								cc=(ccp - colstr);
								if(cc>31) {
									if(cc==32) {							// '-' = ddigit default color
										cc = CMapdef[cmapix][0];			//	Default
									} else if(cc==33) {						// '!' = ddigit hilite
										cc = CMapdef[cmapix][1];			//	Hilite
									} else if(cc==34) {						// '*' = Original starting color
										cc=color;
									}
								}

								c1=DDigit_Cmap_YUV[cc];
								c3=c1 & 0xFF;	c1>>=8U;
								c2=c1 & 0xFF;	c1>>=8U;
								c1 &= 0xFF;
								continue;
							}
						} else {
							switch(num) {
							case '\n':						// NewLine
								x = 0;						// Screen left
								y += DDIGIT_CHAR_HEIGHT;	// Next line down
								continue;
							case '\r':						// 1 line down and back to original X position
								x = in_x;					// Below starting X input postion
								y += DDIGIT_CHAR_HEIGHT;	// Next line down
								continue;
							case '\t':						// Horizontal Tab
								x+= ((DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)-(x%(DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)));
								continue;
							case '\f':						// Forward Space
								x += DDIGIT_CHAR_WIDTH;		// 1 char right
								continue;
							case '\b':						// BackSpace
								x -= DDIGIT_CHAR_WIDTH;		// 1 char left
								continue;
							}
						}
					}
				#endif // DDIGIT_ENABLE_SUPPORT_CONTROL_CODES

				if(x< wid && x > -DDIGIT_CHAR_WIDTH && y < height && y > -DDIGIT_CHAR_HEIGHT) {
					int xx = x, yy = y;		// MUST NOT ALTER x,y here, needed again later.
					int txlft = 0, txrgt = DDIGIT_CHAR_WIDTH;
					if(xx < 0)					{txlft = -xx;	xx = 0;}
					else if(xx + DDIGIT_CHAR_WIDTH > wid)		{txrgt = wid - xx;}
					int tytop = 0, tybot = DDIGIT_CHAR_HEIGHT;
					if(yy < 0)					{tytop = -yy; yy = 0;}
					else if(yy + DDIGIT_CHAR_HEIGHT > height)	{tybot = height - yy;}
					unsigned char* dp = Wptr + xx*2 + yy*pitch;

					num= (num-' ') & 0xFF;
					if (num  >= DDIGIT_CHARACTERS)	// STRICT, Index in font ?
						num = 0;					// Convert to SPACE
					const unsigned short * FontNum=&DDigitFont[num][0];

					for (int tyy = tytop; tyy < tybot; ++tyy, dp += pitch) {
						unsigned char * rp = dp;
						unsigned int fbm=FontNum[tyy]<<txlft;
						for (int tx = txlft; tx < txrgt; ++tx, rp+=2, fbm<<=1) {
							if (fbm & 0x8000U) {	// Character Foreground SET Pixel ?
								if (int(rp) & 2) {  // ODD [YUYV], Assume rp is dword aligned
									rp[0]  = c1;
									rp[-1] = c2;
									rp[1]  = c3;
								} else {
									rp[0]  = c1;
									rp[1]  = c2;
									rp[3]  = c3;
								}
							} else {
								if (int(rp) & 2) {  // ODD [YUYV], Assume rp is dword aligned
									DD_BGF_Y(rp[0]);			// fade background
									DD_BGF_C(rp[-1]);
									DD_BGF_C(rp[1]);
								} else {
									DD_BGF_Y(rp[0]);			// fade background
									DD_BGF_C(rp[1]);
									DD_BGF_C(rp[3]);
								}
							}
						}
					}
				}
				x+=DDIGIT_CHAR_WIDTH;
			}
		}

		void DrawStringYUY2(PVideoFrame &dst, int x, int y,bool hilite, const char *s)
		{
			DrawStringYUY2(dst, x, y, (hilite)?DDIGIT_HILITE:DDIGIT_DEFAULT, s);
		}

		void DrawStringYUY2(PVideoFrame &dst, int x, int y, const char *s)
		{
			DrawStringYUY2(dst, x, y, DDIGIT_DEFAULT, s);
		}

		#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
			void DrawStrYUY2(PVideoFrame &dst, int x, int y, int color, const char *s)
			{
				DrawStringYUY2(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT,color, s);
			}

			void DrawStrYUY2(PVideoFrame &dst, int x, int y, bool hilite, const char *s)
			{
				DrawStringYUY2(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT,(hilite)?DDIGIT_HILITE:DDIGIT_DEFAULT, s);
			}

			void DrawStrYUY2(PVideoFrame &dst, int x, int y, const char *s)
			{
				DrawStringYUY2(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT, DDIGIT_DEFAULT, s);
			}

		#endif // DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS

	#endif // DDIGIT_ENABLE_SUPPORT_YUY2

	#ifdef DDIGIT_ENABLE_SUPPORT_RGB32

		void DrawStringRGB32(PVideoFrame &dst, int x, int y, int color, const char *s,bool ctrl,int length)
		{
			const int height = dst->GetHeight();
			const int wid = dst->GetRowSize() / 4;
			const int pitch = dst->GetPitch();
			unsigned char * Wptr =dst->GetWritePtr();

			int cmapix=0;								// color cmap, 1 = luma defaults

			if(color>=DDIGIT_CMAP_NELS || color<DDIGIT_HILITE)
				color=DDIGIT_DEFAULT;					// Ensure legal

			if(color == DDIGIT_DEFAULT)
				color=CMapdef[cmapix][0];				//	Default
			else if(color == DDIGIT_HILITE)
				color=CMapdef[cmapix][1];				//	Hilite

			const int in_x=x;

			int c1,c2,c3;

			c1=DDigit_Cmap_RGB[color];

			c3=c1 & 0xFF;	c1>>=8U;
			c2=c1 & 0xFF;	c1>>=8U;
			c1 &= 0xFF;

			if(length<=0) {
				const char *fnd=s;
				for(;*fnd;++fnd);
				length=int(fnd-s);			// nul found, shorten length
			}


			int num;

			for (int s_ix=0;s_ix<length  ;++s_ix) {
				num=((unsigned char*)s)[s_ix];			// Cast to speed up ctrl code  detect

				#ifdef DDIGIT_ENABLE_SUPPORT_CONTROL_CODES
					if(num <= '\r' && num >= '\a' && (ctrl)) { // Control Character ?
						if(num == '\a') {				// Color control character ?
							int cc=s[s_ix+1];
							if(cc >= 'a' && cc <= 'v')
								cc -= ('a' - 'A');				// Upper Case
							const char *colstr="0123456789ABCDEFGHIJKLMNOPQRSTUV-!*";
							const char * ccp=colstr;
							while(*ccp && *ccp != cc)
								++ccp;
							if(*ccp) {
								++s_ix; // Color control code parsed with valid arg, skip ctrl code.('\a')
								cc=int(ccp - colstr);
								if(cc>31) {
									if(cc==32) {							// '-' = ddigit default color
										cc = CMapdef[cmapix][0];			//	Default
									} else if(cc==33) {						// '!' = ddigit hilite
										cc = CMapdef[cmapix][1];			//	Hilite
									} else if(cc==34) {						// '*' = Original starting color
										cc=color;
									}
								}

								c1=DDigit_Cmap_RGB[cc];
								c3=c1 & 0xFF;	c1>>=8U;
								c2=c1 & 0xFF;	c1>>=8U;
								c1 &= 0xFF;
								continue;
							}
						} else {
							switch(num) {
							case '\n':						// NewLine
								x = 0;						// Screen left
								y += DDIGIT_CHAR_HEIGHT;	// Next line down
								continue;
							case '\r':						// 1 line down and back to original X position
								x = in_x;					// Below starting X input postion
								y += DDIGIT_CHAR_HEIGHT;	// Next line down
								continue;
							case '\t':						// Horizontal Tab
								x+= ((DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)-(x%(DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)));
								continue;
							case '\f':						// Forward Space
								x += DDIGIT_CHAR_WIDTH;		// 1 char right
								continue;
							case '\b':						// BackSpace
								x -= DDIGIT_CHAR_WIDTH;		// 1 char left
								continue;
							}
						}
					}
				#endif // DDIGIT_ENABLE_SUPPORT_CONTROL_CODES

				if(x< wid && x > -DDIGIT_CHAR_WIDTH && y < height && y > -DDIGIT_CHAR_HEIGHT) {
					int xx = x, yy = y;		// MUST NOT ALTER x,y here, needed again later.
					int txlft = 0, txrgt = DDIGIT_CHAR_WIDTH;
					if(xx < 0)					{txlft = -xx; xx = 0;}
					else if(xx + DDIGIT_CHAR_WIDTH > wid)		{txrgt = wid - xx;}
					int tytop = 0, tybot = DDIGIT_CHAR_HEIGHT;
					if(yy < 0)						{tytop = -yy; yy =0;}
					else if(yy + DDIGIT_CHAR_HEIGHT > height)		{tybot = height - yy;}

					unsigned char* dp = Wptr + xx*4 + (height-1 - yy)*pitch;

					num= (num-' ') & 0xFF;
					if (num  >= DDIGIT_CHARACTERS)	// STRICT, Index in font ?
						num = 0;					// Convert to SPACE
					const unsigned short * FontNum=&DDigitFont[num][0];

					for (int tyy = tytop; tyy < tybot; ++tyy, dp -= pitch) {
						unsigned char * rp = dp;
						unsigned int fbm=FontNum[tyy]<<txlft;
						for (int tx = txlft; tx < txrgt; ++tx, rp+=4, fbm<<=1) {
							if (fbm & 0x8000U) {	// Character Foreground SET Pixel ?
								rp[0] = c3; // B
								rp[1] = c2; // G
								rp[2] = c1; // R

							} else {
								DD_BGF_RGB(rp[0]);			// fade background
								DD_BGF_RGB(rp[1]);
								DD_BGF_RGB(rp[2]);
							}
						}
					}
				}
				x+=DDIGIT_CHAR_WIDTH;
			}
		}

		void DrawStringRGB32(PVideoFrame &dst, int x, int y,bool hilite, const char *s)
		{
			DrawStringRGB32(dst, x, y,(hilite)?DDIGIT_HILITE:DDIGIT_DEFAULT, s);
		}

		void DrawStringRGB32(PVideoFrame &dst, int x, int y, const char *s)
		{
			DrawStringRGB32(dst, x, y, DDIGIT_DEFAULT, s);
		}

		#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS

			void DrawStrRGB32(PVideoFrame &dst, int x, int y, int color, const char *s)
			{
				DrawStringRGB32(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT,color, s);
			}

			void DrawStrRGB32(PVideoFrame &dst, int x, int y, bool hilite, const char *s)
			{
				DrawStringRGB32(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT,(hilite)?DDIGIT_HILITE:DDIGIT_DEFAULT, s);
			}

			void DrawStrRGB32(PVideoFrame &dst, int x, int y, const char *s)
			{
				DrawStringRGB32(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT, DDIGIT_DEFAULT, s);
			}

		#endif // DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS

	#endif // DDIGIT_ENABLE_SUPPORT_RGB32

	#ifdef DDIGIT_ENABLE_SUPPORT_RGB24

		void DrawStringRGB24(PVideoFrame &dst, int x, int y, int color, const char *s,bool ctrl,int length)
		{
			const int height = dst->GetHeight();
			const int wid = dst->GetRowSize() / 3;
			const int pitch = dst->GetPitch();
			unsigned char * Wptr =dst->GetWritePtr();

			int cmapix=0;								// color cmap, 1 = luma defaults

			if(color>=DDIGIT_CMAP_NELS || color<DDIGIT_HILITE)
				color=DDIGIT_DEFAULT;					// Ensure legal

			if(color == DDIGIT_DEFAULT)
				color=CMapdef[cmapix][0];				//	Default
			else if(color == DDIGIT_HILITE)
				color=CMapdef[cmapix][1];				//	Hilite

			const int in_x=x;

			int c1,c2,c3;

			c1=DDigit_Cmap_RGB[color];

			c3=c1 & 0xFF;	c1>>=8U;
			c2=c1 & 0xFF;	c1>>=8U;
			c1 &= 0xFF;

			if(length<=0) {
				const char *fnd=s;
				for(;*fnd;++fnd);
				length=int(fnd-s);			// nul found, shorten length
			}


			int num;

			for (int s_ix=0;s_ix<length  ;++s_ix) {
				num=((unsigned char*)s)[s_ix];			// Cast to speed up ctrl code  detect
				#ifdef DDIGIT_ENABLE_SUPPORT_CONTROL_CODES
					if(num <= '\r' && num >= '\a' && (ctrl)) { // Control Character ?
						if(num == '\a') {				// Color control character ?
							int cc=s[s_ix+1];
							if(cc >= 'a' && cc <= 'v')
								cc -= ('a' - 'A');				// Upper Case
							const char *colstr="0123456789ABCDEFGHIJKLMNOPQRSTUV-!*";
							const char * ccp=colstr;
							while(*ccp && *ccp != cc)
								++ccp;
							if(*ccp) {
								++s_ix; // Color control code parsed with valid arg, skip ctrl code.('\a')
								cc=int(ccp - colstr);
								if(cc>31) {
									if(cc==32) {							// '-' = ddigit default color
										cc = CMapdef[cmapix][0];			//	Default
									} else if(cc==33) {						// '!' = ddigit hilite
										cc = CMapdef[cmapix][1];			//	Hilite
									} else if(cc==34) {						// '*' = Original starting color
										cc=color;
									}
								}

								c1=DDigit_Cmap_RGB[cc];
								c3=c1 & 0xFF;	c1>>=8U;
								c2=c1 & 0xFF;	c1>>=8U;
								c1 &= 0xFF;
								continue;
							}
						} else {
							switch(num) {
							case '\n':						// NewLine
								x = 0;						// Screen left
								y += DDIGIT_CHAR_HEIGHT;	// Next line down
								continue;
							case '\r':						// 1 line down and back to original X position
								x = in_x;					// Below starting X input postion
								y += DDIGIT_CHAR_HEIGHT;	// Next line down
								continue;
							case '\t':						// Horizontal Tab
								x+= ((DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)-(x%(DDIGIT_TABCNT*DDIGIT_CHAR_WIDTH)));
								continue;
							case '\f':						// Forward Space
								x += DDIGIT_CHAR_WIDTH;		// 1 char right
								continue;
							case '\b':						// BackSpace
								x -= DDIGIT_CHAR_WIDTH;		// 1 char left
								continue;
							}
						}
					}
				#endif // DDIGIT_ENABLE_SUPPORT_CONTROL_CODES


				if(x< wid && x > -DDIGIT_CHAR_WIDTH && y < height && y > -DDIGIT_CHAR_HEIGHT) {
					int xx = x, yy = y;		// MUST NOT ALTER x,y here, needed again later.
					int txlft = 0, txrgt = DDIGIT_CHAR_WIDTH;
					if(xx < 0)					{txlft = -xx; xx = 0;}
					else if(xx + DDIGIT_CHAR_WIDTH > wid)		{txrgt = wid - xx;}
					int tytop = 0, tybot = DDIGIT_CHAR_HEIGHT;
					if(yy < 0)						{tytop = -yy; yy =0;}
					else if(yy + DDIGIT_CHAR_HEIGHT > height)		{tybot = height - yy;}

					unsigned char* dp = Wptr + xx*3 + (height-1 - yy)*pitch;

					num= (num-' ') & 0xFF;			// EXT ASCII bug fix
					if (num  >= DDIGIT_CHARACTERS)	// STRICT, Index in font ?
						num = 0;					// Convert to SPACE
					const unsigned short * FontNum=&DDigitFont[num][0];

					for (int tyy = tytop; tyy < tybot; ++tyy, dp -= pitch) {
						unsigned char * rp = dp;
						unsigned int fbm=FontNum[tyy]<<txlft;
						for (int tx = txlft; tx < txrgt;  ++tx, rp+=3, fbm<<=1) {
							if (fbm & 0x8000U) {	// Character Foreground SET Pixel ?
								rp[0] = c3; // B
								rp[1] = c2; // G
								rp[2] = c1; // R
							} else {
								DD_BGF_RGB(rp[0]);			// fade background
								DD_BGF_RGB(rp[1]);
								DD_BGF_RGB(rp[2]);
							}
						}
					}
				}
				x+=DDIGIT_CHAR_WIDTH;
			}
		}

		void DrawStringRGB24(PVideoFrame &dst, int x, int y, bool hilite, const char *s)
		{
			DrawStringRGB24(dst, x, y, (hilite)?DDIGIT_HILITE:DDIGIT_DEFAULT, s);
		}

		void DrawStringRGB24(PVideoFrame &dst, int x, int y, const char *s)
		{
			DrawStringRGB24(dst, x, y, DDIGIT_DEFAULT, s);
		}

		#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS

			void DrawStrRGB24(PVideoFrame &dst, int x, int y, int color, const char *s)
			{
				DrawStringRGB24(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT,color, s);
			}

			void DrawStrRGB24(PVideoFrame &dst, int x, int y, bool hilite, const char *s)
			{
				DrawStringRGB24(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT,(hilite)?DDIGIT_HILITE:DDIGIT_DEFAULT, s);
			}

			void DrawStrRGB24(PVideoFrame &dst, int x, int y, const char *s)
			{
				DrawStringRGB24(dst,x*DDIGIT_CHAR_WIDTH,y*DDIGIT_CHAR_HEIGHT, DDIGIT_DEFAULT, s);
			}

		#endif // DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
	#endif // DDIGIT_ENABLE_SUPPORT_RGB24

#endif // DDIGIT_ENABLE_DISCRETE_COLORSPACE_CODE

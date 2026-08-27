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

/*
	Compiling for both Avisynth v2.58 & v2.6 ProjectName under VS6, where ProjectName is the base name of the plugin.
	Create an additional project ProjectName26 and copy the
	project files into ProjectName folder. Add headers and source files to v2.6 project.
	You should have "avisynth25.h" in the ProjectName Header Files and
	"avisynth26.h" in the ProjectName26 Header Files.
	For the v2.58 project, add preprocessor definition to :-
		Menu/Project/Settings/All Configuration/C/C++, AVISYNTH_PLUGIN_25
*/


#ifndef __DDIGIT_H__
#define __DDIGIT_H__

#include "win_compat.h"
#include <avisynth/avisynth.h>


// ---------------------------------------------------------------------------------


#define DDIGIT_VERSION	"v1.06 - 10 July 2013"

//
// ---------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------
//  !!! Below lines configure DDIGIT and remove code, Edit as required !!!
//
// NOTE, DDigitTest enables BOTH MONOLITHIC & DISCRETE colorspace code, you are UNLIKELY to want this.
// NOTE, Enabling any of below, without enabling the code it affects, will do nothing.
// ---------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------
// !!! ONE (or rarely both) OF THE BELOW TWO SHOULD BE ENABLED !!!
//
// MONOLITHIC ColorSpace Independant code, Includes Horiz,Vert,pixel,char, 16 color print, ctrl codes.
//#define DDIGIT_ENABLE_MONOLITHIC_CODE			// DDigitS()
//
// DISCRETE ColorSpace code, eg DrawStringRGB24() @ pixel coords (optional, color, ctrl codes).
#define DDIGIT_ENABLE_DISCRETE_COLORSPACE_CODE		// Does not require additional DrawString stubs.
//
// ---------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------
// MONOLITHIC ONLY config [Stubs Additional to DDigitS()]
//#define DDIGIT_INCLUDE_DRAWSTRING_FUNCTIONS		// Stubs, Print Strings @ Pixel Coords
//#define DDIGIT_INCLUDE_DRAWSTRING_VERTICAL		// Stubs, Print Vertical Strings @ Pixel Coords
//#define DDIGIT_INCLUDE_DRAWSTR_VERTICAL			// Stubs, Print Vertical Strings @ Char Coords
//----
// BOTH Monolithic & DISCRETE color space config
//#define DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS		// Stubs, Print Strings @ Character Coords
//----
// ColorSpaces to enable support for (Affects BOTH MONOLITHIC & DISCRETE COLOR SPACE CODE)
//#define DDIGIT_ENABLE_SUPPORT_PLANAR		// Enable PLANAR eg YV12 Support
//#define DDIGIT_ENABLE_SUPPORT_YUY2			// Enable YUY2 Support
#define DDIGIT_ENABLE_SUPPORT_RGB32			// Enable RGB32 Support
#define DDIGIT_ENABLE_SUPPORT_RGB24			// Enable RGB24 Support
//----
// BOTH, Enable control code handling support in print strings, including color control.
#define DDIGIT_ENABLE_SUPPORT_CONTROL_CODES	// Formatting + color ctrl.
//----
// BOTH, Font, Enable full character set (incl 128 to 223), Comment out saves 4kb (ASCII 32-127 only)
//#define	DDIGIT_ENABLE_EXTENDED_ASCII		// Disable this, omits half of the font.
//
// --------------------------------------------------------------------------------
// --------------------------------------------------------------------------------
// --------------------------------------------------------------------------------
//
// ------------ Other possible configuration stuff
#define DDIGIT_TABCNT		4									// Tab step setting (4)
// Color index's used for defaults
#define DDIGIT_INDEX_TO_USE_AS_DEFAULT		DDIGIT_WHITE		// DDIGIT_WHITE
#define DDIGIT_INDEX_TO_USE_AS_HILITE		DDIGIT_ORANGE		// DDIGIT_ORANGE
// Extended Luma cmap default index's, used for Y8 AND YV411 only (if skipping YV411 chroma)
#define DDIGIT_Y_INDEX_TO_USE_AS_DEFAULT	DDIGIT_Y_C			// DDIGIT_Y_C
#define DDIGIT_Y_INDEX_TO_USE_AS_HILITE		DDIGIT_Y_F			// DDIGIT_Y_F

//----

//#define	DDIGIT_SKIP_YV411_CHROMA			// If defined, then print Luma Only for YV411

//----
// Enable ONE of Following: 0=most transparent, 4 least transparent(OPAQUE BLACK).
//
#define DDIGIT_BACKGROUNDFADE 0			    // Background fade multiplier = 7/8
//#define DDIGIT_BACKGROUNDFADE 1			// Background fade multiplier = 3/4
//#define DDIGIT_BACKGROUNDFADE 2			// Background fade multiplier = 5/8
//#define DDIGIT_BACKGROUNDFADE 3			// Background fade multiplier = 1/2
//#define DDIGIT_BACKGROUNDFADE 4			// Background OPAQUE BLACK

//
//
// ---------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------
//
// Use these in external string printing fn's just in case font changes.
#define DDIGIT_CHAR_WIDTH	10			// Font Character pixel width
#define DDIGIT_CHAR_HEIGHT	20			// Font Character pixel height
//
#ifdef DDIGIT_ENABLE_EXTENDED_ASCII
	#define DDIGIT_CHARACTERS 192		// Num Chars in Font, Incl EXTENDED ASCII
#else
	#define DDIGIT_CHARACTERS 96		// Num Chars in Font, ASCII Only [128 - 32(SPACE)]
#endif
//
// Lowest and Highest valid ASCII character CODES in font.
// Lowest code = 32 (SPACE), Highest = 127 (ASCII only) or 223 (With Extended ASCII enabled)
#define DDIGIT_CHAR_MIN	32
#define DDIGIT_CHAR_MAX	(DDIGIT_CHAR_MIN+DDIGIT_CHARACTERS-1)
//
//
// ---------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------
//
//

#define DDIGIT_CMAP_NELS		32			// FIXED 4E4, Number of available colors

//
#define	DDIGIT_DEFAULT			-1			// Default color
#define	DDIGIT_HILITE			-2			// Hilite color

// CMAP colors and names fixed PERMANENT, color index arg
#define	DDIGIT_DARKGRAY			0			// \a0
#define	DDIGIT_DODGERBLUE		1			// \a1
#define	DDIGIT_ORANGERED		2			// \a2
#define	DDIGIT_ORCHID			3			// \a3
#define	DDIGIT_LIME				4			// \a4
#define	DDIGIT_AQUAMARINE		5			// \a5
#define	DDIGIT_YELLOW			6			// \a6
#define	DDIGIT_WHITE			7			// \a7
#define	DDIGIT_SILVER			8			// \a8
#define	DDIGIT_CORNFLOWERBLUE	9			// \a9
#define	DDIGIT_ORANGE			10			// \aA
#define	DDIGIT_PLUM				11			// \aB
#define	DDIGIT_CHARTREUSE		12			// \aC
#define	DDIGIT_POWDERBLUE		13			// \aD
#define	DDIGIT_GOLD				14			// \aE
#define	DDIGIT_GAINSBORO		15			// \aF
// Extended Luma cmap
#define	DDIGIT_Y_0				16			// \aG
#define	DDIGIT_Y_1				17			// \aH
#define	DDIGIT_Y_2				18			// \aI
#define	DDIGIT_Y_3				19			// \aJ
#define	DDIGIT_Y_4				20			// \aK
#define	DDIGIT_Y_5				21			// \aL
#define	DDIGIT_Y_6				22			// \aM
#define	DDIGIT_Y_7				23			// \aN
#define	DDIGIT_Y_8				24			// \aO
#define	DDIGIT_Y_9				25			// \aP
#define	DDIGIT_Y_A				26			// \aQ
#define	DDIGIT_Y_B				27			// \aR
#define	DDIGIT_Y_C				28			// \aS
#define	DDIGIT_Y_D				29			// \aT
#define	DDIGIT_Y_E				30			// \aU
#define	DDIGIT_Y_F				31			// \aV


// Color control codes as strings.						ASCII Code of Final char
#define DDIGIT_CC_HILITE			"\a!"				// 33
#define DDIGIT_CC_DEFAULT			"\a-"				// 45
#define DDIGIT_CC_DARKGRAY			"\a0"				// 48
#define DDIGIT_CC_DODGERBLUE		"\a1"				// 49
#define DDIGIT_CC_ORANGERED			"\a2"				// 50
#define DDIGIT_CC_ORCHID			"\a3"				// 51
#define DDIGIT_CC_LIME				"\a4"				// 52
#define DDIGIT_CC_AQUAMARINE		"\a5"				// 53
#define DDIGIT_CC_YELLOW			"\a6"				// 54
#define DDIGIT_CC_WHITE				"\a7"				// 55
#define DDIGIT_CC_SILVER			"\a8"				// 56
#define DDIGIT_CC_CORNFLOWERBLUE	"\a9"				// 57
#define DDIGIT_CC_ORANGE			"\aA"				// 65
#define DDIGIT_CC_PLUM				"\aB"				// 66
#define DDIGIT_CC_CHARTREUSE		"\aC"				// 67
#define DDIGIT_CC_POWDERBLUE		"\aD"				// 68
#define DDIGIT_CC_GOLD				"\aE"				// 69
#define DDIGIT_CC_GAINSBORO			"\aF"				// 70
#define DDIGIT_CC_Y_0				"\aG"				// 71
#define DDIGIT_CC_Y_1				"\aH"				// 72
#define DDIGIT_CC_Y_2				"\aI"				// 73
#define DDIGIT_CC_Y_3				"\aJ"				// 74
#define DDIGIT_CC_Y_4				"\aK"				// 75
#define DDIGIT_CC_Y_5				"\aL"				// 76
#define DDIGIT_CC_Y_6				"\aM"				// 77
#define DDIGIT_CC_Y_7				"\aN"				// 78
#define DDIGIT_CC_Y_8				"\aO"				// 79
#define DDIGIT_CC_Y_9				"\aP"				// 80
#define DDIGIT_CC_Y_A				"\aQ"				// 81
#define DDIGIT_CC_Y_B				"\aR"				// 82
#define DDIGIT_CC_Y_C				"\aS"				// 83
#define DDIGIT_CC_Y_D				"\aT"				// 84
#define DDIGIT_CC_Y_E				"\aU"				// 85
#define DDIGIT_CC_Y_F				"\aV"				// 86

// Array of pointers to color Control Code Strings (as above)
extern const char * DIGIT_COLOR_CODES[DDIGIT_CMAP_NELS];


extern const unsigned long DDigit_Cmap_RGB[DDIGIT_CMAP_NELS];
extern const unsigned long DDigit_Cmap_YUV[DDIGIT_CMAP_NELS];

extern const unsigned short DDigitFont[DDIGIT_CHARACTERS][DDIGIT_CHAR_HEIGHT];



#ifdef DDIGIT_ENABLE_MONOLITHIC_CODE
	//
	// ---------------------------------------
	// ----------- MONOLITHIC ----------------
	// ---------------------------------------


	#if (defined DDIGIT_ENABLE_SUPPORT_RGB32 || defined DDIGIT_ENABLE_SUPPORT_RGB24)
		#define DDIGIT_ENABLE_SUPPORT_RGB			// Used by MONOLITHIC
	#endif


	// Draw EXT ASCII string at [x,y] PIXEL or CHARACTER coords with color[16] selection
	// of forground. pix==true = pixel coords: vert==true = Vertical (top down printing)
	// ctrl = false, temporarily disables control code handling, just used in testing full
	// character set printing in DDigitTest() Demo.

	extern void __stdcall DDigitS(const VideoInfo &vi,PVideoFrame &dst,int x,int y,int color, \
		const bool pix,const bool vert,const char *s,bool ctrl=true,int length=0);


	#ifdef DDIGIT_INCLUDE_DRAWSTRING_FUNCTIONS
		// Draw Strings at pixel Coords
		extern void __stdcall DrawString(const VideoInfo &vi,PVideoFrame &dst,int x,int y,int color,const char *s);
		extern void __stdcall DrawString(const VideoInfo &vi,PVideoFrame &dst,int x,int y,const char *s);
		extern void __stdcall DrawString(const VideoInfo &vi,PVideoFrame &dst,int x,int y,bool hilite,const char *s);
	#endif // DDIGIT_INCLUDE_DRAWSTRING_FUNCTIONS

	#ifdef DDIGIT_INCLUDE_DRAWSTRING_VERTICAL
		// Draw Vertical Strings at pixel Coords
		extern void __stdcall DrawStringV(const VideoInfo &vi,PVideoFrame &dst,int x,int y,int color,const char *s);
		extern void __stdcall DrawStringV(const VideoInfo &vi,PVideoFrame &dst,int x,int y,const char *s);
		extern void __stdcall DrawStringV(const VideoInfo &vi,PVideoFrame &dst,int x,int y,bool hilite,const char *s);
	#endif // DDIGIT_INCLUDE_DRAWSTRING_VERTICAL

	#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
		// Draw Strings at Character Coords
		extern void __stdcall DrawStr(const VideoInfo &vi,PVideoFrame &dst, int x, int y, int color, const char *s);
		extern void __stdcall DrawStr(const VideoInfo &vi,PVideoFrame &dst, int x, int y, const char *s);
		extern void __stdcall DrawStr(const VideoInfo &vi,PVideoFrame &dst, int x, int y, bool hilite, const char *s);
	#endif // DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS

	#ifdef DDIGIT_INCLUDE_DRAWSTR_VERTICAL
		// Draw Vertical Strings at Character Coords
		extern void __stdcall DrawStrV(const VideoInfo &vi,PVideoFrame &dst, int x, int y, int color, const char *s);
		extern void __stdcall DrawStrV(const VideoInfo &vi,PVideoFrame &dst, int x, int y, const char *s);
		extern void __stdcall DrawStrV(const VideoInfo &vi,PVideoFrame &dst, int x, int y, bool hilite, const char *s);
	#endif // DDIGIT_INCLUDE_DRAWSTR_VERTICAL
#endif // DDIGIT_ENABLE_MONOLITHIC_CODE

// -----------------------------------
// -----------------------------------
// -----------------------------------

#ifdef DDIGIT_ENABLE_DISCRETE_COLORSPACE_CODE
	//
	// ---------------------------------------
	// ----------- DISCRETE ------------------
	// ---------------------------------------
	//
	// NOTE, In below function stubs, 'ctrl' is unlikely to be of any use.
	// ctrl = false, temporarily disables control code handling, just used in testing full
	// character set printing in DDigitTest() Demo.

	#ifdef DDIGIT_ENABLE_SUPPORT_PLANAR
		void DrawStringPlanar(PVideoFrame &dst, int x, int y, int color,const char *s,bool ctrl=true,int length=0);
		void DrawStringPlanar(PVideoFrame &dst, int x, int y, bool hilite,const char *s);
		void DrawStringPlanar(PVideoFrame &dst, int x, int y, const char *s);
		#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
			void DrawStrPlanar(PVideoFrame &dst, int x, int y, int color, const char *s);
			void DrawStrPlanar(PVideoFrame &dst, int x, int y, bool hilite, const char *s);
			void DrawStrPlanar(PVideoFrame &dst, int x, int y, const char *s);
		#endif  //DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
	#endif
	#ifdef DDIGIT_ENABLE_SUPPORT_YUY2
		void DrawStringYUY2(PVideoFrame &dst, int x, int y, int color, const char *s, bool ctrl=true,int length=0);
		void DrawStringYUY2(PVideoFrame &dst, int x, int y, bool hilite, const char *s);
		void DrawStringYUY2(PVideoFrame &dst, int x, int y, const char *s);
		#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
			void DrawStrYUY2(PVideoFrame &dst, int x, int y, int color, const char *s);
			void DrawStrYUY2(PVideoFrame &dst, int x, int y, bool hilite, const char *s);
			void DrawStrYUY2(PVideoFrame &dst, int x, int y, const char *s);
		#endif  //DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
	#endif
	#ifdef DDIGIT_ENABLE_SUPPORT_RGB32
		void DrawStringRGB32(PVideoFrame &dst, int x, int y, int color, const char *s, bool ctrl=true,int length=0);
		void DrawStringRGB32(PVideoFrame &dst, int x, int y, bool hilite, const char *s);
		void DrawStringRGB32(PVideoFrame &dst, int x, int y, const char *s);
		#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
			void DrawStrRGB32(PVideoFrame &dst, int x, int y, int color, const char *s);
			void DrawStrRGB32(PVideoFrame &dst, int x, int y, bool hilite, const char *s);
			void DrawStrRGB32(PVideoFrame &dst, int x, int y, const char *s);
		#endif  //DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
	#endif
	#ifdef DDIGIT_ENABLE_SUPPORT_RGB24
		void DrawStringRGB24(PVideoFrame &dst, int x, int y, int color, const char *s, bool ctrl=true,int length=0);
		void DrawStringRGB24(PVideoFrame &dst, int x, int y, bool hilite, const char *s);
		void DrawStringRGB24(PVideoFrame &dst, int x, int y, const char *s);
		#ifdef DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
			void DrawStrRGB24(PVideoFrame &dst, int x, int y, int color, const char *s);
			void DrawStrRGB24(PVideoFrame &dst, int x, int y, bool hilite, const char *s);
			void DrawStrRGB24(PVideoFrame &dst, int x, int y, const char *s);
		#endif  //DDIGIT_INCLUDE_DRAWSTR_FUNCTIONS
	#endif
#endif // DDIGIT_ENABLE_DISCRETE_COLORSPACE_CODE

#endif //__DDIGIT_H__

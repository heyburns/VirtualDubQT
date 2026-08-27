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


#ifndef __COMPILER_H__
	#define __COMPILER_H__

    #ifdef UNICODE
        #undef UNICODE													// Avoid default wide character stuff
    #endif
    #ifdef _UNICODE
        #undef _UNICODE													// Avoid default wide character stuff
    #endif
	#ifdef MBCS
		#undef MBCS
	#endif
	#ifdef _MBCS
		#undef _MBCS
	#endif

    #define _CRT_SECURE_NO_WARNINGS

    // Compile for minimum supported system. MUST be defined before includes.
    // NEED to use SDK for updated headers, TK3 will give error messages/Warnings about Beta versions.
	#ifdef _WIN64
		#define WINVER			0x0502			// XP 64 Bit or Server 2003
		#define _WIN32_WINNT	0x0502			// XP 64 Bit or Server 2003
		#define NTDDI_VERSION	0x05020000		// Server 2003 SP0
		#define _WIN32_IE		0x0700			// 0x0700=IE 7 SP0
	#else
		#define WINVER			0x0501			// XP 32 bit
		#define _WIN32_WINNT	0x0501			// XP 32 bit
		#define NTDDI_VERSION	0x05010300		// XP SP3
		#define _WIN32_IE		0x0603			// 0x0603=IE 6 SP2
	#endif

	#ifdef BUG
		#if _MSC_VER >= 1500									// VS 2008
			// Call as eg DPRINTF("Forty Two = %d",42)          // No Semi colon
			// C99 Compiler (eg VS2008)
			#define DPRINTF(fmt, ...)   dprintf(fmt, ##__VA_ARGS__);       // No Semi colon needed
		#else
			// OLD C89 Compiler: (eg VS6)
			// Call as eg DPRINTF(("Forty Two = %d",42))        // Enclosed in double parentethis, No Semi colon
			#define   DPRINTF(x)      dprintf x;
		#endif
	#else
		#if _MSC_VER >= 1500									// VS 2008
			// C99 Compiler (eg VS2008)
			#define DPRINTF(fmt,...)							/* fmt */
		#else
			// OLD C89 Compiler: (eg VS6)
			#define DPRINTF(x)									/* x */
		#endif
	#endif

	extern int __cdecl dprintf(char* fmt, ...);

#endif // __COMPILER_H__

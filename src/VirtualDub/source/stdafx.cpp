#include <stdafx.h>

#ifdef _MSC_VER
	#pragma hdrstop
#endif

// compiler/setup checks

#if defined(_MSC_VER)
	#if _MSC_VER < 1900
		#error MSVC 19.00 (Visual Studio 2015 v14.0) compiler or newer required.
	#endif
#endif

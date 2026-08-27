#ifndef __WIN_COMPAT_H__
#define __WIN_COMPAT_H__

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <algorithm>

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int BOOL;
typedef unsigned int UINT;
typedef int64_t __int64;
typedef uint64_t __uint64;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef _snprintf
#define _snprintf snprintf
#endif

#ifndef _vsnprintf
#define _vsnprintf vsnprintf
#endif

#ifndef __stdcall
#define __stdcall
#endif

#ifndef __cdecl
#define __cdecl
#endif

#ifndef __declspec
#define __declspec(x)
#endif

#define DPRINTF(...)
#define dprintf(...) 0

#ifndef min
#define min(a,b) std::min((a),(b))
#endif

#ifndef max
#define max(a,b) std::max((a),(b))
#endif

#endif // __WIN_COMPAT_H__

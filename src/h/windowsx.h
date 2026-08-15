#ifndef WINDOWSX_H_SHIM
#define WINDOWSX_H_SHIM

#include "vdwin32_shim.h"

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

#endif

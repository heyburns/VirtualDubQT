#ifndef COMMDLG_H_SHIM
#define COMMDLG_H_SHIM

#include "vdwin32_shim.h"

typedef struct tagCHOOSECOLORA {
    DWORD lStructSize;
    HWND hwndOwner;
    HWND hInstance;
    COLORREF rgbResult;
    COLORREF *lpCustColors;
    DWORD Flags;
    LPARAM lCustData;
    LPCVOID lpfnHook;
    LPCSTR lpTemplateName;
} CHOOSECOLORA, CHOOSECOLOR, *LPCHOOSECOLORA, *LPCHOOSECOLOR;

#define CC_RGBINIT  0x00000001
#define CC_FULLOPEN 0x00000002

inline BOOL ChooseColorA(CHOOSECOLORA *lpcc) { (void)lpcc; return FALSE; }
#define ChooseColor ChooseColorA

typedef UINT_PTR (CALLBACK *LPCFHOOKPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagCHOOSEFONTW {
    DWORD lStructSize;
    HWND hwndOwner;
    HDC hDC;
    LPLOGFONTW lpLogFont;
    INT iPointSize;
    DWORD Flags;
    COLORREF rgbColors;
    LPARAM lCustData;
    LPCFHOOKPROC lpfnHook;
    LPCWSTR lpTemplateName;
    HINSTANCE hInstance;
    LPWSTR lpszStyle;
    WORD nFontType;
    INT nSizeMin;
    INT nSizeMax;
} CHOOSEFONTW, *LPCHOOSEFONTW;

inline BOOL ChooseFontW(CHOOSEFONTW *lpcf) { (void)lpcf; return FALSE; }

#endif

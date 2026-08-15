#ifndef VDWIN32_SHIM_H
#define VDWIN32_SHIM_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <algorithm>
#include <mutex>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/types.h>

#include <QWidget>
#include <QPainter>
#include <QPixmap>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QMutex>
#include <QMessageBox>





#ifndef __stdcall
#define __stdcall
#endif

#ifndef __cdecl
#define __cdecl
#endif

#ifndef __declspec
#define __declspec(x)
#endif

#ifndef _MSC_VER
#define __int64 long long
#ifndef _byteswap_ulong
#define _byteswap_ulong(x) __builtin_bswap32(x)
#endif
#ifndef _byteswap_ushort
#define _byteswap_ushort(x) __builtin_bswap16(x)
#endif
#ifndef _byteswap_uint64
#define _byteswap_uint64(x) __builtin_bswap64(x)
#endif

constexpr long long operator""i64(unsigned long long v) { return static_cast<long long>(v); }
constexpr unsigned long long operator""ui64(unsigned long long v) { return v; }
#endif

#include <vd2/system/vdtypes.h>

// Win32 Primitive Type Definitions

typedef uint32_t       DWORD;
typedef uint16_t       WORD;
typedef uint16_t       ATOM;
typedef uint8_t        BYTE;
typedef unsigned int   UINT;
typedef int            BOOL;
typedef int            INT;
typedef long           LONG;
typedef int64_t        LONGLONG;
typedef uint64_t       ULONGLONG;

typedef uintptr_t      ULONG_PTR;
typedef intptr_t       LONG_PTR;
typedef intptr_t       INT_PTR;
typedef uintptr_t      UINT_PTR;

#ifndef _snprintf
#define _snprintf snprintf
#endif

#ifndef _stricmp
#define _stricmp strcasecmp
#endif

#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif
typedef uintptr_t      WPARAM;
typedef intptr_t       LPARAM;
typedef intptr_t       LRESULT;
typedef void*          HANDLE;
typedef void*          HINSTANCE;
typedef void*          HMODULE;
typedef QWidget*       HWND;
typedef QPainter*      HDC;
typedef void*          HGDIOBJ;
typedef void*          HBITMAP;
typedef void*          HFONT;
typedef void*          HBRUSH;
typedef void*          HPEN;
typedef void*          HRGN;
typedef void*          HMENU;
typedef void*          HCURSOR;
typedef struct HICON__ *HICON;
typedef wchar_t        WCHAR;
typedef const void*    LPCVOID;
typedef struct tagLOGFONTW *LPLOGFONTW;
typedef const char*    LPCSTR;
typedef const wchar_t* LPCWSTR;
typedef char*          LPSTR;
typedef wchar_t*       LPWSTR;
typedef void*          LPVOID;
typedef void*          HTREEITEM;
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR (*DLGPROC)(HWND, UINT, WPARAM, LPARAM);

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

struct RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
};

struct POINT {
    LONG x;
    LONG y;
};

struct NMHDR {
    HWND hwndFrom;
    UINT_PTR idFrom;
    UINT code;
};
typedef NMHDR *LPNMHDR;

#define TVN_GETDISPINFOA 1
#define TVN_GETDISPINFOW 2
#define TVN_DELETEITEMA  3
#define TVN_DELETEITEMW  4
#define TVN_SELCHANGEDA  5
#define TVN_SELCHANGEDW  6
#define NM_DBLCLK        7
#define TVM_GETITEMW     8
#define TVM_SETITEMW     9
#define TVM_INSERTITEMW  10
#define TVIF_TEXT        0x0001
#define TVIF_PARAM       0x0004
#define TVE_EXPAND       0x0002
#define TCM_GETCURSEL    0x130B
#define TCM_SETCURSEL    0x130C
#define TCN_SELCHANGE    0x0010
#define LB_SETITEMDATA   0x01AA
#define LB_GETITEMDATA   0x01A9
#define LB_SETTABSTOPS   0x0192
#define LBN_SELCHANGE    1
#define LBN_DBLCLK       2
#define TVI_ROOT         ((HTREEITEM)(ULONG_PTR)-65536)
#define TVI_FIRST        ((HTREEITEM)(ULONG_PTR)-65535)
#define TVI_LAST         ((HTREEITEM)(ULONG_PTR)-65534)
#define LVNI_ALL         0
#define LVNI_SELECTED    0x0002
#define LVIS_SELECTED    0x0002
#define LVIS_STATEIMAGEMASK 0xF000
#define LVIF_TEXT        0x0001
#define LVIF_IMAGE       0x0002
#define LVIF_PARAM       0x0004
#define LVIF_STATE       0x0008
#define LVIR_BOUNDS      0
#define LVM_REDRAWITEMS  0x1015
#define LVM_GETITEMTEXTW 0x1073
#define LVM_SETITEMW     0x104C
#define LVM_GETITEMRECT  0x100E
#define LVM_GETITEMW     0x104B
#define LVM_INSERTCOLUMNW 0x1061
#define LVM_INSERTITEMW  0x104D
#define LVCF_FMT         0x0001
#define LVCF_WIDTH       0x0002
#define LVCF_TEXT        0x0004
#define LVCFMT_LEFT      0x0000
#define LVCFMT_RIGHT     0x0001
#define INDEXTOSTATEIMAGEMASK(i) ((i) << 12)
#define LVN_ITEMCHANGING 20
#define LVN_ITEMCHANGED  21
#define LVN_ENDLABELEDITA 22
#define LVN_ENDLABELEDITW 23
#define LVN_BEGINDRAG    24
#define LVN_BEGINRDRAG   25
#define NM_RCLICK        26
#define LVN_GETDISPINFOA 30
#define LVN_GETDISPINFOW 31
#define LVN_DELETEITEM   32
#define LVN_COLUMNCLICK  33
#define HDI_FORMAT       0x0004
#define HDI_TEXT         0x0001
#define HDM_GETITEMCOUNT 0x1200
#define HDM_GETITEMW     0x120B
#define HDM_SETITEMW     0x120C
#define HDF_SORTUP       0x0400
#define HDF_SORTDOWN     0x0200
#define HDF_STRING       0x4000
#define LVM_GETITEMCOUNT 0x1004
#define LVIS_FOCUSED     0x0001
#define LVS_EX_FULLROWSELECT 0x00000020
#define LVS_EX_GRIDLINES 0x00000001
#define LVS_EX_CHECKBOXES 0x00000004
#define HKM_GETHOTKEY    0x0402
#define HKM_SETHOTKEY    0x0401
#define HOTKEYF_SHIFT    0x01
#define HOTKEYF_CONTROL  0x02
#define HOTKEYF_ALT      0x04
#define HOTKEYF_EXT      0x08
#define TCIF_TEXT        0x0001
#define TCM_INSERTITEMW  0x133E
#define TCM_DELETEITEM   0x1308
#define WM_SETREDRAW     0x000B
#define LVM_SETCOLUMNWIDTH 0x101E
#define LVM_GETCOLUMNWIDTH 0x101D
#define LVM_DELETEALLITEMS 0x1009
#define LVM_DELETEITEM   0x1008
#define LVM_GETHEADER    0x101F
#define LVSCW_AUTOSIZE   -1
#define LVSCW_AUTOSIZE_USEHEADER -2

typedef int (*PFNLVCOMPARE)(LPARAM, LPARAM, LPARAM);

struct LVCOLUMNW { UINT mask; int fmt; int cx; LPWSTR pszText; int cchTextMax; int iSubItem; int iImage; int iOrder; };
struct HD_ITEMW { UINT mask; int fmt; LPWSTR pszText; int cchTextMax; };
struct TCITEMW { UINT mask; LPWSTR pszText; };
struct NMITEMACTIVATE { NMHDR hdr; int iItem; int iSubItem; POINT ptAction; };
struct NMLISTVIEW { NMHDR hdr; int iItem; int iSubItem; UINT uNewState; UINT uOldState; UINT uChanged; LPARAM lParam; };
struct LVITEMA { UINT mask; int iItem; int iSubItem; UINT state; UINT stateMask; LPSTR pszText; int cchTextMax; int iImage; LPARAM lParam; };
struct LVITEMW { UINT mask; int iItem; int iSubItem; UINT state; UINT stateMask; LPWSTR pszText; int cchTextMax; int iImage; LPARAM lParam; };
struct NMLVDISPINFOA { NMHDR hdr; LVITEMA item; };
struct NMLVDISPINFOW { NMHDR hdr; LVITEMW item; };
struct TVITEMA { UINT mask; HTREEITEM hItem; LPARAM lParam; LPSTR pszText; };
struct TVITEMW { UINT mask; HTREEITEM hItem; LPARAM lParam; LPWSTR pszText; };
struct TVINSERTSTRUCTW { HTREEITEM hParent; HTREEITEM hInsertAfter; TVITEMW item; };
struct NMTVDISPINFOA { NMHDR hdr; TVITEMA item; };
struct NMTVDISPINFOW { NMHDR hdr; TVITEMW item; };
struct NMTREEVIEWA { NMHDR hdr; TVITEMW itemOld; TVITEMW itemNew; };
struct NMTREEVIEWW { NMHDR hdr; TVITEMW itemOld; TVITEMW itemNew; };

inline BOOL IsWindow(HWND h) { return h != NULL; }
inline void ListView_DeleteColumn(HWND, int) {}
INT_PTR DialogBoxParamW(HINSTANCE, const wchar_t*, HWND, DLGPROC, LPARAM);
HWND CreateDialogParamW(HINSTANCE, const wchar_t*, HWND, DLGPROC, LPARAM);

inline INT_PTR DialogBoxParamA(HINSTANCE h, const char* t, HWND p, DLGPROC d, LPARAM l) { (void)t; return DialogBoxParamW(h, L"", p, d, l); }
inline HWND CreateDialogParamA(HINSTANCE h, const char* t, HWND p, DLGPROC d, LPARAM l) { (void)t; return CreateDialogParamW(h, L"", p, d, l); }
inline void ListView_SetExtendedListViewStyleEx(HWND, DWORD, DWORD) {}
inline HWND ListView_GetHeader(HWND) { return (HWND)0; }
inline int Header_GetItemCount(HWND) { return 0; }
inline int ListView_GetNextItem(HWND, int, UINT) { return -1; }
inline int ListView_GetTopIndex(HWND) { return 0; }
inline int ListView_GetItemCount(HWND) { return 0; }
inline void ListView_EnsureVisible(HWND, int, BOOL) {}
inline void ListView_EditLabel(HWND, int) {}
inline BOOL ListView_GetCheckState(HWND, int) { return FALSE; }
inline void ListView_SetCheckState(HWND, int, BOOL) {}
inline void ListView_SetItemState(HWND, int, UINT, UINT) {}
inline void ListView_SortItems(HWND, PFNLVCOMPARE, LPARAM) {}
inline int TabCtrl_GetItemCount(HWND) { return 0; }
inline void TabCtrl_AdjustRect(HWND, BOOL, RECT*) {}
inline HTREEITEM TreeView_GetSelection(HWND) { return NULL; }
inline void TreeView_DeleteAllItems(HWND) {}
inline void TreeView_EnsureVisible(HWND, HTREEITEM) {}
inline void TreeView_SelectItem(HWND, HTREEITEM) {}
inline void TreeView_DeleteItem(HWND, HTREEITEM) {}
inline void TreeView_Expand(HWND, HTREEITEM, UINT) {}

BOOL PostMessage(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam);

struct MENUITEMINFOA { UINT cch; };
struct MENUITEMINFOW { UINT cch; };

#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define WINAPI
#define CALLBACK
#define APIENTRY

typedef RECT* LPRECT;

struct SIZE {
    LONG cx;
    LONG cy;
};

typedef DWORD          COLORREF;
#ifndef RGB
#define RGB(r,g,b) ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#endif

#define TRANSPARENT 1
#define OPAQUE      2
#define DT_TOP             0x00000000
#define DT_LEFT            0x00000000
#define DT_CENTER          0x00000001
#define DT_RIGHT           0x00000002
#define DT_VCENTER         0x00000004
#define DT_BOTTOM          0x00000008
#define DT_WORDBREAK       0x00000010
#define DT_SINGLELINE      0x00000020
#define DT_EXPANDTABS      0x00000040
#define DT_TABSTOP         0x00000080
#define DT_NOCLIP          0x00000100

#define DEFAULT_QUALITY 0
#define ANTIALIASED_QUALITY 4
#define OUT_DEFAULT_PRECIS 0
#define OUT_TT_ONLY_PRECIS 7
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_PITCH 0
#define ANSI_CHARSET 0

inline DWORD SetMapperFlags(HDC, DWORD f) { return f; }

struct tagLOGFONTW {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    WCHAR lfFaceName[32];
};
typedef struct tagLOGFONTW LOGFONTW, *LPLOGFONTW;

inline HDC CreateCompatibleDC(HDC) { return (HDC)1; }
inline HFONT CreateFontIndirectW(const LOGFONTW*) { return (HFONT)1; }
inline HGDIOBJ SelectObject(HDC, HGDIOBJ) { return (HGDIOBJ)1; }
inline COLORREF SetTextColor(HDC, COLORREF c) { return c; }
inline COLORREF SetBkColor(HDC, COLORREF c) { return c; }
inline int SetBkMode(HDC, int m) { return m; }
inline BOOL BeginPath(HDC) { return TRUE; }
inline BOOL EndPath(HDC) { return TRUE; }
inline int GetPath(HDC, POINT*, BYTE*, int) { return 0; }
inline BOOL DeleteDC(HDC) { return TRUE; }
inline BOOL DeleteObject(HGDIOBJ) { return TRUE; }
inline int DrawTextW(HDC, const wchar_t*, int, RECT*, UINT) { return 0; }

typedef DWORD          EXECUTION_STATE;


inline int MessageBoxA(HWND hwnd, const char *lpText, const char *lpCaption, UINT uType) {
    (void)hwnd; (void)uType;
    QMessageBox::warning(nullptr, QString::fromUtf8(lpCaption ? lpCaption : ""), QString::fromUtf8(lpText ? lpText : ""));
    return 1;
}

inline int MessageBoxW(HWND hwnd, const wchar_t *lpText, const wchar_t *lpCaption, UINT uType) {
    (void)hwnd; (void)uType;
    QMessageBox::warning(nullptr, QString::fromWCharArray(lpCaption ? lpCaption : L""), QString::fromWCharArray(lpText ? lpText : L""));
    return 1;
}

inline void OutputDebugStringA(const char* s) { if (s) fputs(s, stderr); }
inline void OutputDebugStringW(const wchar_t* s) { if (s) fwprintf(stderr, L"%ls", s); }
#ifndef OutputDebugString
#define OutputDebugString OutputDebugStringA
#endif

#ifndef FORMAT_MESSAGE_FROM_SYSTEM
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define LANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define MAKELANGID(p, s) ((((uint16_t)(s)) << 10) | (uint16_t)(p))

inline uint32_t FormatMessageA(uint32_t dwFlags, const void* lpSource, uint32_t dwMessageId, uint32_t dwLanguageId, char* lpBuffer, uint32_t nSize, void* Arguments) {
    if (lpBuffer && nSize > 0) {
        snprintf(lpBuffer, nSize, "System error 0x%X (%u)", (unsigned)dwMessageId, (unsigned)dwMessageId);
    }
    return lpBuffer ? (uint32_t)strlen(lpBuffer) : 0;
}

inline uint32_t FormatMessageW(uint32_t dwFlags, const void* lpSource, uint32_t dwMessageId, uint32_t dwLanguageId, wchar_t* lpBuffer, uint32_t nSize, void* Arguments) {
    if (lpBuffer && nSize > 0) {
        swprintf(lpBuffer, nSize, L"System error 0x%X (%u)", (unsigned)dwMessageId, (unsigned)dwMessageId);
    }
    return lpBuffer ? (uint32_t)wcslen(lpBuffer) : 0;
}
#endif





typedef short SHORT;

#define WM_INITDIALOG     0x0110
#define WM_COMMAND        0x0111
#define WM_HSCROLL        0x0114
#define WM_VSCROLL        0x0115
#define WM_GETTEXTLENGTH  0x000E

inline int GetWindowTextW(HWND, wchar_t *str, int maxCount) { if (str && maxCount > 0) str[0] = 0; return 0; }
#define WM_MOUSEMOVE      0x0200
#define WM_LBUTTONUP      0x0202
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205
#define WM_MOUSEWHEEL     0x020A
#define WM_CAPTURECHANGED 0x0215
#define WM_MOUSELEAVE     0x02A3

typedef DWORD*         PDWORD;
typedef DWORD*         LPDWORD;

#define CB_ADDSTRING      0x0143
#define CB_SETCURSEL      0x014E
#define WM_CTLCOLORSTATIC 0x0138

#ifndef _ASSERT
#define _ASSERT(x) ((void)0)
#endif

inline LRESULT SendMessageA(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline UINT GetDlgItemTextA(HWND, int, char *str, int maxCount) { if (str && maxCount > 0) str[0] = 0; return 0; }
inline HWND SetFocus(HWND) { return (HWND)1; }

typedef POINT*         LPPOINT;

#define COLOR_3DFACE      15
#define COLOR_BTNFACE     15
#define BM_SETIMAGE       0x00F7
#define BM_GETCHECK       0x00F0
#define IMAGE_BITMAP      0
#define WM_CREATE         0x0001
#define WM_SIZE           0x0005
#define WM_SYSCOLORCHANGE 0x0015
#define WM_SETFONT        0x0030
#define WM_NCCREATE       0x0081
#define WM_NCDESTROY      0x0082
#define TTN_GETDISPINFOW  0
#define TTM_SETMAXTIPWIDTH 0

#define CB_RESETCONTENT   0x014B
#define TBM_SETPAGESIZE   0x0417
#define UDM_SETRANGE32    0x046F
#define WM_CLOSE          0x0010
#define WM_DESTROY        0x0002
#define WM_TIMER          0x0113
#define WM_DROPFILES      0x0233
#define WM_ERASEBKGND     0x0014
#define WM_GETMINMAXINFO  0x0024
#define WM_HELP           0x0053
#define WM_CONTEXTMENU    0x007B
#define SWP_NOCOPYBITS    0x0100

typedef void* HDWP;

struct MINMAXINFO {
    POINT ptReserved;
    POINT ptMaxSize;
    POINT ptMaxPosition;
    POINT ptMinTrackSize;
    POINT ptMaxTrackSize;
};

#define MF_ENABLED        0x00000000L
#define TPM_LEFTALIGN     0x0000L
#define TPM_TOPALIGN      0x0000L
#define TPM_HORIZONTAL    0x0000L
#define TPM_NONOTIFY      0x0080L
#define TPM_RETURNCMD     0x0100L
#define LB_RESETCONTENT   0x0184
#define LB_GETCURSEL      0x0188
#define LB_SETCURSEL      0x0186
#define LB_ADDSTRING      0x0180

struct TPMPARAMS {
    UINT cbSize;
    RECT rcExclude;
};

#define WM_USER           0x0400
#define SW_SHOWNA         8
#define SW_SHOW           5
#define HWND_TOP          ((HWND)0)
#define DM_REPOSITION     (WM_USER + 2)
#define WM_NEXTDLGCTL     0x0028
#define BST_INDETERMINATE 2

typedef struct HDROP__ *HDROP;

inline UINT DragQueryFile(HDROP, UINT, wchar_t*, UINT) { return 0; }
inline UINT DragQueryFileW(HDROP, UINT, wchar_t*, UINT) { return 0; }
inline HWND CreateDialogParamW(HINSTANCE, const wchar_t*, HWND, DLGPROC, LPARAM) { return (HWND)1; }
inline BOOL PostMessageW(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam) { return PostMessage(hwnd, Msg, wParam, lParam); }
inline HMENU CreatePopupMenu() { return (HMENU)1; }
inline UINT TrackPopupMenuEx(HMENU, UINT, int, int, HWND, const TPMPARAMS*) { return 0; }
inline BOOL DestroyWindow(HWND) { return TRUE; }
inline void DragFinish(void*) {}
inline HDWP BeginDeferWindowPos(int) { return (HDWP)1; }
inline HDWP DeferWindowPos(HDWP, HWND, HWND, int, int, int, int, UINT) { return (HDWP)1; }
inline BOOL EndDeferWindowPos(HDWP) { return TRUE; }
inline int ExcludeClipRect(HDC, int, int, int, int) { return 0; }

#define LR_LOADTRANSPARENT  0x0020
#define LR_LOADMAP3DCOLORS  0x1000
#define HALFTONE            4
#define SRCCOPY             (DWORD)0x00CC0020
#define IDC_ARROW           ((LPCWSTR)32512)

struct BITMAP {
    LONG bmType;
    LONG bmWidth;
    LONG bmHeight;
    LONG bmWidthBytes;
    WORD bmPlanes;
    WORD bmBitsPixel;
    LPVOID bmBits;
};

struct NMTTDISPINFOW {
    NMHDR hdr;
    LPWSTR lpszText;
    WCHAR szText[80];
    HINSTANCE hinst;
    UINT uFlags;
    LPARAM lParam;
};

struct WNDCLASSW {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCWSTR lpszMenuName;
    LPCWSTR lpszClassName;
};

inline HBITMAP CreateDIBSection(HDC, const void*, UINT, void**, HANDLE, DWORD) { return (HBITMAP)1; }
inline int SetStretchBltMode(HDC, int) { return 0; }
inline BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) { return TRUE; }
inline HCURSOR LoadCursorW(HINSTANCE, LPCWSTR) { return (HCURSOR)1; }
inline ATOM RegisterClassW(const WNDCLASSW*) { return 1; }
inline BOOL SetRect(RECT *r, int l, int t, int right, int b) {
    if (!r) return FALSE;
    r->left = l; r->top = t; r->right = right; r->bottom = b;
    return TRUE;
}
inline BOOL DestroyMenu(HMENU) { return TRUE; }

#define SW_HIDE           0
#define WM_PAINT          0x000F
#define DIB_RGB_COLORS    0

inline BOOL ShowWindow(HWND, int) { return TRUE; }

#define WM_LBUTTONDOWN    0x0201
#define WHITE_PEN         6
#define MB_ICONQUESTION   0x00000020L

struct tagBITMAPINFO;
inline int SetDIBitsToDevice(HDC, int, int, DWORD, DWORD, int, int, UINT, UINT, const void*, const void*, UINT) { return 0; }
inline BOOL Polyline(HDC, const POINT*, int) { return TRUE; }

#define CBN_SELCHANGE     1
#define CB_GETCURSEL      0x0147
#define BM_GETSTATE       0x00F2

#define WHEEL_DELTA       120
#define GWL_STYLE         (-16)
#define DEFAULT_GUI_FONT  17

#define TPM_RIGHTALIGN    0x0008L
#define TPM_BOTTOMALIGN   0x0020L

inline int MapWindowPoints(HWND, HWND, POINT*, UINT) { return 0; }
inline BOOL PtInRect(const RECT *r, POINT p) {
    if (!r) return FALSE;
    return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom;
}
inline HWND SetCapture(HWND) { return (HWND)1; }
inline HMENU GetSubMenu(HMENU, int) { return (HMENU)1; }
inline void GdiFlush() {}
inline BOOL ClientToScreen(HWND, POINT*) { return TRUE; }
inline BOOL TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT*) { return TRUE; }
inline BOOL SetCursorPos(int, int) { return TRUE; }
inline int ShowCursor(BOOL) { return 0; }
inline BOOL ReleaseCapture() { return TRUE; }
inline BOOL UpdateWindow(HWND) { return TRUE; }
inline LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 0; }
#define WM_DRAWITEM   0x002B

#ifndef LOWORD
#define LOWORD(l) ((WORD)(((uintptr_t)(l)) & 0xffff))
#endif
#ifndef HIWORD
#define HIWORD(l) ((WORD)((((uintptr_t)(l)) >> 16) & 0xffff))
#endif

inline LRESULT SendMessageW(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    (void)hwnd; (void)Msg; (void)wParam; (void)lParam;
    return 0;
}

#ifndef __asm
#define __asm __asm__ __volatile__
#endif

#define BN_CLICKED 0
#define EN_CHANGE  0x0300
#define EN_UPDATE  0x0400
#define EN_KILLFOCUS 0x0200
#define EN_SETFOCUS 0x0100

#define LOGPIXELSX      88
#define WS_EX_STATICEDGE 0x00020000L
#define BS_ICON         0x00000040L

struct TEXTMETRICW {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
};

inline int MulDiv(int a, int b, int c) { return (int)(((long long)a * b) / (c ? c : 1)); }
inline int GetObjectW(HGDIOBJ, int, void*) { return 0; }
inline BOOL GetTextMetricsW(HDC, TEXTMETRICW *tm) { if (tm) memset(tm, 0, sizeof(*tm)); return TRUE; }
inline BOOL GetTextExtentPoint32A(HDC, const char*, int len, SIZE *sz) { if (sz) { sz->cx = len * 8; sz->cy = 16; } return TRUE; }
inline HMENU LoadMenuW(HINSTANCE, const wchar_t*) { return (HMENU)1; }
#ifndef _RPT1
#define _RPT1(a, b, c) ((void)0)
#endif

#define WS_CHILD        0x40000000L
#define WS_VISIBLE      0x10000000L
#define WS_POPUP        0x80000000L
#define WS_EX_TOPMOST   0x00000008L
#define BS_PUSHBUTTON   0x00000000L
#define BS_AUTOCHECKBOX 0x00000003L
#define BS_PUSHLIKE     0x00001000L
#define BS_BITMAP       0x00000080L
#define IMAGE_ICON      1

inline HICON LoadImageW(HINSTANCE, const wchar_t*, UINT, int, int, UINT) { return (HICON)1; }
#define TOOLTIPS_CLASSW L"tooltips_class32"
#define TTS_NOPREFIX 0x02
#define TTS_ALWAYSTIP 0x01
#define CW_USEDEFAULT ((int)0x80000000)

#define HWND_TOPMOST ((HWND)(intptr_t)-1)
#define SWP_NOMOVE 0x0002
#define SWP_NOSIZE      0x0001
#define SWP_NOZORDER    0x0004
#define SWP_NOACTIVATE  0x0010

#define TTM_SETDELAYTIME 0x0403
#define TTM_ADDTOOLW     0x0432
#define TTDT_AUTOMATIC   0
#define TTDT_RESHOW      1
#define TTF_SUBCLASS     0x0010
#define TTF_IDISHWND     0x0001
#define LPSTR_TEXTCALLBACKW ((wchar_t*)-1)

#define NM_FIRST 0U

typedef BOOL (CALLBACK *WNDENUMPROC)(HWND, LPARAM);

struct TOOLINFOW {
    UINT cbSize;
    UINT uFlags;
    HWND hwnd;
    UINT_PTR uId;
    RECT rect;
    HINSTANCE hinst;
    LPWSTR lpszText;
    LPARAM lParam;
};

inline BOOL EnumChildWindows(HWND, WNDENUMPROC, LPARAM) { return TRUE; }
inline HWND CreateWindowExW(DWORD, const wchar_t*, const wchar_t*, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) { return (HWND)1; }

#define COLOR_WINDOWTEXT 8
#define BLACK_PEN       7

#define TA_TOP          0
#define TA_CENTER       6

inline BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
inline COLORREF GetSysColor(int) { return 0; }
inline UINT SetTextAlign(HDC, UINT f) { return f; }
inline BOOL TextOutA(HDC, int, int, const char*, int) { return TRUE; }

#define PS_SOLID 0
#define PS_NULL  5

#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH  2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH  5

#define SM_CXEDGE 45
#define SM_CYEDGE 46

#define EDGE_RAISED 0x0005
#define EDGE_SUNKEN 0x000A
#define BF_RECT     0x000F
#define BF_SOFT     0x1000
#define BF_ADJUST   0x2000

#define WM_NOTIFY   0x004E

struct PAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
};

inline HPEN CreatePen(int, int, COLORREF) { return (HPEN)1; }
inline HGDIOBJ GetStockObject(int) { return (HGDIOBJ)1; }
inline BOOL Polygon(HDC, const POINT*, int) { return TRUE; }
inline int GetSystemMetrics(int) { return 2; }
inline BOOL InflateRect(RECT *r, int dx, int dy) { if (r) { r->left -= dx; r->right += dx; r->top -= dy; r->bottom += dy; } return TRUE; }
inline BOOL DrawEdge(HDC, RECT*, UINT, UINT) { return TRUE; }
inline HDC BeginPaint(HWND, PAINTSTRUCT *ps) { if (ps) ps->hdc = (HDC)1; return (HDC)1; }
inline BOOL EndPaint(HWND, const PAINTSTRUCT*) { return TRUE; }
inline HWND GetParent(HWND parent) { return parent; }

#define MB_ICONEXCLAMATION 0x00000030L
#define RDW_INVALIDATE  0x0001
#define RDW_ERASE       0x0004
#define RDW_UPDATENOW   0x0100

struct DRAWITEMSTRUCT {
    UINT CtlType;
    UINT CtlID;
    UINT itemID;
    UINT itemAction;
    UINT itemState;
    HWND hwndItem;
    HDC hDC;
    RECT rcItem;
    ULONG_PTR itemData;
};

inline BOOL MessageBeep(UINT) { return TRUE; }
inline UINT IsDlgButtonChecked(HWND, int) { return 0; }
inline UINT GetDlgItemInt(HWND, int, BOOL*, BOOL) { return 0; }
inline BOOL RedrawWindow(HWND, const RECT*, HRGN, UINT) { return TRUE; }
inline HBRUSH CreateSolidBrush(COLORREF) { return (HBRUSH)1; }
inline int FillRect(HDC, const RECT*, HBRUSH) { return 0; }
inline HDC CreateDCW(const wchar_t*, const wchar_t*, const wchar_t*, const void*) { return (HDC)1; }

#define DWLP_USER 8
#define BST_UNCHECKED 0x0000
#define BST_CHECKED   0x0001
#define FW_NORMAL     400

#define UDM_SETRANGE  0x0465
#define LOGPIXELSY    90

#define CF_TTONLY                 0x00000040L
#define CF_INITTOLOGFONTSTRUCT    0x00000040L
#define CF_NOSCRIPTSEL            0x00800000L

inline HWND GetFocus() { return (HWND)0; }
inline BOOL SetDlgItemInt(HWND, int, UINT, BOOL) { return TRUE; }
inline BOOL CheckDlgButton(HWND, int, UINT) { return TRUE; }
inline HDC GetDC(HWND) { return (HDC)1; }
inline int ReleaseDC(HWND, HDC) { return 1; }
inline int GetDeviceCaps(HDC, int) { return 96; }

#define TBM_SETRANGE    0x0405
#define TBM_SETPOS      0x0406
#define TBM_GETPOS      0x0400
#define TBM_SETRANGEMIN 0x0407
#define TBM_SETRANGEMAX 0x0408
#define TBM_SETTICFREQ  0x0414
#define GWL_ID          (-12)
#define DWLP_MSGRESULT  0

inline LONG GetWindowLongW(HWND hwnd, int nIndex) { (void)hwnd; (void)nIndex; return 0; }
inline LONG SetWindowLongW(HWND hwnd, int nIndex, LONG dwNewLong) { (void)hwnd; (void)nIndex; return dwNewLong; }

template <size_t N>
inline int swprintf_s(wchar_t (&buffer)[N], const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    int len = vswprintf(buffer, N, format, args);
    va_end(args);
    return len;
}
#ifndef MAKELONG
#define MAKELONG(a, b) ((LONG)(((WORD)(((uintptr_t)(a)) & 0xffff)) | ((DWORD)((WORD)(((uintptr_t)(b)) & 0xffff))) << 16))
#endif
#ifndef MAKEINTRESOURCEW
#define MAKEINTRESOURCEW(i) ((wchar_t*)((uintptr_t)((WORD)(i))))
#endif

typedef INT_PTR (CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);

inline INT_PTR DialogBoxParamW(HINSTANCE, const wchar_t*, HWND, DLGPROC, LPARAM) { return 0; }
inline BOOL EndDialog(HWND, INT_PTR) { return TRUE; }
inline LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR val) { return val; }
inline LONG_PTR GetWindowLongPtrW(HWND, int) { return 0; }
inline LRESULT SendDlgItemMessageW(HWND, int, UINT, WPARAM, LPARAM) { return 0; }
inline BOOL SetDlgItemTextA(HWND, int, const char*) { return TRUE; }
inline BOOL SetDlgItemTextW(HWND, int, const wchar_t*) { return TRUE; }

inline HWND GetDlgItem(HWND parent, int id) { (void)id; return parent; }
inline BOOL EnableWindow(HWND hwnd, BOOL bEnable) { (void)hwnd; (void)bEnable; return TRUE; }
inline BOOL IsWindowEnabled(HWND hwnd) { (void)hwnd; return TRUE; }
inline BOOL SetWindowTextA(HWND hwnd, const char *text) { (void)hwnd; (void)text; return TRUE; }
inline BOOL SetWindowTextW(HWND hwnd, const wchar_t *text) { (void)hwnd; (void)text; return TRUE; }

#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define WINAPI
#define CALLBACK
#define APIENTRY

#ifndef _wcsdup
#define _wcsdup wcsdup
#endif

#ifndef _malloca
#define _malloca malloc
#endif

#ifndef _freea
#define _freea free
#endif

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif


#ifndef __noop
#define __noop ((void)0)
#endif

#define MB_OK               0x00000000L
#define MB_OKCANCEL         0x00000001L
#define MB_ABORTRETRYIGNORE 0x00000002L
#define MB_YESNOCANCEL      0x00000003L
#define MB_YESNO            0x00000004L
#define MB_RETRYCANCEL      0x00000005L
#define MB_ICONERROR        0x00000010L
#define MB_ICONWARNING      0x00000030L
#define MB_SETFOREGROUND    0x00010000L
#define MB_TASKMODAL        0x00002000L

#define IDOK                1
#define IDCANCEL            2
#define IDABORT             3
#define IDRETRY             4
#define IDIGNORE            5
#define IDYES               6
#define IDNO                7

inline int _vscprintf(const char *format, va_list argptr) {
    va_list args_copy;
    va_copy(args_copy, argptr);
    int len = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);
    return len;
}

inline int _vscwprintf(const wchar_t *format, va_list argptr) {
    va_list args_copy;
    va_copy(args_copy, argptr);
    int len = vswprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);
    return len;
}

inline int _vsnprintf_s(char *buffer, size_t sizeOfBuffer, size_t count, const char *format, va_list argptr) {
    (void)count;
    return vsnprintf(buffer, sizeOfBuffer, format, argptr);
}

inline int _vsnwprintf_s(wchar_t *buffer, size_t sizeOfBuffer, size_t count, const wchar_t *format, va_list argptr) {
    (void)count;
    return vswprintf(buffer, sizeOfBuffer, format, argptr);
}

template <size_t N>
inline int _vsnprintf_s(char (&buffer)[N], size_t count, const char *format, va_list argptr) {
    (void)count;
    return vsnprintf(buffer, N, format, argptr);
}

template <size_t N>
inline int _vsnwprintf_s(wchar_t (&buffer)[N], size_t count, const wchar_t *format, va_list argptr) {
    (void)count;
    return vswprintf(buffer, N, format, argptr);
}



#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

#define GENERIC_READ    (0x80000000L)
#define GENERIC_WRITE   (0x40000000L)
#define GENERIC_EXECUTE (0x20000000L)
#define GENERIC_ALL     (0x10000000L)

#define FILE_SHARE_READ   0x00000001
#define FILE_SHARE_WRITE  0x00000002
#define FILE_SHARE_DELETE 0x00000004

#define CREATE_NEW        1
#define CREATE_ALWAYS     2
#define OPEN_EXISTING     3
#define OPEN_ALWAYS       4
#define TRUNCATE_EXISTING 5

#define FILE_ATTRIBUTE_NORMAL      0x00000080
#define FILE_ATTRIBUTE_DIRECTORY   0x00000010
#define FILE_ATTRIBUTE_READONLY    0x00000001
#define FILE_ATTRIBUTE_HIDDEN      0x00000002
#define FILE_ATTRIBUTE_SYSTEM      0x00000004

#define FILE_FLAG_WRITE_THROUGH    0x80000000
#define FILE_FLAG_SEQUENTIAL_SCAN  0x08000000
#define FILE_FLAG_RANDOM_ACCESS    0x04000000
#define FILE_FLAG_NO_BUFFERING     0x20000000
#define FILE_FLAG_DELETE_ON_CLOSE  0x04000000

#define FILE_BEGIN   0
#define FILE_CURRENT 1
#define FILE_END     2

#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT  258
#define WAIT_FAILED   ((DWORD)0xFFFFFFFF)
#define INFINITE      0xFFFFFFFF

#define FILE_NOTIFY_CHANGE_FILE_NAME   0x00000001
#define FILE_NOTIFY_CHANGE_DIR_NAME    0x00000002
#define FILE_NOTIFY_CHANGE_ATTRIBUTES  0x00000004
#define FILE_NOTIFY_CHANGE_SIZE        0x00000008
#define FILE_NOTIFY_CHANGE_LAST_WRITE  0x00000010
#define FILE_NOTIFY_CHANGE_CREATION    0x00000040

#define PT_MOVETO      0x06
#define PT_LINETO      0x02
#define PT_BEZIERTO    0x04
#define PT_CLOSEFIGURE 0x01


typedef void* TIMERPROC;
inline uintptr_t SetTimer(HWND, uintptr_t, UINT, TIMERPROC) { return 1; }
inline BOOL KillTimer(HWND, uintptr_t) { return TRUE; }

struct BITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
};

struct RGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
};

struct BITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
};

#define BI_RGB        0L
#define BI_RLE8       1L
#define BI_RLE4       2L
#define BI_BITFIELDS  3L
#define BI_JPEG       4L
#define BI_PNG        5L

struct CRITICAL_SECTION {
    QMutex *mutex;
};

inline void InitializeCriticalSection(CRITICAL_SECTION *cs) {
    cs->mutex = new QMutex();
}

inline void DeleteCriticalSection(CRITICAL_SECTION *cs) {
    if (cs->mutex) {
        delete cs->mutex;
        cs->mutex = nullptr;
    }
}

inline void EnterCriticalSection(CRITICAL_SECTION *cs) {
    if (cs->mutex) cs->mutex->lock();
}

inline void LeaveCriticalSection(CRITICAL_SECTION *cs) {
    if (cs->mutex) cs->mutex->unlock();
}

union LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
};

struct OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        } DUMMYSTRUCTNAME;
        void* Pointer;
    } DUMMYUNIONNAME;
    DWORD Offset;
    DWORD OffsetHigh;
    HANDLE hEvent;
};

#define ERROR_IO_PENDING 997L
#define FILE_FLAG_OVERLAPPED 0x40000000
#define DUPLICATE_SAME_ACCESS 0x00000002

inline HANDLE CreateEvent(void*, BOOL, BOOL, void*) { return (HANDLE)1; }
inline BOOL CloseHandle(HANDLE) { return TRUE; }
inline BOOL CancelIo(HANDLE) { return TRUE; }
inline BOOL SetEndOfFile(HANDLE) { return TRUE; }
inline DWORD WaitForSingleObject(HANDLE, DWORD) { return WAIT_OBJECT_0; }
inline DWORD WaitForMultipleObjects(DWORD, const HANDLE*, BOOL, DWORD) { return WAIT_OBJECT_0; }
inline BOOL GetOverlappedResult(HANDLE, OVERLAPPED*, DWORD*, BOOL) { return TRUE; }
inline BOOL WriteFile(HANDLE, const void*, DWORD, DWORD *pActual, OVERLAPPED*) { if (pActual) *pActual = 0; return TRUE; }
inline BOOL GetFileSizeEx(HANDLE, LARGE_INTEGER *lp) { if (lp) lp->QuadPart = 0; return TRUE; }
inline BOOL SetFilePointerEx(HANDLE, LARGE_INTEGER lp, LARGE_INTEGER *lpOut, DWORD) { if (lpOut) lpOut->QuadPart = lp.QuadPart; return TRUE; }
inline HANDLE CreateFileW(const wchar_t*, DWORD, DWORD, void*, DWORD, DWORD, void*) { return (HANDLE)1; }
inline HANDLE CreateFileA(const char*, DWORD, DWORD, void*, DWORD, DWORD, void*) { return (HANDLE)1; }
inline HANDLE GetCurrentProcess() { return (HANDLE)1; }
inline BOOL DuplicateHandle(HANDLE, HANDLE, HANDLE, HANDLE*, DWORD, BOOL, DWORD) { return TRUE; }
inline HANDLE FindFirstChangeNotificationW(const wchar_t*, BOOL, DWORD) { return (HANDLE)1; }
inline BOOL FindNextChangeNotification(HANDLE) { return TRUE; }
inline BOOL FindCloseChangeNotification(HANDLE) { return TRUE; }
inline BOOL VDGetFileSizeW32(HANDLE, int64_t& size) { size = 0; return TRUE; }
inline BOOL VDSetFilePointerW32(HANDLE, int64_t, DWORD) { return TRUE; }

struct PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
};

struct STARTUPINFOW {
    DWORD cb;
    LPWSTR lpReserved;
    LPWSTR lpDesktop;
    LPWSTR lpTitle;
    DWORD dwX;
    DWORD dwY;
    DWORD dwXSize;
    DWORD dwYSize;
    DWORD dwXCountChars;
    DWORD dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    BYTE *lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
};

#define CREATE_NEW_PROCESS_GROUP 0x00000200
#define CREATE_DEFAULT_ERROR_MODE 0x04000000
#define STARTF_USESHOWWINDOW 0x00000001
#define SW_SHOWNORMAL 1

inline BOOL GetWindowsDirectoryW(wchar_t *buf, UINT size) {
    if (buf && size > 0) buf[0] = L'\0';
    return TRUE;
}

inline BOOL CreateProcessW(const wchar_t*, wchar_t*, void*, void*, BOOL, DWORD, void*, const wchar_t*, STARTUPINFOW*, PROCESS_INFORMATION*) {
    return TRUE;
}

inline BOOL QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
    if (lpPerformanceCount) {
        auto now = std::chrono::high_resolution_clock::now();
        lpPerformanceCount->QuadPart = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    }
    return TRUE;
}

inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency) {
    if (lpFrequency) {
        lpFrequency->QuadPart = 1000000000LL;
    }
    return TRUE;
}



inline void Sleep(DWORD dwMilliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(dwMilliseconds));
}

inline DWORD GetCurrentThreadId() {
    return (DWORD)(uintptr_t)pthread_self();
}

inline DWORD GetCurrentProcessId() {
    return (DWORD)getpid();
}

inline DWORD GetTickCount() {
    auto now = std::chrono::steady_clock::now();
    return (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}


inline DWORD GetLastError() {
    return 0;
}

inline void SetLastError(DWORD err) {
    (void)err;
}

BOOL GetClientRect(HWND hwnd, RECT *lpRect);
BOOL GetWindowRect(HWND hwnd, RECT *lpRect);
BOOL InvalidateRect(HWND hwnd, const RECT *lpRect, BOOL bErase);
LRESULT SendMessage(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL PostMessage(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam);

typedef void* HKEY;
#define HKEY_CLASSES_ROOT   ((HKEY)(uintptr_t)0x80000000)
#define HKEY_CURRENT_USER   ((HKEY)(uintptr_t)0x80000001)
#define HKEY_LOCAL_MACHINE  ((HKEY)(uintptr_t)0x80000002)

#define KEY_READ      0x00020019
#define KEY_WRITE     0x00020006
#define KEY_ALL_ACCESS 0x000F003F
#define REG_SZ        1
#define REG_BINARY    3
#define REG_DWORD     4
#define REG_OPTION_NON_VOLATILE 0

struct FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
};

inline LONG RegOpenKeyExA(HKEY, const char*, DWORD, DWORD, HKEY*) { return 1; }
inline LONG RegCreateKeyExA(HKEY, const char*, DWORD, char*, DWORD, DWORD, void*, HKEY*, DWORD*) { return 1; }
inline LONG RegCloseKey(HKEY) { return 0; }
inline LONG RegQueryValueExA(HKEY, const char*, DWORD*, DWORD*, BYTE*, DWORD*) { return 1; }
inline LONG RegQueryValueExW(HKEY, const wchar_t*, DWORD*, DWORD*, BYTE*, DWORD*) { return 1; }
inline LONG RegSetValueExA(HKEY, const char*, DWORD, DWORD, const BYTE*, DWORD) { return 0; }
inline LONG RegSetValueExW(HKEY, const wchar_t*, DWORD, DWORD, const BYTE*, DWORD) { return 0; }
inline LONG RegDeleteValueA(HKEY, const char*) { return 0; }
inline LONG RegDeleteKeyA(HKEY, const char*) { return 0; }
inline LONG RegEnumKeyExA(HKEY, DWORD, char*, DWORD*, DWORD*, char*, DWORD*, FILETIME*) { return 1; }
inline LONG RegEnumValueA(HKEY, DWORD, char*, DWORD*, DWORD*, DWORD*, BYTE*, DWORD*) { return 1; }


#define CP_ACP   0
#define CP_UTF8  65001
#define MB_ERR_INVALID_CHARS 0x00000008

inline int WideCharToMultiByte(UINT codePage, DWORD dwFlags, const wchar_t *src, int srcLen, char *dst, int dstLen, const char*, BOOL*) {
    if (!src) return 0;
    std::wstring ws;
    if (srcLen < 0) {
        ws = src;
    } else {
        ws.assign(src, srcLen);
    }
    std::string s;
    if (codePage == CP_UTF8) {
        s = QString::fromWCharArray(ws.c_str(), (int)ws.length()).toUtf8().toStdString();
    } else {
        s = QString::fromWCharArray(ws.c_str(), (int)ws.length()).toLocal8Bit().toStdString();
    }
    if (dstLen == 0) {
        return (int)s.length() + (srcLen < 0 ? 1 : 0);
    }
    int copyLen = std::min((int)s.length(), dstLen);
    memcpy(dst, s.data(), copyLen);
    if (copyLen < dstLen && srcLen < 0) {
        dst[copyLen] = '\0';
        return copyLen + 1;
    }
    return copyLen;
}

inline int MultiByteToWideChar(UINT codePage, DWORD dwFlags, const char *src, int srcLen, wchar_t *dst, int dstLen) {
    if (!src) return 0;
    std::string s;
    if (srcLen < 0) {
        s = src;
    } else {
        s.assign(src, srcLen);
    }
    QString qstr;
    if (codePage == CP_UTF8) {
        qstr = QString::fromUtf8(s.c_str(), (int)s.length());
    } else {
        qstr = QString::fromLocal8Bit(s.c_str(), (int)s.length());
    }
    if (dstLen == 0) {
        return (int)qstr.length() + (srcLen < 0 ? 1 : 0);
    }
    int copyLen = std::min((int)qstr.length(), dstLen);
    qstr.toWCharArray(dst);
    if (copyLen < dstLen && srcLen < 0) {
        dst[copyLen] = L'\0';
        return copyLen + 1;
    }
    return copyLen;
}

struct SYSTEM_INFO {
    DWORD dwPageSize;
    void *lpMinimumApplicationAddress;
    void *lpMaximumApplicationAddress;
    ULONG_PTR dwActiveProcessorMask;

    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
};

inline void GetSystemInfo(SYSTEM_INFO *si) {
    if (si) {
        memset(si, 0, sizeof(*si));
        si->dwPageSize = 4096;
        si->dwAllocationGranularity = 65536;
        si->dwNumberOfProcessors = 4;
    }
}

#define MEM_COMMIT      0x00001000
#define MEM_RESERVE     0x00002000
#define MEM_RELEASE     0x00008000

#define PAGE_NOACCESS          0x01
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04
#define PAGE_EXECUTE_READWRITE 0x40

inline void* VirtualAlloc(void* lpAddress, size_t dwSize, DWORD, DWORD) {
    if (lpAddress) return lpAddress;
    void* p = nullptr;
    if (posix_memalign(&p, 4096, dwSize) != 0) return nullptr;
    return p;
}

inline BOOL VirtualFree(void* lpAddress, size_t, DWORD) {
    free(lpAddress);
    return TRUE;
}

inline BOOL FlushInstructionCache(HANDLE, const void*, size_t) {
    return TRUE;
}

#define TIMERR_NOERROR 0
struct TIMECAPS {
    UINT wPeriodMin;
    UINT wPeriodMax;
};

inline uint32_t timeGetTime() {
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

inline UINT timeGetDevCaps(TIMECAPS *ptc, UINT cbtc) {
    if (ptc && cbtc >= sizeof(TIMECAPS)) {
        ptc->wPeriodMin = 1;
        ptc->wPeriodMax = 1000;
    }
    return TIMERR_NOERROR;
}

inline UINT timeBeginPeriod(UINT) { return TIMERR_NOERROR; }
inline UINT timeEndPeriod(UINT) { return TIMERR_NOERROR; }

#define THREAD_PRIORITY_HIGHEST 2
inline HANDLE GetCurrentThread() { return (HANDLE)2; }
inline BOOL SetThreadPriority(HANDLE, int) { return TRUE; }

#endif // VDWIN32_SHIM_H



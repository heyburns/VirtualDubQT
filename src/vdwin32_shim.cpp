#include "vdwin32_shim.h"
#include <QCoreApplication>

BOOL GetClientRect(HWND hwnd, RECT *lpRect) {
    if (!hwnd || !lpRect) return FALSE;
    QRect rect = hwnd->rect();
    lpRect->left = rect.left();
    lpRect->top = rect.top();
    lpRect->right = rect.right() + 1;
    lpRect->bottom = rect.bottom() + 1;
    return TRUE;
}

BOOL GetWindowRect(HWND hwnd, RECT *lpRect) {
    if (!hwnd || !lpRect) return FALSE;
    QRect rect = hwnd->geometry();
    lpRect->left = rect.left();
    lpRect->top = rect.top();
    lpRect->right = rect.right();
    lpRect->bottom = rect.bottom();
    return TRUE;
}

BOOL InvalidateRect(HWND hwnd, const RECT *lpRect, BOOL bErase) {
    Q_UNUSED(bErase);
    if (!hwnd) return FALSE;
    if (lpRect) {
        hwnd->update(QRect(lpRect->left, lpRect->top, lpRect->right - lpRect->left, lpRect->bottom - lpRect->top));
    } else {
        hwnd->update();
    }
    return TRUE;
}

LRESULT SendMessage(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    Q_UNUSED(hwnd);
    Q_UNUSED(Msg);
    Q_UNUSED(wParam);
    Q_UNUSED(lParam);
    return 0;
}

BOOL PostMessage(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    Q_UNUSED(hwnd);
    Q_UNUSED(Msg);
    Q_UNUSED(wParam);
    Q_UNUSED(lParam);
    return TRUE;
}

HINSTANCE g_hInst = (HINSTANCE)1;

bool guiChooseColor(QWidget*, unsigned int&) { return false; }

void VDDSPBlend8_LerpConst_SSE2(void*, void const*, void const*, unsigned int, unsigned char) {}
void VDDSPBlend8_Min_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_Max_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_Add_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_Multiply_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_LinearBurn_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_Screen_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_Overlay_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_HardLight_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_LinearLight_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_PinLight_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_HardMix_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_Difference_SSE2(void*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_Lerp_SSE2(void*, void const*, void const*, void const*, unsigned int) {}
void VDDSPBlend8_Select_SSE2(void*, void const*, void const*, void const*, unsigned int) {}

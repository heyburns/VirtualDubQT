#include "vdwin32_shim.h"
#include "DSP.h"
#include <QApplication>
#include <QColorDialog>
#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

namespace {

bool isLiveQtWidget(HWND hwnd) {
    if (!hwnd || !qobject_cast<QApplication *>(QCoreApplication::instance()))
        return false;

    // Several legacy creation shims use non-null sentinel handles. Never
    // dereference one unless Qt confirms that it is a live QWidget.
    return QApplication::allWidgets().contains(hwnd);
}

bool isSupportedPostedMessage(UINT message) {
    return message == WM_CLOSE || message == WM_SETREDRAW;
}

} // namespace

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
    Q_UNUSED(lParam);

    QCoreApplication *application = QCoreApplication::instance();
    if (!application)
        return 0;

    if (QThread::currentThread() != application->thread()) {
        LRESULT result = 0;
        const bool invoked = QMetaObject::invokeMethod(
            application,
            [&result, hwnd, Msg, wParam, lParam]() {
                result = SendMessage(hwnd, Msg, wParam, lParam);
            },
            Qt::BlockingQueuedConnection);
        return invoked ? result : 0;
    }

    if (!isLiveQtWidget(hwnd))
        return 0;

    switch (Msg) {
    case WM_CLOSE:
        hwnd->close();
        return 0;

    case WM_SETREDRAW:
        hwnd->setUpdatesEnabled(wParam != 0);
        if (wParam)
            hwnd->update();
        return 0;

    case WM_GETTEXTLENGTH:
        return static_cast<LRESULT>(hwnd->windowTitle().size());

    default:
        // Win32 control messages require a real control adapter. Returning
        // zero explicitly reports unsupported behavior instead of mutating a
        // widget using guessed semantics.
        return 0;
    }
}

BOOL PostMessage(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    QCoreApplication *application = QCoreApplication::instance();
    if (!application || !hwnd || !isSupportedPostedMessage(Msg))
        return FALSE;
    if (QThread::currentThread() == application->thread() && !isLiveQtWidget(hwnd))
        return FALSE;

    const bool queued = QMetaObject::invokeMethod(
        application,
        [hwnd, Msg, wParam, lParam]() {
            // Validation occurs on the GUI thread, so sentinel handles used
            // by unported dialog creation shims are ignored safely.
            if (isLiveQtWidget(hwnd))
                SendMessage(hwnd, Msg, wParam, lParam);
        },
        Qt::QueuedConnection);
    return queued ? TRUE : FALSE;
}

HINSTANCE g_hInst = (HINSTANCE)1;

bool guiChooseColor(QWidget *parent, unsigned int& color) {
    const QColor initialColor(
        static_cast<int>(color & 0xffU),
        static_cast<int>((color >> 8) & 0xffU),
        static_cast<int>((color >> 16) & 0xffU));
    const QColor selectedColor = QColorDialog::getColor(initialColor, parent, QStringLiteral("Select Color"));
    if (!selectedColor.isValid())
        return false;

    color = RGB(selectedColor.red(), selectedColor.green(), selectedColor.blue());
    return true;
}

// These symbols remain named SSE2 because legacy runtime dispatch refers to
// them directly. On the native port they use the already-tested portable DSP
// routines, which is slower than SIMD but always produces valid output.
void VDDSPBlend8_LerpConst_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16, unsigned char factor) {
    if (n16) VDDSPBlend8_LerpConst(dst, src0, src1, n16, factor);
}
void VDDSPBlend8_Min_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_Min(dst, src0, src1, n16);
}
void VDDSPBlend8_Max_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_Max(dst, src0, src1, n16);
}
void VDDSPBlend8_Add_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_Add(dst, src0, src1, n16);
}
void VDDSPBlend8_Multiply_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_Multiply(dst, src0, src1, n16);
}
void VDDSPBlend8_LinearBurn_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_LinearBurn(dst, src0, src1, n16);
}
void VDDSPBlend8_Screen_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_Screen(dst, src0, src1, n16);
}
void VDDSPBlend8_Overlay_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_Overlay(dst, src0, src1, n16);
}
void VDDSPBlend8_HardLight_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_HardLight(dst, src0, src1, n16);
}
void VDDSPBlend8_LinearLight_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_LinearLight(dst, src0, src1, n16);
}
void VDDSPBlend8_PinLight_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_PinLight(dst, src0, src1, n16);
}
void VDDSPBlend8_HardMix_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_HardMix(dst, src0, src1, n16);
}
void VDDSPBlend8_Difference_SSE2(void *dst, const void *src0, const void *src1, unsigned int n16) {
    if (n16) VDDSPBlend8_Difference(dst, src0, src1, n16);
}
void VDDSPBlend8_Lerp_SSE2(void *dst, const void *src0, const void *src1, const void *mask, unsigned int n16) {
    if (n16) VDDSPBlend8_Lerp(dst, src0, src1, mask, n16);
}
void VDDSPBlend8_Select_SSE2(void *dst, const void *src0, const void *src1, const void *mask, unsigned int n16) {
    if (n16) VDDSPBlend8_Select(dst, src0, src1, mask, n16);
}

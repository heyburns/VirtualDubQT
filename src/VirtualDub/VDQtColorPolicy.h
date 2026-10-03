#ifndef VDQT_COLOR_POLICY_H
#define VDQT_COLOR_POLICY_H

#include <QString>
#include <algorithm>
extern "C" {
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

// The legacy output setting names a color family. Matrix, primaries and
// transfer are DIFFERENT FFmpeg enums: never pass one enum's name to all three.
// This is matrix/range conversion and tagging, not HDR tone/gamut mapping.
struct VDQtColorMatrixInfo {
    QString matrix;
    QString primaries;
    QString transfer;
    QString scaleMatrix;
    int coefficients = SWS_CS_ITU601;
};

inline bool VDQtResolveColorMatrix(const QString& name, int bitDepth,
                                   VDQtColorMatrixInfo *result = nullptr) {
    const QString key = name.trimmed().toLower();
    VDQtColorMatrixInfo info;
    if (key == QStringLiteral("bt601") || key == QStringLiteral("smpte170m")) {
        info = {"smpte170m", "smpte170m", "smpte170m", "bt601", SWS_CS_ITU601};
    } else if (key == QStringLiteral("bt709")) {
        info = {"bt709", "bt709", "bt709", "bt709", SWS_CS_ITU709};
    } else if (key == QStringLiteral("bt2020") || key == QStringLiteral("bt2020nc")) {
        info = {"bt2020nc", "bt2020", bitDepth > 10 ? "bt2020-12" : "bt2020-10",
                "bt2020", SWS_CS_BT2020};
    } else {
        return false;
    }
    if (result) *result = info;
    return true;
}

inline QString VDQtOutputMatrixFilter(const QString& name, const QString& pixelFormat) {
    VDQtColorMatrixInfo info;
    const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(
        av_get_pix_fmt(pixelFormat.toUtf8().constData()));
    if (!descriptor) return {};
    int bitDepth = 8;
    for (int component = 0; component < descriptor->nb_components; ++component)
        bitDepth = std::max(bitDepth, static_cast<int>(descriptor->comp[component].depth));
    if (!VDQtResolveColorMatrix(name, bitDepth, &info)) return {};
    const bool rgb = (descriptor->flags & AV_PIX_FMT_FLAG_RGB) != 0;
    QString filters;
    if (!rgb && descriptor->nb_components >= 3)
        filters = QStringLiteral("scale=out_color_matrix=%1:out_range=tv:flags=bicubic,")
            .arg(info.scaleMatrix);
    // Frame properties can override encoder options at the first frame. Set
    // them AFTER conversion so the muxed tags cannot revert to unspecified.
    return filters + QStringLiteral("setparams=range=%1:color_primaries=%2:color_trc=%3:colorspace=%4")
        .arg(rgb ? QStringLiteral("full") : QStringLiteral("limited"), info.primaries,
             info.transfer, rgb ? QStringLiteral("gbr") : info.matrix);
}

#endif

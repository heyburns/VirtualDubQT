#ifndef VDQT_VIDEO_ASPECT_H
#define VDQT_VIDEO_ASPECT_H

#include <QImage>
#include <QString>
#include <limits>

extern "C" {
#include <libavutil/rational.h>
}

// A QImage is the immutable hand-off between decoder workers, cached previews,
// filter stages and export. Store pixel shape with that particular image rather
// than in mutable GUI state, which would describe the wrong frame after a seek.
// The pixels stay unchanged; manual display aspect overrides never edit this.
inline AVRational VDQtNormalizedSampleAspectRatio(qint64 numerator, qint64 denominator) {
    if (numerator <= 0 || denominator <= 0) return {1, 1};
    AVRational result;
    av_reduce(&result.num, &result.den, numerator, denominator, std::numeric_limits<int>::max());
    return result.num > 0 && result.den > 0 ? result : AVRational{1, 1};
}

inline AVRational VDQtImageSampleAspectRatio(const QImage& image) {
    const QString text = image.text(QStringLiteral("VirtualDubQt.SampleAspectRatio"));
    const qsizetype separator = text.indexOf(QLatin1Char(':'));
    if (separator < 0) return {1, 1};
    bool numeratorValid = false, denominatorValid = false;
    const qint64 numerator = text.left(separator).toLongLong(&numeratorValid);
    const qint64 denominator = text.mid(separator + 1).toLongLong(&denominatorValid);
    return numeratorValid && denominatorValid
        ? VDQtNormalizedSampleAspectRatio(numerator, denominator) : AVRational{1, 1};
}

inline void VDQtSetImageSampleAspectRatio(QImage& image, AVRational ratio) {
    if (image.isNull()) return;
    ratio = VDQtNormalizedSampleAspectRatio(ratio.num, ratio.den);
    const QString key = QStringLiteral("VirtualDubQt.SampleAspectRatio");
    // Square pixels need no allocation of text metadata. More importantly,
    // do not detach a shallow no-op/temporal image just to reapply the same SAR.
    const QString value = ratio.num == ratio.den ? QString()
        : QStringLiteral("%1:%2").arg(ratio.num).arg(ratio.den);
    if (image.text(key) != value) image.setText(key, value);
}

#endif

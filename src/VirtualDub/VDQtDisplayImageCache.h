#ifndef VDQT_DISPLAY_IMAGE_CACHE_H
#define VDQT_DISPLAY_IMAGE_CACHE_H

#include <QImage>
#include <QSize>
#include <utility>

// A single presentation-only result. Pixel/shape changes invalidate naturally;
// panning, exposing the window and painting a new badge do not resample again.
// Keep the existing Qt scaling/alpha conversion exactly, including 16-bit data.
class VDQtDisplayImageCache {
public:
    explicit VDQtDisplayImageCache(qsizetype maximumBytes = 32 * 1024 * 1024)
        : mMaximumBytes(qMax<qsizetype>(0, maximumBytes)) {}

    QImage render(const QImage& source, const QSize& size, bool alphaOnly,
                  Qt::TransformationMode transformation) {
        if (source.isNull() || size.isEmpty()) {
            clear();
            return {};
        }
        const bool effectiveAlpha = alphaOnly && source.hasAlphaChannel();
        if (!mImage.isNull() && mSourceKey == source.cacheKey() && mSize == size
            && mAlphaOnly == effectiveAlpha && mTransformation == transformation)
            return mImage;
        const QImage input = effectiveAlpha
            ? source.convertToFormat(QImage::Format_Alpha8) : source;
        QImage scaled = input.scaled(size, Qt::IgnoreAspectRatio, transformation);
        clear();
        // Extreme fixed zoom remains drawable but must not pin a huge image or
        // make the next frame allocate alongside a huge retained scaled image.
        if (scaled.sizeInBytes() > mMaximumBytes) return scaled;
        mImage = std::move(scaled);
        mSourceKey = source.cacheKey();
        mSize = size;
        mAlphaOnly = effectiveAlpha;
        mTransformation = transformation;
        return mImage;
    }

    void clear() {
        mImage = {};
        mSourceKey = 0;
        mSize = {};
    }

private:
    const qsizetype mMaximumBytes;
    QImage mImage;
    qint64 mSourceKey = 0;
    QSize mSize;
    bool mAlphaOnly = false;
    Qt::TransformationMode mTransformation = Qt::FastTransformation;
};

#endif

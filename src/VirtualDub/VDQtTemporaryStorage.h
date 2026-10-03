#ifndef VDQT_TEMPORARY_STORAGE_H
#define VDQT_TEMPORARY_STORAGE_H

#include <QStorageInfo>
#include <QString>
#include <limits>

// Conservative preflight, not a reservation: another process can consume space
// after this check, so every encoder/write still has to handle I/O failures.
// Keep the arithmetic integral and checked before constructing large sources.
inline bool VDQtEstimateTemporaryStorage(qint64 payloadBytes, qint64 units,
                                         qint64 overheadPerUnit, qint64 *result) {
    constexpr qint64 reserve = qint64{64} * 1024 * 1024;
    constexpr qint64 maximum = std::numeric_limits<qint64>::max();
    if (!result || payloadBytes < 0 || units < 0 || overheadPerUnit < 0
        || payloadBytes > maximum - reserve) return false;
    const qint64 remaining = maximum - reserve - payloadBytes;
    if (overheadPerUnit && units > remaining / overheadPerUnit) return false;
    *result = payloadBytes + units * overheadPerUnit + reserve;
    return true;
}

// Rendered intermediates estimate the uncompressed payload conservatively.
// Never multiply dimensions/counts before checking: saved source metadata can
// describe more bytes than a signed file offset can represent.
inline bool VDQtEstimateFrameTemporaryStorage(qint64 width, qint64 height,
                                              qint64 bytesPerPixel, qint64 frames,
                                              qint64 overheadPerFrame, qint64 *result) {
    constexpr qint64 maximum = std::numeric_limits<qint64>::max();
    if (!result || width <= 0 || height <= 0 || bytesPerPixel <= 0
        || frames < 0 || overheadPerFrame < 0) return false;
    qint64 payload = width;
    for (const qint64 factor : {height, bytesPerPixel, frames}) {
        if (factor && payload > maximum / factor) return false;
        payload *= factor;
    }
    return VDQtEstimateTemporaryStorage(payload, frames, overheadPerFrame, result);
}

inline bool VDQtRequireTemporaryStorage(const QString& directory,
                                        qint64 requiredBytes, QString *error) {
    QStorageInfo storage(directory);
    storage.refresh();
    if (requiredBytes < 0 || !storage.isValid() || !storage.isReady()
        || storage.isReadOnly() || storage.bytesAvailable() < 0) {
        if (error) *error = QStringLiteral("The temporary storage volume is unavailable or its free space cannot be checked.");
        return false;
    }
    if (storage.bytesAvailable() < requiredBytes) {
        if (error) *error = QStringLiteral(
            "There is not enough free temporary storage. This operation needs approximately %1 GiB "
            "including safety space, but %2 GiB is available on %3. Choose another temporary "
            "location or use a mode that does not require this intermediate copy.")
            .arg(requiredBytes / (1024.0 * 1024 * 1024), 0, 'f', 2)
            .arg(storage.bytesAvailable() / (1024.0 * 1024 * 1024), 0, 'f', 2)
            .arg(storage.rootPath());
        return false;
    }
    if (error) error->clear();
    return true;
}

#endif

#ifndef VDQTMATROSKATIMING_H
#define VDQTMATROSKATIMING_H

#include <QByteArray>
#include <QString>
#include <QVector>

struct VDQtMatroskaFrame {
    qint64 timestamp = 0;
    qint64 duration = 0;
    qint64 seekPosition = -1; // Containing Cluster ID position, not packet payload.
    bool keyFrame = false;
};

struct VDQtMatroskaTiming {
    QVector<VDQtMatroskaFrame> frames;
    qint64 referenceTimestamp = 0;
    quint64 headerBytesRead = 0;
};

// Read Block/SimpleBlock headers, skipping compressed pictures, audio and
// attachments. packetPosition is FFmpeg's first selected packet's pos (the
// block data's track VINT); it selects the track without assuming stream order.
// Timestamps are in the selected stream's time base, before its affine codec
// delay adjustment, which the caller calibrates against the reference packet.
// Malformed or unsupported layouts return false without changing the output.
bool VDQtReadMatroskaTiming(const QString& path, qint64 packetPosition,
                           const QByteArray& codecId, int timeBaseNumerator,
                           int timeBaseDenominator, VDQtMatroskaTiming *timing);

#endif

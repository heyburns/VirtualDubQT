#ifndef VDQTASFTIMING_H
#define VDQTASFTIMING_H

#include <QString>
#include <QVector>

struct VDQtAsfFrameTiming {
    qint64 timestamp = 0; // ASF milliseconds before FFmpeg's preroll adjustment.
    qint64 seekPosition = -1; // Packet containing the beginning of this object.
    quint32 size = 0;
    bool keyFrame = false;
};

// Read fixed-size ASF packet/payload headers, skipping compressed picture/audio
// bytes. The caller must establish a codec with one displayed picture per object
// and no reordering. Fragmented objects are counted only after complete assembly.
// Unsupported/ambiguous layouts fail transactionally, retaining decoded indexing.
bool VDQtReadAsfTiming(const QString& path, int streamId,
                       QVector<VDQtAsfFrameTiming> *output);

#endif

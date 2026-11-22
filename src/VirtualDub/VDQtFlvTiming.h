#ifndef VDQTFLVTIMING_H
#define VDQTFLVTIMING_H

#include <QString>
#include <QVector>

struct VDQtFlvFrameTiming {
    qint64 timestamp = 0; // FLV milliseconds; Spark has no picture reordering.
    qint64 seekPosition = -1; // Tag header, following PreviousTagSize.
    bool keyFrame = false;
};

// A deliberately narrow acceleration for ordinary Sorenson Spark (FLV1).
// Walk tag headers and bounded picture prefixes, skipping compressed payload.
// Validate startup disposable-picture accounting against the bundled decoder's
// reference rules. Unsupported codecs/layouts leave output unchanged.
bool VDQtReadFlvTiming(const QString& path, int width, int height,
                       QVector<VDQtFlvFrameTiming> *output);

#endif

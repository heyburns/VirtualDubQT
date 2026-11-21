#ifndef VDQTMP4TIMING_H
#define VDQTMP4TIMING_H

#include <QString>
#include <QVector>

// libavformat exposes MOV sample positions/DTS/key flags, but not the CTTS
// composition offsets needed to map those samples to displayed pictures. Read
// only that metadata, never the movie payload or FFmpeg's private structures.
// Unsupported/ambiguous layouts return false and retain the decoder fallback.
bool VDQtReadMp4CompositionOffsets(const QString& path, int trackId,
                                  int timeScale, int sampleCount,
                                  QVector<qint64> *offsets);

#endif

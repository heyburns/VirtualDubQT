#ifndef VDQTAVITIMING_H
#define VDQTAVITIMING_H

#include <QString>
#include <QVector>
extern "C" {
#include <libavformat/avformat.h>
}

struct VDQtAviFrameOrder {
    QVector<int> samples; // Native index entries in presentation order.
    bool hasBFrames = false;
};

// Validate FFmpeg's complete native AVI index against RIFF chunk headers.
// MPEG-4 additionally uses the public header parser to recover picture order,
// rejecting packed/NVOP layouts rather than equating chunks with pictures.
// Only bounded prefixes are read; no compressed picture is decoded here.
// Failure leaves output unchanged and retains the decoder's verified fallback.
bool VDQtReadAviFrameOrder(const QString& path, AVStream *stream,
                          VDQtAviFrameOrder *output);

#endif

#ifndef VDQT_TEST_FIXTURES_H
#define VDQT_TEST_FIXTURES_H

#include <QImage>
#include <QStringList>
#include <QTemporaryDir>

// Every fixture and export belongs to this directory. Nothing reads or replaces
// user media, configuration, or queues; QTemporaryDir removes only our own files.
class VDQtTestFixtures {
public:
    QTemporaryDir directory;
    QString error;
    QString mp4;
    QString avs;
    QStringList edgeCaseMedia;

    bool createBasic(int width = 320, int height = 180, int frames = 48);
    bool createEdgeCases();
    bool writeText(const QString& path, const QByteArray& contents);
    bool ffmpeg(const QStringList& arguments);
    qint64 diskBytes() const;

    // Padding, varying alpha, and nontrivial edges expose accidental assumptions
    // about RGB row length, alpha preservation, and shared-image ownership.
    static QImage patternedImage(int width, int height, QImage::Format format,
                                int frame = 0);
};

#endif

// Exercise real audio export waiters when their progress callback pumps the
// child's finished event. The executable doubles as a deterministic encoder;
// only disposable generated PCM, temporary PATH/environment and our own child
// processes are used. A second callback cancels an old broken loop, so this
// regression fails promptly instead of leaving CTest spinning indefinitely.
#include <QString>
#include "VirtualDub/VDQtAudioExport.h"
#include "VirtualDub/VDQtAudioPlayer.h"
#include "VirtualDub/VDQtWaveform.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <iostream>
#include <limits>

namespace {
constexpr int fixtureFrames = 12007;
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
bool writePcm(const QString& path, qint64 frames) {
    if (frames < 1 || frames > 1000000) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    const quint32 bytes = quint32(frames * 4);
    stream.writeRawData("RIFF", 4);
    stream << quint32(bytes + 36);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000)
           << quint32(192000) << quint16(4) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    const QByteArray block(4096, '\0');
    quint32 remaining = bytes;
    while (remaining) {
        const int count = int(std::min<quint32>(remaining, quint32(block.size())));
        if (stream.writeRawData(block.constData(), count) != count) return false;
        remaining -= quint32(count);
    }
    return stream.status() == QDataStream::Ok;
}
class Environment final {
public:
    explicit Environment(const char *name)
        : mName(name), mValue(qgetenv(name)), mPresent(qEnvironmentVariableIsSet(name)) {}
    ~Environment() {
        if (mPresent) qputenv(mName.constData(), mValue);
        else qunsetenv(mName.constData());
    }
    void set(const QByteArray& value) { qputenv(mName.constData(), value); }
private:
    QByteArray mName;
    QByteArray mValue;
    bool mPresent;
};

int encoderChild(const QStringList& arguments) {
    const QString control = qEnvironmentVariable("VDQT_COMPLETION_CONTROL");
    const QString root = qEnvironmentVariable("VDQT_COMPLETION_ROOT");
    const int inputArgument = arguments.indexOf(QStringLiteral("-i"));
    if (control.isEmpty() || root.isEmpty() || inputArgument < 0
        || inputArgument + 1 >= arguments.size() || arguments.isEmpty()) return 90;
    const QString input = arguments.at(inputArgument + 1);
    const QString output = QFileInfo(arguments.last()).absoluteFilePath();
    if (!output.startsWith(root + QLatin1Char('/')) || output == QFileInfo(input).absoluteFilePath()
        || !writeFile(QDir(control).filePath("pid"), QByteArray::number(QCoreApplication::applicationPid())))
        return 91;
    QElapsedTimer deadline;
    deadline.start();
    while (!QFileInfo::exists(QDir(control).filePath("release"))) {
        if (deadline.elapsed() > 5000) {
            std::fputs("ENCODER_TOKEN_TIMEOUT\n", stderr);
            return 92;
        }
        QThread::msleep(1);
    }
    if (qEnvironmentVariable("VDQT_COMPLETION_MODE") == QStringLiteral("failure")) {
        std::fputs("FINAL_FAILURE_MARKER\n", stderr);
        std::fflush(stderr);
        return 7;
    }
    QFile::remove(output); // Only the checked, operation-owned staging target.
    bool copied = false;
    if (input.endsWith(QStringLiteral(".ffconcat"))) {
        QFile manifest(input);
        if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 16 * 1024) return 93;
        qint64 frames = 0;
        for (const QByteArray& line : manifest.readAll().split('\n')) {
            if (!line.startsWith("file ")) continue;
            const QString segment = QFileInfo(input).dir().filePath(QString::fromUtf8(line.mid(5)));
            VDQtWaveformData waveform;
            if (!VDQtReadWaveformPeaks(segment, 1, &waveform) || waveform.channels != 2
                || waveform.sampleRate != 48000 || waveform.containerBits != 16
                || waveform.floatingPoint || waveform.sampleFrames > 1000000 - frames) return 94;
            frames += waveform.sampleFrames;
        }
        // The generated fixture is silence, so this is an exact PCM concat,
        // not a malformed placeholder pretending to be an encoded output.
        copied = writePcm(output, frames);
    } else copied = QFile::copy(input, output);
    if (!copied) return 95;
    std::fputs("FINAL_SUCCESS_MARKER\n", stderr);
    std::fflush(stderr);
    return 0;
}

struct Completion {
    QString control;
    bool cancelRunning = false;
    bool cancelCompleted = false;
    bool released = false;
    bool reapedInCallback = false;
    bool invalidPid = false;
    int encodingCallbacks = 0;
    qint64 pid = 0;

    bool progress(int current, int) {
        // Source extraction and the final success notification do not wait
        // for a child. Detect its handshake, not a guessed extraction percent.
        if (current >= 100) return true;
        QFile pidFile(QDir(control).filePath("pid"));
        if (!pidFile.open(QIODevice::ReadOnly)) return true;
        bool valid = false;
        pid = pidFile.read(32).trimmed().toLongLong(&valid);
        if (!valid || pid <= 1 || pid == QCoreApplication::applicationPid()
            || pid > std::numeric_limits<int>::max()) {
            invalidPid = true;
            return false;
        }
        // An unguarded while(!waitForFinished()) reaches here again after
        // its child was reaped. Cancel it so the baseline never hangs.
        if (++encodingCallbacks != 1) return false;
        if (cancelRunning) return false;
        if (!writeFile(QDir(control).filePath("release"), "release\n")) return false;
        released = true;
        QElapsedTimer deadline;
        deadline.start();
        const QString processPath = QStringLiteral("/proc/%1").arg(pid);
        while (QFileInfo::exists(processPath)) {
            if (deadline.elapsed() > 5000) return false;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        // /proc disappears only after the owning QProcess has reaped this
        // controlled child. Its finished event was processed before returning.
        reapedInCallback = true;
        return !cancelCompleted;
    }
};

enum class Path { AudioExport, Transcode, Concat };

bool boundedEofCases(const QString& root) {
    const QString input = QDir(root).filePath("two-second-source.wav");
    if (!writePcm(input, 2 * 48000)) return false;
    VDQtAudioPlayer player(false);
    if (!player.openFile(input)) return false;
    const auto progress = [](int, int) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return true;
    };
    bool passed = true;
    struct Case { int startSeconds; int requestedSeconds; bool pad; int expectedSeconds; };
    const Case cases[] = {{0, 10, false, 2}, {0, 10, true, 10},
                          {0, 1, false, 1}, {3, 10, false, 0}};
    int number = 0;
    for (const auto& test : cases) {
        const QString output = QDir(root).filePath(QStringLiteral("eof-%1.wav").arg(number++));
        const QByteArray original("existing output stays intact when EOF precedes the requested window");
        if (!writeFile(output, original)) return false;
        VDQtAudioExportRequest request;
        request.outputPath = output;
        request.replaceExisting = true;
        request.codec.codecId = QStringLiteral("pcm_s16le");
        request.sampleRanges = {{int64_t(test.startSeconds) * 48000, int64_t(test.requestedSeconds) * 48000}};
        // Leaving the true case untouched also guards the legacy default.
        if (!test.pad) request.padToRequestedLength = false;
        QElapsedTimer deadline;
        deadline.start();
        QString error;
        const bool exported = VDQtExportAudio(player, request, progress, &error);
        if (test.expectedSeconds) {
            VDQtWaveformData waveform;
            passed &= check(exported && VDQtReadWaveformPeaks(output, 1, &waveform)
                && waveform.sampleFrames == qint64(test.expectedSeconds) * 48000
                && waveform.durationSeconds == test.expectedSeconds,
                "bounded EOF preserves actual audio, legacy padding and the requested hard sample cap");
        } else {
            QFile unchanged(output);
            passed &= check(!exported && !error.isEmpty() && deadline.elapsed() < 5000
                && unchanged.open(QIODevice::ReadOnly) && unchanged.readAll() == original,
                "an audio-only window starting after EOF fails promptly without padding or replacing output");
        }
    }
    const QList<VDAudioFilterInstance> noFilters;
    const QString single = QDir(root).filePath("eof-single-range.wav");
    const QString multiple = QDir(root).filePath("eof-multiple-ranges.wav");
    VDQtWaveformData waveform;
    passed &= check(player.exportAudioRangesToFile(single, {{0, 10 * 48000}}, progress, &noFilters, false)
        && VDQtReadWaveformPeaks(single, 1, &waveform) && waveform.sampleFrames == 2 * 48000,
        "the bounded EOF option survives the single-range shortcut");
    passed &= check(player.exportAudioRangesToFile(multiple, {{0, 5 * 48000}, {48000, 5 * 48000}},
        progress, &noFilters, false) && VDQtReadWaveformPeaks(multiple, 1, &waveform)
        && waveform.sampleFrames == 3 * 48000,
        "each edited range stops at source EOF before the actual PCM concatenation");
    VDQtAudioExportRequest edited;
    edited.outputPath = QDir(root).filePath("eof-edited-export.wav");
    edited.codec.codecId = QStringLiteral("pcm_s16le");
    edited.sampleRanges = {{0, 5 * 48000}, {48000, 5 * 48000}};
    edited.padToRequestedLength = false;
    QString error;
    passed &= check(VDQtExportAudio(player, edited, progress, &error)
        && VDQtReadWaveformPeaks(edited.outputPath, 1, &waveform) && waveform.sampleFrames == 3 * 48000,
        "edited AudioExport requests pass their bounded EOF policy through extraction and encoding");
    return passed;
}

bool completionCase(const QString& root, const QString& input, Path path,
                    bool failure, bool cancelRunning = false, bool cancelCompleted = false) {
    QTemporaryDir control(QDir(root).filePath("control-XXXXXX"));
    if (!control.isValid()) return false;
    Environment controlEnvironment("VDQT_COMPLETION_CONTROL");
    Environment modeEnvironment("VDQT_COMPLETION_MODE");
    controlEnvironment.set(control.path().toUtf8());
    modeEnvironment.set(failure ? "failure" : "success");
    const QString output = QDir(control.path()).filePath("output.wav");
    const QByteArray original("original output must survive a failed or canceled export");
    if (!writeFile(output, original)) return false;
    Completion completion{control.path(), cancelRunning, cancelCompleted};
    VDQtAudioPlayer player(false);
    if (!player.openFile(input)) return false;
    const auto progress = [&](int current, int maximum) { return completion.progress(current, maximum); };
    QString error;
    bool exported = false;
    if (path == Path::AudioExport) {
        VDQtAudioExportRequest request;
        request.outputPath = output;
        request.replaceExisting = true;
        request.codec.codecId = QStringLiteral("pcm_s16le");
        request.sampleRanges = {{0, fixtureFrames}};
        exported = VDQtExportAudio(player, request, progress, &error);
    } else if (path == Path::Transcode) {
        VDQtAudioFilterSystem filters;
        VDAudioFilterInstance gain = filters.createFilter(VDAudioFilterType::Gain);
        gain.params["decibels"] = 0;
        const QList<VDAudioFilterInstance> identity = {gain};
        exported = player.exportAudioToFile(output, 0, fixtureFrames, progress, &identity);
    } else {
        const QList<VDAudioFilterInstance> noFilters;
        exported = player.exportAudioRangesToFile(output, {{0, 6000}, {6000, 6007}}, progress, &noFilters);
    }
    bool passed = check(!completion.invalidPid && completion.encodingCallbacks == 1,
        "finished child is not waited on again after event-pumped progress returns");
    passed &= check(completion.pid > 0 && !QFileInfo::exists(QStringLiteral("/proc/%1").arg(completion.pid)),
        "every controlled encoder has been reaped before returning");
    if (!cancelRunning)
        passed &= check(completion.released && completion.reapedInCallback,
            "controlled child completion was actually delivered inside the progress callback");
    if (!failure && !cancelRunning && !cancelCompleted) {
        VDQtWaveformData waveform;
        passed &= check(exported && VDQtReadWaveformPeaks(output, 1, &waveform)
            && waveform.sampleFrames == fixtureFrames && waveform.channels == 2 && waveform.sampleRate == 48000,
            "successful completion publishes a complete valid generated PCM output");
    } else {
        QFile unchanged(output);
        passed &= check(!exported && unchanged.open(QIODevice::ReadOnly) && unchanged.readAll() == original,
            "failed or canceled completion preserves the original destination");
        if (path == Path::AudioExport)
            passed &= check(error.contains(failure ? "FINAL_FAILURE_MARKER" : "cancelled"),
                "final stderr failure diagnostics and cancellation are preserved");
    }
    return passed;
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == QStringLiteral("ffmpeg"))
        return encoderChild(application.arguments().mid(1));
    QTemporaryDir root;
    if (!root.isValid() || !QFileInfo::exists(QStringLiteral("/proc/self"))) return 1;
    // These cases deliberately use the real installed FFmpeg before PATH is
    // isolated below: they verify the complete native extraction/encode path.
    bool passed = boundedEofCases(root.path());
    const QString binaryDirectory = QDir(root.path()).filePath("bin");
    const QString source = QDir(root.path()).filePath("source.wav");
    if (!QDir().mkpath(binaryDirectory) || !writePcm(source, fixtureFrames)
        || !QFile::link(QCoreApplication::applicationFilePath(), QDir(binaryDirectory).filePath("ffmpeg"))) return 1;
    Environment pathEnvironment("PATH");
    Environment rootEnvironment("VDQT_COMPLETION_ROOT");
    pathEnvironment.set(binaryDirectory.toUtf8() + ':' + qgetenv("PATH"));
    rootEnvironment.set(root.path().toUtf8());
    for (Path path : {Path::AudioExport, Path::Transcode, Path::Concat}) {
        passed &= completionCase(root.path(), source, path, false);
        passed &= completionCase(root.path(), source, path, true);
        passed &= completionCase(root.path(), source, path, false, true);
    }
    passed &= completionCase(root.path(), source, Path::AudioExport, false, false, true);
    return passed ? 0 : 1;
}

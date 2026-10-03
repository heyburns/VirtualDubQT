// Project encoder-control contracts, not the separate codec troubleshooting
// suite. Verify both the visible choices and the options actually executed.
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtCodecEngine.h"
#include "VirtualDub/VDQtDialogs.h"
#include "VirtualDub/VDQtVideoExporter.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QProcess>
#include <QSpinBox>
#include <QTimer>
#include <cmath>
#include <iostream>

namespace {
bool check(bool condition, const char *description) {
    if (!condition) std::cerr << "FAIL: " << description << '\n';
    return condition;
}

QString option(const QStringList& arguments, const QString& key) {
    const int index = arguments.indexOf(key);
    return index >= 0 && index + 1 < arguments.size() ? arguments[index + 1] : QString();
}

QByteArray bytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject probe(const QString& path) {
    QProcess process;
    process.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-count_frames",
        "-show_entries", "stream=codec_name,profile,nb_read_frames", "-of", "json", path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(5000);
        return {};
    }
    const auto streams = QJsonDocument::fromJson(process.readAllStandardOutput())
        .object().value("streams").toArray();
    return streams.isEmpty() ? QJsonObject{} : streams.first().toObject();
}

bool policyContracts() {
    bool passed = true;
    QStringList arguments;
    QString error;
    auto params = VDQtCodecEngine::getDefaultVideoParamsForCodec("libx264");
    params.rateMode = "bitrate";
    params.targetBitrateKbps = 700;
    params.maxBitrateKbps = 900;
    params.preset = "fast";
    params.tune = "grain";
    params.profile = "main";
    params.twoPass = true;
    passed &= check(VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && option(arguments, "-b:v") == "700k" && option(arguments, "-maxrate") == "900k"
        && option(arguments, "-bufsize") == "1800k" && option(arguments, "-profile:v") == "main"
        && option(arguments, "-preset") == "fast" && option(arguments, "-tune") == "grain",
        "x264 profile, target/maximum bitrate, preset and tuning are emitted");
    passed &= check(VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, true, &arguments, &error)
        && option(arguments, "-bf") == "0", "VFR explicitly disables encoder reordering");
    params.twoPass = false;
    params.maxBitrateKbps = 0;
    params.rateMode = "cqp";
    params.crf = 17;
    passed &= check(VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && option(arguments, "-qp") == "17" && !arguments.contains("-b:v"),
        "x264 constant quantizer does not silently become bitrate mode");
    params.rateMode = "lossless";
    params.profile.clear();
    passed &= check(VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && option(arguments, "-qp") == "0", "x264 lossless uses QP zero");
    params.profile = "high";
    arguments = {"unchanged"};
    passed &= check(!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && arguments == QStringList{"unchanged"} && error.contains("Lossless x264"),
        "incompatible lossless profile fails without altering the destination arguments");
    params.rateMode = "crf";
    params.crf = 0;
    passed &= check(!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error),
        "CRF zero is lossless too and requires a compatible x264 profile");
    passed &= check(VDQtCodecEngine::getDefaultVideoParamsForCodec("  LIBX264_10BIT  ").codecId
        == "libx264_10bit", "encoder defaults normalize aliases consistently with command generation");

    params = VDQtCodecEngine::getDefaultVideoParamsForCodec("libx265_lossless");
    params.tune = "grain";
    passed &= check(VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && option(arguments, "-x265-params") == "lossless=1"
        && option(arguments, "-preset") == "medium" && option(arguments, "-tune") == "grain"
        && option(arguments, "-profile:v") == "main",
        "lossless HEVC still applies speed, tuning and profile choices");
    params.rateMode = "crf";
    passed &= check(!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error),
        "the lossless HEVC alias cannot claim a lossy saved rate mode");

    params = VDQtCodecEngine::getDefaultVideoParamsForCodec("libsvtav1");
    passed &= check(VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && option(arguments, "-preset") == "6" && option(arguments, "-crf") == "30",
        "SVT's numeric preset is applied (policy coverage even when encoder is unavailable)");
    params.preset = "medium";
    passed &= check(!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && error.contains("preset"), "SVT does not accept an ineffective x264-style preset");

    params = VDQtCodecEngine::getDefaultVideoParamsForCodec("mpeg4");
    passed &= check(params.rateMode == "cqp"
        && VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && option(arguments, "-q:v") == "5" && !arguments.contains("-crf"),
        "native MPEG-4 quality is mapped to the supported qscale option");
    for (const QString& invalid : {QString("crf"), QString("lossless")}) {
        params.rateMode = invalid;
        passed &= check(!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error),
            "MPEG-4 rejects an unsupported saved quality/lossless mode");
    }
    params.rateMode = "cqp";
    params.crf = 32;
    passed &= check(!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error),
        "MPEG-4 quantizer range is not the unrelated CRF 0..63 range");
    params = VDQtCodecEngine::getDefaultVideoParamsForCodec("libvpx");
    passed &= check(VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && option(arguments, "-b:v") == "2000k", "VP8 constrained quality retains its required target bitrate");
    params.targetBitrateKbps = 0;
    passed &= check(!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error),
        "VP8 zero-bitrate constant quality fails before FFmpeg is started");
    params = VDQtCodecEngine::getDefaultVideoParamsForCodec("libvpx-vp9");
    params.rateMode = "lossless";
    passed &= check(VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && option(arguments, "-lossless") == "1", "VP9 lossless is an actual encoder option");

    params = VDQtCodecEngine::getDefaultVideoParamsForCodec("libaom-av1");
    passed &= check(VDQtCodecEngine::getVideoCapabilities(params.codecId).rateModes == QStringList{"default"}
        && VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)
        && !arguments.contains("-preset") && !arguments.contains("-crf"),
        "uncurated lib-prefixed encoders do not inherit guessed x264 capabilities");
    params.preset = "veryfast";
    passed &= check(!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error),
        "explicit unsupported presets are reported rather than ignored");
    params = VDQtCodecEngine::getDefaultVideoParamsForCodec("ffv1");
    params.ffv1Slices = 5;
    passed &= check(!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error),
        "invalid FFV1 slice grids fail preflight");
    for (const QString& id : {QString("prores_ks"), QString("ffv1"), QString("huffyuv"), QString("cfhd")}) {
        params = VDQtCodecEngine::getDefaultVideoParamsForCodec(id);
        params.rateMode = "crf";
        params.crf = 23;
        params.preset = "medium";
        params.profile = "high";
        passed &= check(VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error),
            "inert legacy intra-codec schema defaults remain backward compatible");
    }
    return passed;
}

bool dialogContracts() {
    bool passed = true;
    VDVideoCompressionDialog dialog;
    auto *list = dialog.findChild<QListWidget*>();
    if (!list) return false;
    for (const QString& id : {QString("mpeg4"), QString("libx265_lossless"), QString("libx264")}) {
        int row = -1;
        for (int index = 0; index < list->count(); ++index)
            if (list->item(index)->data(Qt::UserRole).toString() == id) row = index;
        if (row < 0) {
            std::cout << "Unavailable dialog encoder skipped: " << id.toStdString() << '\n';
            continue;
        }
        list->setCurrentRow(row);
        bool inspected = false;
        QTimer::singleShot(0, [&]() {
            auto *modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!modal) return;
            auto *mode = modal->findChild<QComboBox*>("videoRateMode");
            if (id == "mpeg4") {
                auto *quality = modal->findChild<QSpinBox*>("videoQuality");
                inspected = mode && mode->findData("cqp") >= 0 && mode->findData("crf") < 0
                    && mode->findData("lossless") < 0 && quality && quality->minimum() == 1
                    && quality->maximum() == 31 && !modal->findChild<QComboBox*>("videoPreset");
            } else if (id == "libx265_lossless") {
                inspected = mode && mode->count() == 1 && mode->currentData() == "lossless"
                    && modal->findChild<QComboBox*>("videoPreset")
                    && modal->findChild<QComboBox*>("videoProfile");
            } else {
                inspected = mode && mode->findData("cqp") >= 0 && mode->findData("lossless") >= 0
                    && modal->findChild<QComboBox*>("videoProfile")
                    && modal->findChild<QComboBox*>("videoTune");
            }
            modal->reject();
        });
        const bool invoked = QMetaObject::invokeMethod(&dialog, "onConfigureClicked", Qt::DirectConnection);
        passed &= check(invoked && inspected, "codec dialogs expose only effective encoder-specific controls");
    }
    return passed;
}

bool actualEncoding(VDQtTestFixtures& fixtures) {
    bool passed = true;
    const QString pattern = "testsrc2=size=64x48:rate=24";
    const auto encode = [&](VDVideoCodecParams params, const QString& basename, QByteArray *log = nullptr) {
        QStringList arguments;
        QString error;
        if (!VDQtCodecEngine::buildFfmpegVideoEncodeArguments(params, false, &arguments, &error)) {
            std::cerr << error.toStdString() << '\n';
            return QString();
        }
        // Bound the test encoder only; do not change the application's choices.
        if (arguments.contains("-x265-params")) {
            const int index = arguments.indexOf("-x265-params") + 1;
            arguments[index] += ":pools=1:frame-threads=1";
        } else if (params.codecId.startsWith("libx265")) {
            arguments << "-x265-params" << "pools=1:frame-threads=1";
        }
        const QString output = fixtures.directory.filePath(basename + ".mkv");
        QProcess process;
        process.start("ffmpeg", QStringList{"-hide_banner", "-nostdin", "-loglevel", "info", "-y",
            "-f", "lavfi", "-i", pattern, "-frames:v", "4", "-threads", "1"}
            + arguments + QStringList{"-pix_fmt", params.pixFmt, "-an", output});
        if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
            process.kill();
            process.waitForFinished(5000);
            return QString();
        }
        const QByteArray stderrLog = process.readAllStandardError();
        if (log) *log = stderrLog;
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            std::cerr << stderrLog.constData() << '\n';
            return QString();
        }
        return output;
    };
    auto params = VDQtCodecEngine::getDefaultVideoParamsForCodec("mpeg4");
    params.crf = 2;
    const QString goodQuality = encode(params, "mpeg4-q2");
    params.crf = 31;
    const QString lowQuality = encode(params, "mpeg4-q31");
    passed &= check(!goodQuality.isEmpty() && !lowQuality.isEmpty()
        && probe(goodQuality).value("nb_read_frames") == "4"
        && bytes(goodQuality).size() > bytes(lowQuality).size(),
        "real native MPEG-4 qscale changes encoded size while retaining every frame");

    if (VDQtCodecEngine::instance().checkVideoEncoderAvailable("libx264")) {
        params = VDQtCodecEngine::getDefaultVideoParamsForCodec("libx264");
        params.profile = "baseline";
        params.bFrames = 0;
        params.maxBitrateKbps = 900;
        QByteArray log;
        const QString output = encode(params, "h264-profile-vbv", &log);
        passed &= check(!output.isEmpty() && probe(output).value("profile").toString().contains("Baseline")
            && log.contains("vbv_maxrate=900") && log.contains("vbv_bufsize=1800"),
            "real H.264 stream has the selected profile and encoder-confirmed VBV settings");
    }
    for (const QString& id : {QString("libvpx"), QString("libvpx-vp9"), QString("libx265_lossless")}) {
        if (!VDQtCodecEngine::instance().checkVideoEncoderAvailable(id)) continue;
        params = VDQtCodecEngine::getDefaultVideoParamsForCodec(id);
        if (id == "libvpx-vp9") params.rateMode = "lossless";
        const QString output = encode(params, id);
        passed &= check(!output.isEmpty() && probe(output).value("nb_read_frames") == "4",
            "real VP8 quality/VP9 lossless/HEVC lossless commands are accepted");
        if (!output.isEmpty() && params.rateMode == "lossless") {
            const QString decoded = fixtures.directory.filePath(id + ".raw");
            const QString reference = fixtures.directory.filePath(id + "-reference.raw");
            passed &= check(fixtures.ffmpeg({"-i", output, "-pix_fmt", "yuv420p", "-f", "rawvideo", decoded})
                && fixtures.ffmpeg({"-f", "lavfi", "-i", pattern, "-frames:v", "4", "-pix_fmt", "yuv420p",
                                   "-f", "rawvideo", reference})
                && bytes(decoded) == bytes(reference), "lossless video decodes to the exact input YUV samples");
        }
    }
    return passed;
}

bool exportContracts(VDQtTestFixtures& fixtures) {
    if (!fixtures.createBasic(64, 48, 4)) return false;
    bool passed = true;
    VDQtVideoExporter exporter;
    VDQtVideoExporter::ExportOptions options;
    options.inputPath = fixtures.mp4;
    options.includeAudio = false;
    options.endFrame = 3;
    options.containerType = "mkv";
    options.unattended = true;
    options.processing = VDQtVideoExporter::ProcessingSnapshot{};
    options.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec("mpeg4");
    for (int mode : {VideoMode_FullProcessing, VideoMode_FastRecompress}) {
        options.videoMode = mode;
        options.outputPath = fixtures.directory.filePath(QString("project-export-%1.mkv").arg(mode));
        passed &= check(exporter.exportVideo(options)
            && probe(options.outputPath).value("codec_name") == "mpeg4"
            && probe(options.outputPath).value("nb_read_frames") == "4",
            "processed and Fast Recompress exports share the validated encoder mapping");
    }
    options.processing->videoCodec.rateMode = "lossless";
    options.outputPath = fixtures.directory.filePath("must-remain.mkv");
    if (!fixtures.writeText(options.outputPath, "existing output")) return false;
    passed &= check(!exporter.exportVideo(options) && exporter.lastError().contains("unsupported")
        && bytes(options.outputPath) == "existing output", "invalid saved encoder mode fails before output replacement");
    return passed;
}

bool legacyVfrClocks(VDQtTestFixtures& fixtures) {
    bool passed = true;
    const auto mpeg4Clock = VDQtCodecEngine::getVideoEncoderClock("mpeg4", 24);
    passed &= check(mpeg4Clock.numerator == 1 && mpeg4Clock.denominator == 60000,
        "MPEG-4 uses a representable 16-bit time-base denominator");
    for (const QString& id : {QString("mpeg1video"), QString("mpeg2video")}) {
        const auto clock = VDQtCodecEngine::getVideoEncoderClock(id, 24000.0 / 1001);
        passed &= check(clock.numerator == 1001 && clock.denominator == 24000,
            "MPEG-1/2 retain supported NTSC nominal clocks without forcing CFR output");
    }
    const QString source = fixtures.directory.filePath("legacy-vfr.mkv");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=64x48:rate=24", "-frames:v", "8",
        "-vf", "select='lt(n,4)+gte(n,4)*not(mod(n,3))'", "-fps_mode", "vfr", "-c:v", "ffv1", "-an", source})) return false;
    const auto times = [](const QString& path) {
        QList<double> result;
        QProcess process;
        process.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_frames",
            "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", path});
        if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
            process.kill(); process.waitForFinished(5000); return result;
        }
        for (const auto& frame : QJsonDocument::fromJson(process.readAllStandardOutput())
                .object().value("frames").toArray())
            result.append(frame.toObject().value("best_effort_timestamp_time").toString().toDouble());
        return result;
    };
    const QList<double> sourceTimes = times(source);
    VDQtVideoExporter exporter;
    VDQtVideoExporter::ExportOptions request;
    request.inputPath = source;
    request.endFrame = 7;
    request.includeAudio = false;
    request.unattended = true;
    request.containerType = "mkv";
    request.processing = VDQtVideoExporter::ProcessingSnapshot{};
    for (const QString& id : {QString("mpeg4"), QString("mpeg1video"), QString("mpeg2video")}) {
        if (!VDQtCodecEngine::instance().checkVideoEncoderAvailable(id)) continue;
        request.processing->videoCodec = VDQtCodecEngine::getDefaultVideoParamsForCodec(id);
        for (int mode : {VideoMode_FullProcessing, VideoMode_FastRecompress}) {
            request.videoMode = mode;
            request.outputPath = fixtures.directory.filePath(QString("%1-vfr-%2.mkv").arg(id).arg(mode));
            const bool encoded = exporter.exportVideo(request);
            if (!encoded) std::cerr << exporter.lastError().toStdString() << '\n';
            const auto outputTimes = encoded ? times(request.outputPath) : QList<double>{};
            bool retained = outputTimes.size() == sourceTimes.size() && sourceTimes.size() == 8;
            const auto clock = VDQtCodecEngine::getVideoEncoderClock(id, 24);
            const double tolerance = double(clock.numerator) / clock.denominator + 0.001;
            for (int index = 0; retained && index < sourceTimes.size(); ++index)
                retained = std::abs(outputTimes[index] - sourceTimes[index]) <= tolerance;
            passed &= check(encoded && retained,
                "legacy MPEG Full/Fast VFR export retains each picture and representable presentation timing");
        }
    }
    return passed;
}
}

int main(int argc, char **argv) {
    VDQtTestFixtures fixtures;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    qputenv("XDG_CONFIG_HOME", fixtures.directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixtures.directory.filePath("data").toUtf8());
    QApplication application(argc, argv);
    bool passed = policyContracts();
    passed &= dialogContracts();
    passed &= actualEncoding(fixtures);
    passed &= exportContracts(fixtures);
    passed &= legacyVfrClocks(fixtures);
    if (!passed && !fixtures.error.isEmpty()) std::cerr << fixtures.error.toStdString() << '\n';
    return passed ? 0 : 1;
}

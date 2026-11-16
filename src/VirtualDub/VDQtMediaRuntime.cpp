#include "VDQtMediaRuntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <dlfcn.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavdevice/avdevice.h>
#include <libpostproc/postprocess.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace {
struct Library {
    const char *name;
    unsigned (*version)();
    unsigned expectedVersion;
};
const Library libraries[] = {
    {"avcodec", avcodec_version, LIBAVCODEC_VERSION_INT},
    {"avformat", avformat_version, LIBAVFORMAT_VERSION_INT},
    {"avutil", avutil_version, LIBAVUTIL_VERSION_INT},
    {"avfilter", avfilter_version, LIBAVFILTER_VERSION_INT},
    {"swscale", swscale_version, LIBSWSCALE_VERSION_INT},
    {"swresample", swresample_version, LIBSWRESAMPLE_VERSION_INT},
    {"avdevice", avdevice_version, LIBAVDEVICE_VERSION_INT},
    {"postproc", postproc_version, LIBPOSTPROC_VERSION_INT}
};

QString versionText(unsigned value) {
    return QStringLiteral("%1.%2.%3").arg(value >> 16).arg((value >> 8) & 255).arg(value & 255);
}

QString libraryPath(const Library& library) {
    Dl_info location{};
    if (!dladdr(reinterpret_cast<void *>(library.version), &location) || !location.dli_fname)
        return {};
    return QFileInfo(QString::fromLocal8Bit(location.dli_fname)).canonicalFilePath();
}

bool fail(QString *error, const QString& reason) {
    if (error) *error = QStringLiteral("Bundled FFmpeg runtime error: %1\nSystem FFmpeg will not be used.").arg(reason);
    return false;
}
}

QString VDQtMediaRuntime::root() {
    // Resolve relative to the executable, never its current directory, PATH,
    // APPDIR or an inherited environment override. These are the only layouts.
    const QDir application(QCoreApplication::applicationDirPath());
    const QString adjacent = application.filePath(QStringLiteral("ffmpeg-runtime"));
    if (QFileInfo(adjacent).exists() || QFileInfo(adjacent).isSymLink())
        return QDir::cleanPath(adjacent);
    return QDir::cleanPath(application.filePath(QStringLiteral(VDQT_FFMPEG_INSTALLED_RELATIVE)));
}

QString VDQtMediaRuntime::ffmpeg() {
    // Return the expected absolute pathname even when missing. QProcess then
    // fails on this exact file; an empty/unqualified name could invite fallback.
    return QDir(root()).filePath(QStringLiteral("bin/ffmpeg"));
}

QString VDQtMediaRuntime::ffprobe() {
    return QDir(root()).filePath(QStringLiteral("bin/ffprobe"));
}

QString VDQtMediaRuntime::encoderProgram(const QString& configuredProgram) {
    const QString name = QFileInfo(configuredProgram).fileName();
    if (name == QStringLiteral("ffmpeg")) return ffmpeg();
    if (name == QStringLiteral("ffprobe")) return ffprobe();
    return configuredProgram;
}

bool VDQtMediaRuntime::validate(QString *errorMessage) {
    const QString canonicalRoot = QFileInfo(root()).canonicalFilePath();
    if (canonicalRoot.isEmpty()) return fail(errorMessage, QStringLiteral("missing directory %1").arg(root()));
    if (!QFileInfo::exists(QDir(root()).filePath(QStringLiteral("ffmpeg-runtime-manifest.json"))))
        return fail(errorMessage, QStringLiteral("missing build manifest in %1").arg(root()));
    // A matched FFmpeg version alone does not make an older assembly safe:
    // private libva/font/C++ runtimes can prevent host AVS plugins from loading.
    // Reject bundles predating the platform policy instead of reporting them
    // as supported and leaving the first script open to discover the conflict.
    QFile runtimeInfo(QDir(root()).filePath(QStringLiteral("runtime-build-info.json")));
    if (!runtimeInfo.open(QIODevice::ReadOnly))
        return fail(errorMessage, QStringLiteral("missing runtime compatibility metadata; reassemble the private runtime"));
    const QJsonObject buildInfo = QJsonDocument::fromJson(runtimeInfo.readAll()).object();
    const QString platformPatternText = buildInfo.value(QStringLiteral("host_platform_library_pattern")).toString();
    const QRegularExpression platformPattern(platformPatternText);
    if (buildInfo.value(QStringLiteral("platform_library_policy")).toString() != QStringLiteral("host-desktop-v1")
        || platformPatternText.isEmpty() || !platformPattern.isValid())
        return fail(errorMessage, QStringLiteral("bundle predates the host desktop/AviSynth plugin compatibility policy; reassemble the private runtime"));
    // Use the assembler's recorded policy so deployment and startup cannot
    // drift into separate hand-maintained lists. Catch partially updated bundles
    // too: a new marker must not hide leftovers from the former unsafe payload.
    QDirIterator runtimeLibraries(QDir(root()).filePath(QStringLiteral("lib")),
                                  QDir::Files, QDirIterator::Subdirectories);
    while (runtimeLibraries.hasNext()) {
        runtimeLibraries.next();
        if (platformPattern.match(runtimeLibraries.fileName()).hasMatch())
            return fail(errorMessage, QStringLiteral("unsafe bundled platform library %1; reassemble without host graphics/font/audio/C++ libraries")
                        .arg(runtimeLibraries.filePath()));
    }
    for (const auto& library : libraries) {
        const QString actual = libraryPath(library);
        const QString expected = QFileInfo(QDir(root()).filePath(
            QStringLiteral("lib/lib%1.so").arg(QString::fromLatin1(library.name)))).canonicalFilePath();
        if (actual.isEmpty() || expected.isEmpty() || actual != expected
            || !actual.startsWith(canonicalRoot + QLatin1Char('/')))
            return fail(errorMessage, QStringLiteral("lib%1 was loaded from %2, not the private bundle %3")
                        .arg(QString::fromLatin1(library.name), actual, canonicalRoot));
        if (library.version() != library.expectedVersion)
            return fail(errorMessage, QStringLiteral("lib%1 version %2 does not match build SDK %3")
                        .arg(QString::fromLatin1(library.name), versionText(library.version()), versionText(library.expectedVersion)));
    }
    for (const QString& tool : {ffmpeg(), ffprobe()}) {
        const QFileInfo file(tool);
        if (!file.isFile() || !file.isExecutable()
            || !file.canonicalFilePath().startsWith(canonicalRoot + QLatin1Char('/')))
            return fail(errorMessage, QStringLiteral("missing, unusable or external tool %1").arg(tool));
        QProcess process;
        process.start(tool, {QStringLiteral("-version")});
        if (!process.waitForStarted(3000) || !process.waitForFinished(5000)) {
            process.kill(); process.waitForFinished(1000);
            return fail(errorMessage, QStringLiteral("could not verify %1").arg(tool));
        }
        const QString output = QString::fromUtf8(process.readAllStandardOutput());
        const QString diagnostic = QString::fromUtf8(process.readAllStandardError());
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return fail(errorMessage, QStringLiteral("%1 cannot start: %2").arg(tool, diagnostic.left(4096)));
        const QString expectedHeading = QStringLiteral("%1 version %2 ")
            .arg(file.fileName(), QString::fromLatin1(av_version_info()));
        if (!output.startsWith(expectedHeading)
            || !output.contains(QStringLiteral("configuration: ") + QString::fromLatin1(avcodec_configuration())))
            return fail(errorMessage, QStringLiteral("%1 does not match the application's FFmpeg build").arg(tool));
        for (const auto& library : libraries) {
            const QRegularExpression pattern(QStringLiteral("lib%1\\s+(\\d+)\\.\\s*(\\d+)\\.\\s*(\\d+)\\s*/\\s*(\\d+)\\.\\s*(\\d+)\\.\\s*(\\d+)")
                .arg(QString::fromLatin1(library.name)));
            const auto match = pattern.match(output);
            if (!match.hasMatch()) return fail(errorMessage, QStringLiteral("%1 has no library version report").arg(tool));
            for (const int start : {1, 4}) {
                const unsigned value = (match.captured(start).toUInt() << 16)
                    | (match.captured(start + 1).toUInt() << 8) | match.captured(start + 2).toUInt();
                if (value != library.expectedVersion)
                    return fail(errorMessage, QStringLiteral("%1 uses a different lib%2 version").arg(tool, QString::fromLatin1(library.name)));
            }
        }
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

QString VDQtMediaRuntime::report() {
    QStringList lines{QStringLiteral("Bundled FFmpeg only (system fallback disabled)"),
        QStringLiteral("  Graphics/fonts/audio/C++ runtime: host platform (not system FFmpeg)"),
        QStringLiteral("  Version: %1").arg(QString::fromLatin1(av_version_info())),
        QStringLiteral("  Bundle: %1").arg(root()),
        QStringLiteral("  ffmpeg: %1").arg(ffmpeg()),
        QStringLiteral("  ffprobe: %1").arg(ffprobe())};
    for (const auto& library : libraries)
        lines << QStringLiteral("  lib%1 %2: %3").arg(QString::fromLatin1(library.name),
                    versionText(library.version()), libraryPath(library));
    return lines.join(QLatin1Char('\n'));
}

// Imported queues can contain source/mode combinations the interactive menus
// intentionally disallow. A failed explicit audio choice must fail the job,
// not silently replace it with the native video's default soundtrack.
#include <QString>
#include "support/VDQtTestFixtures.h"
#include "VirtualDub/VDQtMainWindow.h"
#include "VirtualDub/VDQtJobQueue.h"
#include "VirtualDub/VDQtWaveform.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTimer>
#include <QUuid>
#include <iostream>

namespace {
bool check(bool valid, const QString& description) {
    if (!valid) std::cerr << "FAIL: " << description.toStdString() << '\n';
    return valid;
}
QByteArray contents(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
VDQtJobState videoJob(const QString& source, const QString& output, const QString& name) {
    VDQtJobState job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.name = name;
    job.sourcePaths = {source};
    job.options.inputPath = source;
    job.options.outputPath = output;
    job.options.videoMode = VideoMode_DirectStreamCopy;
    job.options.audioMode = AudioMode_DirectStreamCopy;
    job.options.containerType = "mkv";
    return job;
}
}

int main(int argc, char **argv) {
    VDQtTestFixtures fixtures;
    if (!fixtures.directory.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", fixtures.directory.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", fixtures.directory.filePath("data").toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("VD_DISABLE_AUDIO_OUTPUT", "1");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("VDQtRegression");
    QCoreApplication::setApplicationName("SelectedAudioQueue");
    const QString source = fixtures.directory.filePath("native-video-audio.mkv");
    const QString unavailable = fixtures.directory.filePath("invalid-selected-audio.wav");
    if (!fixtures.ffmpeg({"-f", "lavfi", "-i", "testsrc2=size=64x48:rate=10:duration=0.4",
            "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.4",
            "-c:v", "ffv1", "-c:a", "pcm_s16le", source})
        || !fixtures.writeText(unavailable, "This owned file is not an audio container.\n")) return 3;
    const QByteArray originalSource = contents(source);
    const QByteArray originalOutput("EXISTING_OUTPUT_MUST_SURVIVE_FAILED_AUDIO_SELECTION");
    const QString sourceAlias = fixtures.directory.filePath("native-source-alias.mkv");
    if (!QFile::link(source, sourceAlias)) return 4;
    const QList<VDAudioFilterInstance> noFilters;
    VDQtAudioPlayer reference(false);
    const QString referenceWav = fixtures.directory.filePath("source-reference.wav");
    VDQtWaveformData referencePeaks;
    QString error;
    if (!reference.openFile(source) || !reference.hasAudio()
        || !reference.exportAudioToFile(referenceWav, 0, -1, {}, &noFilters, false)
        || !VDQtReadWaveformPeaks(referenceWav, 1, &referencePeaks, &error)
        || referencePeaks.peaks.isEmpty() || referencePeaks.peaks.first() <= 0) return 4;
    reference.close();
    QList<VDQtJobState> jobs;
    for (int index = 0; index < 3; ++index) {
        auto job = videoJob(source, fixtures.directory.filePath(QString("failure-%1.mkv").arg(index)),
            QString("invalid explicit selection %1").arg(index));
        job.replaceExisting = true;
        if (!fixtures.writeText(job.options.outputPath, originalOutput)) return 4;
        if (index == 1) job.audioStreamIndex = 0; // Stream zero is VIDEO, not audio.
        else job.audioSourcePath = unavailable;
        if (index == 2) job.options.audioMode = AudioMode_FullProcessing;
        jobs.append(job);
    }
    // Stale/unavailable audio metadata is deliberately ignored when the job
    // explicitly excludes audio. These controls prevent an overly broad guard.
    for (int index = 0; index < 2; ++index) {
        auto job = videoJob(source, fixtures.directory.filePath(QString("no-audio-%1.mkv").arg(index)),
            QString("disabled audio %1").arg(index));
        job.audioSourcePath = unavailable;
        if (index == 0) job.options.includeAudio = false;
        else job.audioDisabled = true;
        jobs.append(job);
    }
    // Explicitly choosing even the default embedded stream means Full audio
    // processing in the GUI. The queue must honor that intent too, including
    // an external pathname that is physically the same native source.
    for (int index = 0; index < 2; ++index) {
        auto job = videoJob(source, fixtures.directory.filePath(QString("selected-default-%1.mkv").arg(index)),
            QString("selected default with gain %1").arg(index));
        job.audioStreamIndex = 1;
        if (index == 1) job.audioSourcePath = sourceAlias;
        auto gain = VDQtAudioFilterSystem::instance().createFilter(VDAudioFilterType::Gain);
        gain.params["decibels"] = 6;
        job.processing.audioFilters = {gain};
        job.processing.audioCodec.codecId = "pcm_s16le";
        jobs.append(job);
    }
    const QString imported = fixtures.directory.filePath("imported.vdqjobs");
    if (!VDQtProjectFile::saveJobQueue(imported, jobs, &error)) {
        std::cerr << error.toStdString() << '\n'; return 5;
    }
    VDQtMainWindow window;
    window.setAutomationUnattended(true);
    window.show();
    auto *queue = window.findChild<VDQtJobQueue*>();
    if (!queue || !queue->replaceFromFile(imported, &error)) {
        std::cerr << error.toStdString() << '\n'; return 6;
    }
    int unexpected = 0;
    bool timedOut = false;
    QElapsedTimer deadline; deadline.start();
    QTimer responder; responder.setInterval(10);
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        if (deadline.elapsed() > 10000) {
            timedOut = true;
            QMetaObject::invokeMethod(&window, "abortCurrentJob", Qt::DirectConnection);
        }
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible()) continue;
            if (auto *message = qobject_cast<QMessageBox*>(widget)) {
                ++unexpected; message->reject();
            } else if (timedOut) {
                if (auto *progress = qobject_cast<QProgressDialog*>(widget)) progress->cancel();
            }
        }
    });
    responder.start();
    const bool invoked = QMetaObject::invokeMethod(&window, "runPendingJobs", Qt::DirectConnection);
    responder.stop();
    bool passed = check(invoked && !timedOut && !unexpected && !queue->isRunning()
        && queue->count() == jobs.size() && queue->persistenceError().isEmpty(),
        "imported queue finishes with durable state and no interactive dialogs");
    for (int index = 0; index < 3; ++index) {
        const auto *job = queue->jobAt(index);
        if (job && job->status != VDQtJobStatus::Failed)
            std::cerr << "Selection job " << index << " status="
                      << VDQtJobQueue::statusText(job->status).toStdString()
                      << " error=" << job->error.toStdString() << '\n';
        passed &= check(job && job->status == VDQtJobStatus::Failed
            && job->error.contains("selected audio", Qt::CaseInsensitive)
            && job->error.contains("opened", Qt::CaseInsensitive)
            && contents(jobs.at(index).options.outputPath) == originalOutput,
            QString("failed explicit audio selection %1 preserves destination instead of copying native audio").arg(index));
    }
    for (int index = 3; index < 5; ++index) {
        const auto *job = queue->jobAt(index);
        VDQtAudioPlayer output(false);
        passed &= check(job && job->status == VDQtJobStatus::Complete
            && output.openFile(job->options.outputPath) && !output.hasAudio(),
            QString("audio-disabled control %1 succeeds with no soundtrack").arg(index - 3));
    }
    for (int index = 5; index < jobs.size(); ++index) {
        const auto *job = queue->jobAt(index);
        VDQtAudioPlayer output(false);
        const QString decoded = fixtures.directory.filePath(QString("selected-default-%1.wav").arg(index - 5));
        VDQtWaveformData peaks;
        passed &= check(job && job->status == VDQtJobStatus::Complete
            && output.openFile(job->options.outputPath) && output.hasAudio()
            && output.exportAudioToFile(decoded, 0, -1, {}, &noFilters, false)
            && VDQtReadWaveformPeaks(decoded, 1, &peaks, &error)
            && peaks.sampleFrames == referencePeaks.sampleFrames && peaks.channels == referencePeaks.channels
            && !peaks.peaks.isEmpty() && peaks.peaks.first() > referencePeaks.peaks.first() * 1.9f
            && peaks.peaks.first() < referencePeaks.peaks.first() * 2.1f,
            QString("explicit default/aliased selection %1 follows GUI full-processing gain policy").arg(index - 5));
    }
    passed &= check(contents(source) == originalSource
        && contents(unavailable) == "This owned file is not an audio container.\n",
        "source and unavailable selection fixtures remain unchanged");
    window.close();
    return passed ? 0 : 1;
}

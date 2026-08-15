#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

#include "VDQtVideoDecoder.h"
#include "VDQtAudioPlayer.h"
#include "VDQtVideoExporter.h"
#include "VDQtCodecEngine.h"
#include "VDQtCodecSettings.h"
#include "VDQtFilterSystem.h"

struct TestCaseResult {
    QString testName;
    QString videoMode;
    QString videoCodec;
    QString audioMode;
    QString audioCodec;
    QString container;
    QString filterSetup;
    bool exportSuccess;
    bool validationSuccess;
    QString actualVideoCodec;
    QString actualAudioCodec;
    int actualWidth;
    int actualHeight;
    QString actualPixFmt;
    int actualAudioRate;
    int actualAudioChannels;
    qint64 fileSizeBytes;
    QString errorMessage;
};

struct ProbeResult {
    bool ok = false;
    QString vCodec;
    QString aCodec;
    int width = 0;
    int height = 0;
    QString pixFmt;
    int sampleRate = 0;
    int channels = 0;
    double duration = 0.0;
    QString error;
};

ProbeResult runFFprobe(const QString &filePath) {
    ProbeResult res;
    QProcess probe;
    probe.start("ffprobe", QStringList() << "-v" << "error"
                                        << "-show_entries" << "stream=codec_type,codec_name,width,height,pix_fmt,sample_rate,channels:format=duration"
                                        << "-of" << "json"
                                        << filePath);
    if (!probe.waitForFinished(5000)) {
        res.error = "ffprobe timeout or failed to run";
        return res;
    }

    QByteArray data = probe.readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        res.error = "Invalid JSON from ffprobe: " + QString::fromUtf8(probe.readAllStandardError());
        return res;
    }

    QJsonObject root = doc.object();
    QJsonArray streams = root["streams"].toArray();
    for (const QJsonValue &v : streams) {
        QJsonObject s = v.toObject();
        QString type = s["codec_type"].toString();
        if (type == "video" && res.vCodec.isEmpty()) {
            res.vCodec = s["codec_name"].toString();
            res.width = s["width"].toInt();
            res.height = s["height"].toInt();
            res.pixFmt = s["pix_fmt"].toString();
        } else if (type == "audio" && res.aCodec.isEmpty()) {
            res.aCodec = s["codec_name"].toString();
            res.sampleRate = s["sample_rate"].toString().toInt();
            res.channels = s["channels"].toInt();
        }
    }

    if (root.contains("format")) {
        res.duration = root["format"].toObject()["duration"].toString().toDouble();
    }

    res.ok = true;
    return res;
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::cout << "================================================================================" << std::endl;
    std::cout << "   VIRTUALDUB EXPORT & CODEC SUITE AUTOMATED VERIFICATION MATRIX" << std::endl;
    std::cout << "================================================================================" << std::endl;

    QString testDir = "/tmp/vd_test_suite_" + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(testDir);

    // 1. Prepare short test sources
    QString rawVideoSource = "/home/alan/Downloads/Myths.mp4";
    if (!QFile::exists(rawVideoSource)) {
        std::cerr << "Error: " << rawVideoSource.toStdString() << " not found!" << std::endl;
        return 1;
    }

    // Prepare a 2-second reference MP4 (with AAC) and MKV (with Opus)
    QString testMediaAac = testDir + "/src_aac.mp4";
    QString testMediaOpus = testDir + "/src_opus.mkv";
    QString testMediaAvs = testDir + "/src_script.avs";

    {
        QProcess p;
        p.start("ffmpeg", QStringList() << "-y" << "-ss" << "10" << "-t" << "2" << "-i" << rawVideoSource << "-c:v" << "libx264" << "-c:a" << "aac" << testMediaAac);
        p.waitForFinished(10000);
    }
    {
        QProcess p;
        p.start("ffmpeg", QStringList() << "-y" << "-ss" << "10" << "-t" << "2" << "-i" << rawVideoSource << "-c:v" << "libx264" << "-c:a" << "opus" << "-strict" << "-2" << testMediaOpus);
        p.waitForFinished(10000);
    }
    {
        QFile avs(testMediaAvs);
        if (avs.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&avs);
            out << "v = LWLibavVideoSource(\"" << testMediaAac << "\")\n";
            out << "a = LWLibavAudioSource(\"" << testMediaAac << "\", stream_index=1)\n";
            out << "AudioDub(v, a)\n";
            avs.close();
        }
    }

    std::vector<TestCaseResult> results;

    struct VideoCodecDef {
        QString id;
        QString name;
        int proresProfile = 3;
        int crf = 23;
        QString pixFmt = "yuv420p";
        QString expectedCodec = "h264";
    };

    std::vector<VideoCodecDef> videoCodecs = {
        { "(Uncompressed)", "Raw Video", 0, 0, "rgb24", "rawvideo" },
        { "libx264", "H.264 (8-bit)", 0, 22, "yuv420p", "h264" },
        { "libx264_10bit", "H.264 (10-bit)", 0, 20, "yuv420p10le", "h264" },
        { "libx265", "H.265 / HEVC", 0, 24, "yuv420p", "hevc" },
        { "libx265_lossless", "H.265 Lossless", 0, 0, "yuv420p", "hevc" },
        { "prores_ks", "ProRes HQ", 3, 0, "yuv422p10le", "prores" },
        { "prores_ks", "ProRes 4444", 4, 0, "yuv444p10le", "prores" },
        { "ffv1", "FFV1 Lossless", 0, 0, "yuv420p", "ffv1" },
        { "huffyuv", "HuffYUV", 0, 0, "yuv422p", "huffyuv" },
        { "cfhd", "CineForm HD", 0, 0, "yuv422p10le", "cfhd" },
        { "libvpx-vp9", "VP9", 0, 30, "yuv420p", "vp9" }
    };

    struct AudioCodecDef {
        int audioMode; // 0 = Direct, 1 = Full
        QString codecId;
        QString name;
        QString rateMode;
        int bitrateKbps;
        int vbrQuality;
        QString expectedCodec;
    };

    std::vector<AudioCodecDef> audioCodecs = {
        { AudioMode_DirectStreamCopy, "copy", "Direct Stream Copy", "cbr", 0, 0, "aac" },
        { AudioMode_FullProcessing, "aac", "AAC (256k)", "cbr", 256, 0, "aac" },
        { AudioMode_FullProcessing, "opus", "Opus (160k)", "vbr", 160, 0, "opus" },
        { AudioMode_FullProcessing, "ac3", "AC-3 (384k)", "cbr", 384, 0, "ac3" },
        { AudioMode_FullProcessing, "flac", "FLAC Lossless", "cbr", 0, 0, "flac" },
        { AudioMode_FullProcessing, "pcm_s16le", "PCM 16-bit", "cbr", 0, 0, "pcm_s16le" }
    };

    struct VideoModeDef {
        int mode;
        QString name;
    };

    std::vector<VideoModeDef> videoModes = {
        { VideoMode_DirectStreamCopy, "DirectStreamCopy" },
        { VideoMode_FastRecompress, "FastRecompress" },
        { VideoMode_NormalRecompress, "NormalRecompress" },
        { VideoMode_FullProcessing, "FullProcessing" }
    };

    struct TestConfig {
        QString name;
        QString inputPath;
        int videoMode;
        QString vCodec;
        int proresProfile;
        int crf;
        QString pixFmt;
        int audioMode;
        QString aCodec;
        QString aRateMode;
        int aBitrate;
        int aVbrQuality;
        QString container;
        bool addFilters;
        int expectedW;
        int expectedH;
        QString expectedVCodec;
        QString expectedACodec;
    };

    std::vector<TestConfig> tests;

    // Generate 4 x 11 x 6 = 264 combinations
    for (const auto &vm : videoModes) {
        for (const auto &vc : videoCodecs) {
            for (const auto &ac : audioCodecs) {
                TestConfig tc;
                tc.name = QString("%1_%2_%3").arg(vm.name).arg(vc.name.simplified().remove(' ').remove('/').remove('(').remove(')').remove('-').remove('.')).arg(ac.name.simplified().remove(' ').remove('/').remove('(').remove(')').remove('-').remove('.'));
                tc.inputPath = testMediaAac;
                tc.videoMode = vm.mode;
                tc.vCodec = (vm.mode == VideoMode_DirectStreamCopy) ? "copy" : vc.id;
                tc.proresProfile = vc.proresProfile;
                tc.crf = vc.crf;
                tc.pixFmt = vc.pixFmt;
                tc.audioMode = ac.audioMode;
                tc.aCodec = ac.codecId;
                tc.aRateMode = ac.rateMode;
                tc.aBitrate = ac.bitrateKbps;
                tc.aVbrQuality = ac.vbrQuality;
                tc.container = "mkv"; // MKV universal container supports all audio/video codecs
                tc.addFilters = (vm.mode == VideoMode_FullProcessing);
                tc.expectedW = (vm.mode == VideoMode_FullProcessing) ? 640 : 1280;
                tc.expectedH = (vm.mode == VideoMode_FullProcessing) ? 360 : 720;
                tc.expectedVCodec = (vm.mode == VideoMode_DirectStreamCopy) ? "h264" : vc.expectedCodec;
                tc.expectedACodec = ac.expectedCodec;

                tests.push_back(tc);
            }
        }
    }

    int passedCount = 0;
    int totalCount = tests.size();

    for (size_t i = 0; i < tests.size(); ++i) {
        const auto &tc = tests[i];
        TestCaseResult res;
        res.testName = tc.name;
        res.videoCodec = tc.vCodec;
        res.audioCodec = tc.aCodec;
        res.container = tc.container;

        if (tc.videoMode == VideoMode_DirectStreamCopy) res.videoMode = "DirectStreamCopy";
        else if (tc.videoMode == VideoMode_FastRecompress) res.videoMode = "FastRecompress";
        else if (tc.videoMode == VideoMode_NormalRecompress) res.videoMode = "NormalRecompress";
        else res.videoMode = "FullProcessing";

        res.audioMode = (tc.audioMode == AudioMode_DirectStreamCopy) ? "DirectStreamCopy" : "FullProcessing";

        std::cout << "[" << std::setw(2) << (i + 1) << "/" << totalCount << "] Testing: " << tc.name.toStdString() << " ... " << std::flush;

        // Configure Codec Engine
        VDVideoCodecParams vp;
        vp.codecId = tc.vCodec;
        vp.proresProfile = tc.proresProfile;
        vp.crf = tc.crf;
        vp.targetBitrateKbps = 0;
        vp.pixFmt = tc.pixFmt;
        VDQtCodecEngine::instance().setVideoParams(vp);

        VDAudioCodecParams ap;
        ap.codecId = tc.aCodec;
        ap.rateMode = tc.aRateMode;
        ap.bitrateKbps = tc.aBitrate;
        ap.vbrQuality = tc.aVbrQuality;
        VDQtCodecEngine::instance().setAudioParams(ap);

        // Configure Filters
        VDQtFilterSystem::instance().clearFilters();
        if (tc.addFilters) {
            VDQtFilterSystem::instance().addFilter(VDFilterType::Resize);
            QMap<QString, double> p;
            p["width"] = 640;
            p["height"] = 360;
            VDQtFilterSystem::instance().updateFilterParams(0, p);
            res.filterSetup = "Resize(640,360)";
        } else {
            res.filterSetup = "None";
        }

        // Open Source
        VDQtVideoDecoder dec;
        if (!dec.openFile(tc.inputPath)) {
            res.exportSuccess = false;
            res.validationSuccess = false;
            res.errorMessage = "Failed to open input: " + tc.inputPath;
            std::cout << "FAIL (decoder open)" << std::endl;
            results.push_back(res);
            continue;
        }
        int totalFrames = dec.getFrameCount();

        VDQtAudioPlayer aud;
        if (dec.isAvsNative()) {
            aud.openAvsClip(dec.getAvsClip(), dec.getAvsVi());
        } else {
            aud.openFile(tc.inputPath);
        }

        QString outExt = "." + tc.container;
        if (tc.container == "mov_faststart") outExt = ".mov";
        else if (tc.container == "mp4_faststart") outExt = ".mp4";
        QString outPath = QString("%1/out_%2%3").arg(testDir).arg(tc.name).arg(outExt);

        VDQtVideoExporter exporter;
        VDQtVideoExporter::ExportOptions opts;
        opts.inputPath = tc.inputPath;
        opts.outputPath = outPath;
        opts.startFrame = 0;
        opts.endFrame = std::min(4, totalFrames - 1);
        opts.videoMode = tc.videoMode;
        opts.audioMode = tc.audioMode;
        opts.containerType = tc.container;

        bool expOk = exporter.exportVideo(opts, &dec, &aud, nullptr, nullptr);
        res.exportSuccess = expOk;
        res.fileSizeBytes = QFileInfo(outPath).size();

        if (!expOk || res.fileSizeBytes == 0) {
            res.validationSuccess = false;
            res.errorMessage = "Export failed or generated empty file";
            std::cout << "FAIL (export failed)" << std::endl;
            results.push_back(res);
            aud.close();
            continue;
        }

        // Probe Output
        ProbeResult pr = runFFprobe(outPath);
        if (!pr.ok) {
            res.validationSuccess = false;
            res.errorMessage = "ffprobe failed: " + pr.error;
            std::cout << "FAIL (probe failed)" << std::endl;
            results.push_back(res);
            aud.close();
            continue;
        }

        res.actualVideoCodec = pr.vCodec;
        res.actualAudioCodec = pr.aCodec;
        res.actualWidth = pr.width;
        res.actualHeight = pr.height;
        res.actualPixFmt = pr.pixFmt;
        res.actualAudioRate = pr.sampleRate;
        res.actualAudioChannels = pr.channels;

        // Validation Checks
        bool valid = true;
        QString errs;

        // Check Video Codec
        if (!tc.expectedVCodec.isEmpty() && pr.vCodec.toLower() != tc.expectedVCodec.toLower()) {
            valid = false;
            errs += QString("Expected video codec '%1' but got '%2'; ").arg(tc.expectedVCodec).arg(pr.vCodec);
        }

        // Check Audio Codec
        if (!tc.expectedACodec.isEmpty() && pr.aCodec.toLower() != tc.expectedACodec.toLower()) {
            valid = false;
            errs += QString("Expected audio codec '%1' but got '%2'; ").arg(tc.expectedACodec).arg(pr.aCodec);
        }

        // Check Resolution (If Full Processing with Resize, must be 640x360. If Fast/Normal Recompress, must bypass Resize!)
        if (tc.videoMode == VideoMode_FullProcessing && tc.expectedW > 0) {
            if (pr.width != tc.expectedW || pr.height != tc.expectedH) {
                valid = false;
                errs += QString("Expected filtered resolution %1x%2 but got %3x%4; ").arg(tc.expectedW).arg(tc.expectedH).arg(pr.width).arg(pr.height);
            }
        } else if (tc.videoMode == VideoMode_FastRecompress || tc.videoMode == VideoMode_NormalRecompress) {
            // Must match original input resolution (bypassing the 640x360 filter!)
            if (pr.width == 640 && pr.height == 360) {
                valid = false;
                errs += QString("Fast/Normal Recompress failed to bypass filters (got resized %1x%2); ").arg(pr.width).arg(pr.height);
            }
        }

        res.validationSuccess = valid;
        res.errorMessage = errs;

        if (valid) {
            std::cout << "PASS (Video: " << pr.vCodec.toStdString() << " " << pr.width << "x" << pr.height << " | Audio: " << pr.aCodec.toStdString() << ")" << std::endl;
            passedCount++;
        } else {
            std::cout << "FAIL: " << errs.toStdString() << std::endl;
        }

        results.push_back(res);
        aud.close();
        dec.close();
    }

    std::cout << "================================================================================" << std::endl;
    std::cout << "   TEST SUMMARY: " << passedCount << " / " << totalCount << " PASSED" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // Output Markdown summary table to stdout for easy reading
    std::cout << "\n\n| Test Case | Video Mode | Video Codec (Exp/Act) | Audio Mode | Audio Codec (Exp/Act) | Resolution | Result |" << std::endl;
    std::cout << "| :--- | :--- | :--- | :--- | :--- | :--- | :--- |" << std::endl;
    for (const auto &r : results) {
        std::string status = r.validationSuccess ? "**PASS**" : "**FAIL**";
        std::cout << "| " << r.testName.toStdString()
                  << " | " << r.videoMode.toStdString()
                  << " | " << r.videoCodec.toStdString() << " / " << r.actualVideoCodec.toStdString()
                  << " | " << r.audioMode.toStdString()
                  << " | " << r.audioCodec.toStdString() << " / " << r.actualAudioCodec.toStdString()
                  << " | " << r.actualWidth << "x" << r.actualHeight
                  << " | " << status << " |" << std::endl;
    }

    return (passedCount == totalCount) ? 0 : 1;
}

// Typed saved codec controls must reject malformed present values rather than
// silently selecting defaults. Omitted legacy fields still keep their defaults.
#include "VirtualDub/VDQtProjectFile.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool check(bool condition, const QString& message) {
    if (!condition) std::cerr << "FAIL: " << message.toStdString() << '\n';
    return condition;
}
bool write(const QString& path, const QJsonObject& object) {
    QFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    const QString path = directory.filePath("codec-settings.vdqsettings");
    QString error;
    if (!directory.isValid()
        || !VDQtProjectFile::saveProcessingSettings(path, VDQtProcessingState(), &error)) return 2;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 2;
    const QJsonObject baseline = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    bool passed = true;
    const QList<QJsonValue> wrongStrings = {
        QJsonValue(QJsonValue::Null), true, 42, QJsonArray(), QJsonObject()
    };
    const QStringList videoStrings = {"codecId", "rateMode", "preset", "tune", "profile",
        "pixelFormat", "colorMatrix", "proresVendor"};
    const QStringList audioStrings = {"codecId", "rateMode"};
    for (const QString& codec : {QString("videoCodec"), QString("audioCodec")}) {
        const QStringList members = codec == "videoCodec" ? videoStrings : audioStrings;
        for (const QString& member : members) {
            for (const QJsonValue& wrong : wrongStrings) {
                QJsonObject root = baseline;
                QJsonObject processing = root["processing"].toObject();
                QJsonObject config = processing[codec].toObject();
                config[member] = wrong;
                processing[codec] = config;
                root["processing"] = processing;
                if (!write(path, root)) return 2;
                VDQtProcessingState destination;
                destination.videoCodec.codecId = "untouched";
                const bool loaded = VDQtProjectFile::loadProcessingSettings(path, &destination, &error);
                passed &= check(!loaded && !error.isEmpty()
                    && destination.videoCodec.codecId == "untouched",
                    codec + QLatin1Char('.') + member + " rejects a non-string atomically");
            }
            QJsonObject root = baseline;
            QJsonObject processing = root["processing"].toObject();
            QJsonObject config = processing[codec].toObject();
            config[member] = QString(129, 'x');
            processing[codec] = config;
            root["processing"] = processing;
            if (!write(path, root)) return 2;
            VDQtProcessingState destination;
            passed &= check(!VDQtProjectFile::loadProcessingSettings(path, &destination, &error),
                codec + QLatin1Char('.') + member + " rejects unreasonably long controls");
        }
        for (const QJsonValue& wrong : {QJsonValue(QJsonValue::Null), QJsonValue(true), QJsonValue(42),
             QJsonValue(QJsonArray()), QJsonValue("not an object")}) {
            QJsonObject root = baseline;
            QJsonObject processing = root["processing"].toObject();
            processing[codec] = wrong;
            root["processing"] = processing;
            if (!write(path, root)) return 2;
            VDQtProcessingState destination;
            passed &= check(!VDQtProjectFile::loadProcessingSettings(path, &destination, &error),
                            codec + " rejects a present non-object configuration");
        }
    }
    for (const QJsonValue& wrong : {QJsonValue(QJsonValue::Null), QJsonValue(1), QJsonValue("true"),
         QJsonValue(QJsonArray()), QJsonValue(QJsonObject())}) {
        QJsonObject root = baseline;
        QJsonObject processing = root["processing"].toObject();
        QJsonObject codec = processing["videoCodec"].toObject();
        codec["twoPass"] = wrong;
        processing["videoCodec"] = codec;
        root["processing"] = processing;
        if (!write(path, root)) return 2;
        VDQtProcessingState destination;
        passed &= check(!VDQtProjectFile::loadProcessingSettings(path, &destination, &error),
                        "twoPass rejects a present non-boolean");
    }
    QJsonObject legacy = baseline;
    QJsonObject processing = legacy["processing"].toObject();
    processing.remove("videoCodec");
    processing.remove("audioCodec");
    legacy["processing"] = processing;
    if (!write(path, legacy)) return 2;
    VDQtProcessingState restored;
    passed &= check(VDQtProjectFile::loadProcessingSettings(path, &restored, &error)
        && restored.videoCodec.codecId == VDVideoCodecParams().codecId
        && restored.audioCodec.codecId == VDAudioCodecParams().codecId,
        "omitted legacy codec configurations preserve established defaults");
    return passed ? 0 : 1;
}

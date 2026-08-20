#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <algorithm>
#include "VirtualDub/VDQtMainWindow.h"

namespace {

QString syliaString(const QString& value) {
    QString escaped;
    escaped.reserve(value.size() + 8);
    for (const QChar character : value) {
        if (character == QLatin1Char('\\') || character == QLatin1Char('"'))
            escaped += QLatin1Char('\\');
        if (character == QLatin1Char('\n')) escaped += QStringLiteral("\\n");
        else if (character == QLatin1Char('\r')) escaped += QStringLiteral("\\r");
        else escaped += character;
    }
    return escaped;
}

bool isOption(const QString& value, const QStringList& spellings) {
    for (const QString& spelling : spellings) {
        if (value.compare(spelling, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

void printUsage() {
    QTextStream output(stdout);
    output
        << "VirtualDubQt\n"
        << "  VirtualDubQt [video]\n"
        << "  VirtualDubQt --script <file> [--exit]\n"
        << "  VirtualDubQt --command <Sylia-call> [--exit]\n"
        << "  VirtualDubQt --save-avi <source|*> <destination> [--exit]\n"
        << "  VirtualDubQt --analysis <source|*> [--exit]\n"
        << "  VirtualDubQt --external-encoder <source|*> <destination> <set> [--exit]\n"
        << "\nCompatible aliases: /s, /cmd, /SaveAVI, /x\n"
        << "--exit/--no-ui runs without leaving the editor window open.\n";
}

} // namespace

int main(int argc, char *argv[]) {
    QStringList startupArguments;
    for (int index = 1; index < argc; ++index)
        startupArguments.append(QString::fromLocal8Bit(argv[index]));
    const bool wantsHelp = std::any_of(
        startupArguments.cbegin(), startupArguments.cend(), [](const QString& value) {
            return isOption(value, {QStringLiteral("--help"), QStringLiteral("-h"),
                                    QStringLiteral("/help"), QStringLiteral("/?")});
        });
    if (wantsHelp) {
        printUsage();
        return 0;
    }
    const bool unattended = std::any_of(
        startupArguments.cbegin(), startupArguments.cend(), [](const QString& value) {
            return isOption(value, {QStringLiteral("--exit"), QStringLiteral("--no-ui"),
                                    QStringLiteral("-x"), QStringLiteral("/x")});
        });
    if (unattended && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    app.setApplicationName("VirtualDub");
    app.setOrganizationName("VirtualDub Port");

    VDQtMainWindow w;
    w.setAutomationUnattended(unattended);
    bool performedAction = false;
    QString error;
    for (int index = 0; index < startupArguments.size(); ++index) {
        const QString argument = startupArguments.at(index);
        if (isOption(argument, {QStringLiteral("--exit"), QStringLiteral("--no-ui"),
                                QStringLiteral("-x"), QStringLiteral("/x")})) {
            continue;
        }
        if (isOption(argument, {QStringLiteral("--help"), QStringLiteral("-h"),
                                QStringLiteral("/help"), QStringLiteral("/?")})) {
            printUsage();
            return 0;
        }
        if (isOption(argument, {QStringLiteral("--script"), QStringLiteral("-s"),
                                QStringLiteral("/s")})) {
            if (++index >= startupArguments.size()) {
                QTextStream(stderr) << "Missing script path after " << argument << "\n";
                return 2;
            }
            performedAction = true;
            if (!w.runAutomationScript(startupArguments.at(index), &error)) {
                QTextStream(stderr) << error << '\n';
                return 1;
            }
            continue;
        }
        if (isOption(argument, {QStringLiteral("--command"), QStringLiteral("--cmd"),
                                QStringLiteral("/cmd")})) {
            if (++index >= startupArguments.size()) {
                QTextStream(stderr) << "Missing Sylia command after " << argument << "\n";
                return 2;
            }
            QString command = startupArguments.at(index).trimmed();
            if (!command.endsWith(QLatin1Char(';'))) command += QLatin1Char(';');
            performedAction = true;
            if (!w.runAutomationText(command, QDir::currentPath(), &error)) {
                QTextStream(stderr) << error << '\n';
                return 1;
            }
            continue;
        }
        if (isOption(argument, {QStringLiteral("--save-avi"),
                                QStringLiteral("/SaveAVI"), QStringLiteral("/p")})) {
            if (index + 2 >= startupArguments.size()) {
                QTextStream(stderr) << argument
                    << " requires a source and destination path\n";
                return 2;
            }
            const QString source = startupArguments.at(++index);
            const QString destination = startupArguments.at(++index);
            if (source != QLatin1String("*") && !w.openVideoFile(source)) {
                QTextStream(stderr) << "Could not open source: " << source << '\n';
                return 1;
            }
            const QString command = QStringLiteral("VirtualDub.SaveAVI(\"%1\");")
                .arg(syliaString(QFileInfo(destination).absoluteFilePath()));
            performedAction = true;
            if (!w.runAutomationText(command, QDir::currentPath(), &error)) {
                QTextStream(stderr) << error << '\n';
                return 1;
            }
            continue;
        }
        if (isOption(argument, {QStringLiteral("--analysis"),
                                QStringLiteral("/RunNullVideoPass")})) {
            if (++index >= startupArguments.size()) {
                QTextStream(stderr) << argument << " requires a source path\n";
                return 2;
            }
            const QString source = startupArguments.at(index);
            if (source != QLatin1String("*") && !w.openVideoFile(source)) {
                QTextStream(stderr) << "Could not open source: " << source << '\n';
                return 1;
            }
            performedAction = true;
            if (!w.runAutomationText(
                    QStringLiteral("VirtualDub.RunNullVideoPass();"),
                    QDir::currentPath(), &error)) {
                QTextStream(stderr) << error << '\n';
                return 1;
            }
            continue;
        }
        if (isOption(argument, {QStringLiteral("--external-encoder"),
                                QStringLiteral("/ExportViaEncoderSet")})) {
            if (index + 3 >= startupArguments.size()) {
                QTextStream(stderr) << argument
                    << " requires a source, destination, and encoder-set name\n";
                return 2;
            }
            const QString source = startupArguments.at(++index);
            const QString destination = startupArguments.at(++index);
            const QString setName = startupArguments.at(++index);
            if (source != QLatin1String("*") && !w.openVideoFile(source)) {
                QTextStream(stderr) << "Could not open source: " << source << '\n';
                return 1;
            }
            const QString command = QStringLiteral(
                "VirtualDub.ExportViaEncoderSet(\"%1\", \"%2\");")
                .arg(syliaString(QFileInfo(destination).absoluteFilePath()),
                     syliaString(setName));
            performedAction = true;
            if (!w.runAutomationText(command, QDir::currentPath(), &error)) {
                QTextStream(stderr) << error << '\n';
                return 1;
            }
            continue;
        }
        if (argument.startsWith(QLatin1Char('-'))
            || (argument.startsWith(QLatin1Char('/'))
                && !QFileInfo::exists(argument))) {
            QTextStream(stderr) << "Unknown option: " << argument << '\n';
            return 2;
        }
        performedAction = true;
        if (!w.openVideoFile(argument)) {
            QTextStream(stderr) << "Could not open source: " << argument << '\n';
            return 1;
        }
    }

    if (w.automationExitRequested()) return w.automationExitCode();
    if (unattended) return 0;
    Q_UNUSED(performedAction);
    w.show();
    return app.exec();
}

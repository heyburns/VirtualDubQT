#include "VDQtAviSynth.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <dlfcn.h>

namespace VDQtAviSynth {
namespace {

// Resolve the whole table before publishing it. A runtime missing one
// required entry cannot safely serve any of the native script consumers.
const char *resolve(void *library, Api& functions) {
#define VDQT_AVS_FUNCTION(name) \
    functions.name = reinterpret_cast<decltype(functions.name)>(dlsym(library, #name)); \
    if (!functions.name) return #name;
#include "VDQtAviSynthFunctions.inc"
#undef VDQT_AVS_FUNCTION
    return nullptr;
}

struct Runtime {
    Api functions;
    QString error;
    void *library = nullptr;

    Runtime() {
        // AviSynth uses its interface version as the Linux SONAME major. Use
        // the same major as the C headers since AVS_Value crosses this boundary.
        // An unversioned linker alias can lead to a different installed ABI.
        const QString soname = QStringLiteral("libavisynth.so.%1").arg(AVISYNTH_INTERFACE_VERSION);
        const QDir local(QDir::home().filePath(QStringLiteral(".local")));
        QStringList candidates{soname};
        for (const auto& directory : {QStringLiteral("lib64/"), QStringLiteral("lib/")}) {
            const auto path = local.filePath(directory + soname);
            if (QFileInfo::exists(path)) candidates.append(path);
        }

        QStringList details;
        for (const auto& candidate : candidates) {
            void *handle = dlopen(QFile::encodeName(candidate).constData(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
                const char *reason = dlerror();
                if (reason) details.append(QString::fromLocal8Bit(reason));
                continue;
            }
            Api resolved;
            if (const char *missing = resolve(handle, resolved)) {
                details.append(QStringLiteral("%1 is missing %2.")
                                   .arg(candidate, QString::fromLatin1(missing)));
                dlclose(handle);
                continue;
            }
            functions = resolved;
            library = handle;
            return;
        }

        details.removeDuplicates();
        error = QStringLiteral("AviSynth+ could not be loaded. Install the native Linux "
                               "AviSynth+ runtime providing %1 and restart VirtualDubQT.").arg(soname);
        if (!details.isEmpty()) error += QStringLiteral("\n\n") + details.join(QLatin1Char('\n'));
    }
};

const Runtime& runtime() {
    // C++ initializes this once, including when workers first open scripts
    // concurrently. The handle is deliberately never closed after success.
    static const Runtime loaded;
    return loaded;
}

} // namespace

bool ensureLoaded(QString *error) {
    const auto& loaded = runtime();
    if (error) *error = loaded.error;
    return loaded.library != nullptr;
}

const Api& api() {
    return runtime().functions;
}

} // namespace VDQtAviSynth

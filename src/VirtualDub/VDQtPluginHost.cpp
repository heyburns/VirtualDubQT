#include "VDQtPluginHost.h"

#include <vd2/system/vdtypes.h>
#include <vd2/plugin/vdvideofilt.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr int kMaximumPluginDimension = 32768;
constexpr qsizetype kMaximumPluginFrameBytes = qsizetype{1024} * 1024 * 1024;

struct PluginDefinition;

struct ModuleToken {
    alignas(std::max_align_t) unsigned char storage[64] = {};
};

struct PluginModule {
    QString path;
    std::unique_ptr<QLibrary> library;
    ModuleToken token;
    VDXFilterModuleDeinitProc deinitProc = nullptr;
    QList<QSharedPointer<PluginDefinition>> filters;
    QStringList metadata;
    int apiVersion = 0;
    bool initialized = false;
};

struct PluginDefinition {
    VDXFilterDefinition definition = {};
    FilterModDefinition filterMod = {};
    bool hasFilterMod = false;
    QSharedPointer<PluginModule> module;
    VDQtPluginFilterInfo info;
};

struct DiscoveryContext {
    QSharedPointer<PluginModule> module;
    QList<QSharedPointer<PluginDefinition>> definitions;
    int nextIndex = 0;
};

thread_local DiscoveryContext *gDiscoveryContext = nullptr;

QString pluginIdentifier(const QString& modulePath, const QString& name, int index) {
    const QByteArray material = QFileInfo(modulePath).absoluteFilePath().toUtf8()
        + '\n' + name.toUtf8() + '\n' + QByteArray::number(index);
    return QStringLiteral("vdx:")
        + QString::fromLatin1(QCryptographicHash::hash(
              material, QCryptographicHash::Sha256).toHex());
}

VDXFilterDefinition *registerFilter(VDXFilterDefinition *definition,
                                    int definitionBytes,
                                    FilterModDefinition *filterMod,
                                    int filterModBytes) {
    if (!gDiscoveryContext || !definition || definitionBytes <= 0)
        return nullptr;

    auto registered = QSharedPointer<PluginDefinition>::create();
    const size_t bytes = std::min<size_t>(
        static_cast<size_t>(definitionBytes), sizeof(VDXFilterDefinition));
    std::memcpy(&registered->definition, definition, bytes);
    if (!registered->definition.name || !registered->definition.runProc)
        return nullptr;

    if (filterMod && filterModBytes > 0) {
        const size_t modBytes = std::min<size_t>(
            static_cast<size_t>(filterModBytes), sizeof(FilterModDefinition));
        std::memcpy(&registered->filterMod, filterMod, modBytes);
        registered->hasFilterMod = true;
        registered->definition.fm = &registered->filterMod;
    } else {
        registered->definition.fm = nullptr;
    }

    registered->module = gDiscoveryContext->module;
    const QString name = QString::fromLocal8Bit(registered->definition.name);
    registered->info.id = pluginIdentifier(
        registered->module->path, name, gDiscoveryContext->nextIndex++);
    registered->info.name = name;
    registered->info.description = registered->definition.desc
        ? QString::fromLocal8Bit(registered->definition.desc) : QString();
    registered->info.author = registered->definition.maker
        ? QString::fromLocal8Bit(registered->definition.maker) : QString();
    registered->info.modulePath = registered->module->path;
    registered->info.apiVersion = registered->module->apiVersion;
    registered->info.hasNativeConfiguration = false;
    gDiscoveryContext->definitions.append(registered);
    return &registered->definition;
}

VDXFilterDefinition *__cdecl addLegacyFilter(
    VDXFilterModule *, VDXFilterDefinition *definition, int definitionBytes) {
    return registerFilter(definition, definitionBytes, nullptr, 0);
}

VDXFilterDefinition *__cdecl addFilterMod(
    VDXFilterModule *, VDXFilterDefinition *definition, int definitionBytes,
    FilterModDefinition *filterMod, int filterModBytes) {
    return registerFilter(definition, definitionBytes, filterMod, filterModBytes);
}

void __cdecl removeLegacyFilter(VDXFilterDefinition *) {
    // Modules are unloaded as a unit, so individual removal is unnecessary.
}

bool __cdecl isFpuEnabled() { return true; }

bool __cdecl isMmxEnabled() {
#if defined(__i386__) || defined(__x86_64__)
    return true;
#else
    return false;
#endif
}

void __cdecl initializeLegacyVTables(VDXFilterVTbls *) {
    // VDXBitmap's useful helpers are non-virtual in current headers. Very old
    // modules that call through the pre-1.2 private VBitmap vtable are rejected
    // by normal API negotiation and do not reach this compatibility host.
}

[[noreturn]] void __cdecl throwOutOfMemory() { throw std::bad_alloc(); }

[[noreturn]] void __cdecl throwPluginError(const char *format, ...) {
    char buffer[2048] = {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer, sizeof buffer, format ? format : "Plugin error", arguments);
    va_end(arguments);
    throw std::runtime_error(buffer);
}

long __cdecl cpuFlags() {
    long flags = CPUF_SUPPORTS_FPU;
#if defined(__i386__) || defined(__x86_64__)
    flags |= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_MMX;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_cpu_supports("sse")) flags |= CPUF_SUPPORTS_SSE;
    if (__builtin_cpu_supports("sse2")) flags |= CPUF_SUPPORTS_SSE2;
    if (__builtin_cpu_supports("sse3")) flags |= CPUF_SUPPORTS_SSE3;
    if (__builtin_cpu_supports("ssse3")) flags |= CPUF_SUPPORTS_SSSE3;
    if (__builtin_cpu_supports("sse4.1")) flags |= CPUF_SUPPORTS_SSE41;
    if (__builtin_cpu_supports("avx")) flags |= CPUF_SUPPORTS_AVX;
    if (__builtin_cpu_supports("avx2")) flags |= CPUF_SUPPORTS_AVX2;
#else
    flags |= CPUF_SUPPORTS_SSE | CPUF_SUPPORTS_SSE2;
#endif
#endif
    return flags;
}

long __cdecl hostVersion(char *buffer, int length) {
    constexpr long version = 20000;
    if (buffer && length > 0)
        std::snprintf(buffer, static_cast<size_t>(length),
                      "VirtualDubQt Linux compatibility host");
    return version;
}

VDXFilterFunctions gFilterFunctions = {
    addLegacyFilter,
    removeLegacyFilter,
    isFpuEnabled,
    isMmxEnabled,
    initializeLegacyVTables,
    throwOutOfMemory,
    throwPluginError,
    cpuFlags,
    hostVersion
};

FilterModInitFunctions gFilterModFunctions = { addFilterMod };

class FilterModPixmapProvider final : public IFilterModPixmap {
public:
    const VDXPixmap *source = nullptr;
    const VDXPixmap *destination = nullptr;
    FilterModPixmapInfo sourceInfo;
    FilterModPixmapInfo destinationInfo;

    FilterModPixmapInfo *GetPixmapInfo(const VDXPixmap *pixmap) override {
        return pixmap == destination ? &destinationInfo : &sourceInfo;
    }

    uint64 GetFormat_XRGB64() override {
        return static_cast<uint64>(vd2::kPixFormat_XRGB64);
    }
};

struct AlignedImage {
    QByteArray bytes;
    QImage image;

    bool allocate(int width, int height, int alignment) {
        if (width <= 0 || height <= 0 || width > kMaximumPluginDimension
            || height > kMaximumPluginDimension)
            return false;
        alignment = std::clamp(alignment, 4, 64);
        const qint64 rowBytes = static_cast<qint64>(width) * 4;
        const qint64 stride = (rowBytes + alignment - 1) & ~(alignment - 1);
        const qint64 allocation = stride * height + alignment;
        if (allocation <= 0 || allocation > kMaximumPluginFrameBytes)
            return false;
        bytes.resize(static_cast<qsizetype>(allocation));
        const quintptr base = reinterpret_cast<quintptr>(bytes.data());
        uchar *aligned = reinterpret_cast<uchar *>(
            (base + static_cast<quintptr>(alignment - 1))
            & ~static_cast<quintptr>(alignment - 1));
        image = QImage(aligned, width, height, static_cast<qsizetype>(stride),
                       QImage::Format_ARGB32);
        return !image.isNull();
    }

    void copyFrom(const QImage& source) {
        const int bytesPerRow = std::min(source.width(), image.width()) * 4;
        const int rows = std::min(source.height(), image.height());
        for (int y = 0; y < rows; ++y)
            std::memcpy(image.scanLine(y), source.constScanLine(y),
                        static_cast<size_t>(bytesPerRow));
    }
};

void fillBitmap(VDXFBitmap& bitmap, VDXPixmapLayout& layout,
                VDXPixmap& pixmap, QImage& image, sint64 frameNumber) {
    std::memset(&bitmap, 0, sizeof bitmap);
    std::memset(&layout, 0, sizeof layout);
    std::memset(&pixmap, 0, sizeof pixmap);
    const int stride = image.bytesPerLine();
    bitmap.data = reinterpret_cast<uint32 *>(
        image.bits() + static_cast<qsizetype>(image.height() - 1) * stride);
    bitmap.depth = 0;
    bitmap.w = image.width();
    bitmap.h = image.height();
    bitmap.pitch = -stride;
    bitmap.modulo = bitmap.pitch - static_cast<ptrdiff_t>(bitmap.w) * 4;
    bitmap.size = static_cast<ptrdiff_t>(stride) * bitmap.h;
    bitmap.offset = 0;
    bitmap.mFrameRateHi = 0;
    bitmap.mFrameRateLo = 0;
    bitmap.mFrameCount = -1;
    bitmap.mFrameNumber = frameNumber;

    layout.data = 0;
    layout.w = image.width();
    layout.h = image.height();
    layout.pitch = stride;
    layout.format = vd2::kPixFormat_XRGB8888;

    pixmap.data = image.bits();
    pixmap.w = image.width();
    pixmap.h = image.height();
    pixmap.pitch = stride;
    pixmap.format = vd2::kPixFormat_XRGB8888;
    bitmap.mpPixmapLayout = &layout;
    bitmap.mpPixmap = &pixmap;
}

class VideoFilterRuntime {
public:
    explicit VideoFilterRuntime(QSharedPointer<PluginDefinition> pluginDefinition)
        : plugin(std::move(pluginDefinition)) {}

    ~VideoFilterRuntime() { shutdown(); }

    bool matches(const QByteArray& configuration, const QImage& input) const {
        return initialized && serializedConfiguration == configuration
            && sourceWidth == input.width() && sourceHeight == input.height();
    }

    bool initialize(const QByteArray& configuration, const QImage& input,
                    QString *errorMessage) {
        shutdown();
        if (!plugin || input.isNull()) return setError(
            errorMessage, QStringLiteral("The plugin or source frame is unavailable."));
        if (plugin->definition.mSourceCountLowMinus1 > 0
            || plugin->definition.mSourceCountHighMinus1 > 0) {
            return setError(errorMessage,
                QStringLiteral("This plugin requires multiple video inputs, which this host cannot supply."));
        }

        sourceWidth = input.width();
        sourceHeight = input.height();
        serializedConfiguration = configuration;
        try {
            const int stateBytes = std::max(0, plugin->definition.inst_data_size);
            filterData.resize(static_cast<size_t>(stateBytes));
            std::fill(filterData.begin(), filterData.end(), std::byte{});
            filterMod.filter = &plugin->definition;
            filterMod.filterMod = plugin->hasFilterMod ? &plugin->filterMod : nullptr;
            filterMod.filter_data = stateBytes ? filterData.data() : nullptr;
            filterMod.fmpreview = nullptr;
            filterMod.fmtimeline = nullptr;
            filterMod.fmsystem = nullptr;
            filterMod.fmpixmap = &pixmapProvider;
            filterMod.fmproject = nullptr;

            sourceFrames[0] = &sourceBitmap;
            outputFrames[0] = &destinationBitmap;
            sourceStreams[0] = &sourceBitmap;
            activation = std::make_unique<VDXFilterActivation>(
                VDXFilterActivation{
                    &plugin->definition,
                    stateBytes ? filterData.data() : nullptr,
                    destinationBitmap,
                    sourceBitmap,
                    nullptr,
                    &lastBitmap,
                    0, 0, 0, 0,
                    &stateInfo,
                    nullptr,
                    nullptr,
                    1,
                    sourceFrames,
                    outputFrames,
                    nullptr,
                    1,
                    sourceStreams,
                    &filterMod
                });

            if (plugin->definition.initProc
                && plugin->definition.initProc(activation.get(), &gFilterFunctions)) {
                return setError(errorMessage,
                                QStringLiteral("The plugin rejected initialization."));
            }
            initCalled = true;
            if (plugin->hasFilterMod && plugin->filterMod.activateProc)
                plugin->filterMod.activateProc(&filterMod, &gFilterFunctions);
            if (!configuration.isEmpty() && plugin->definition.deserializeProc) {
                plugin->definition.deserializeProc(
                    activation.get(), &gFilterFunctions,
                    configuration.constData(), configuration.size());
            }

            if (!prepareBuffers(input, errorMessage)) return false;
            if (plugin->definition.startProc
                && plugin->definition.startProc(activation.get(), &gFilterFunctions)) {
                return setError(errorMessage,
                                QStringLiteral("The plugin could not start processing."));
            }
            started = true;
            initialized = true;
            return true;
        } catch (const std::exception& error) {
            shutdown();
            return setError(errorMessage, QString::fromLocal8Bit(error.what()));
        } catch (...) {
            shutdown();
            return setError(errorMessage,
                            QStringLiteral("The plugin raised an unknown exception."));
        }
    }

    bool process(const QImage& input, QImage *output, QString *errorMessage) {
        if (!initialized || !activation || !output)
            return setError(errorMessage, QStringLiteral("The plugin is not initialized."));
        try {
            const QImage converted = input.convertToFormat(QImage::Format_ARGB32);
            sourceImage.copyFrom(converted);
            if (swapBuffers) destinationImage.image.fill(Qt::transparent);
            else destinationImage.copyFrom(converted);
            updateFrameBindings();
            stateInfo.lCurrentFrame = static_cast<sint32>(
                std::min<sint64>(frameNumber, std::numeric_limits<sint32>::max()));
            stateInfo.lCurrentSourceFrame = stateInfo.lCurrentFrame;
            stateInfo.mOutputFrame = stateInfo.lCurrentFrame;
            sourceBitmap.mFrameNumber = frameNumber;
            destinationBitmap.mFrameNumber = frameNumber;
            stateInfo.flags = kVDXVFEvent_None;
            if (plugin->definition.runProc
                && plugin->definition.runProc(activation.get(), &gFilterFunctions)) {
                return setError(errorMessage,
                                QStringLiteral("The plugin reported a frame-processing failure."));
            }
            *output = swapBuffers ? destinationImage.image.copy()
                                  : sourceImage.image.copy();
            if (needsLast) {
                lastImage.copyFrom(converted);
                fillBitmap(lastBitmap, lastLayout, lastPixmap,
                           lastImage.image, frameNumber);
            }
            ++frameNumber;
            return !output->isNull();
        } catch (const std::exception& error) {
            return setError(errorMessage, QString::fromLocal8Bit(error.what()));
        } catch (...) {
            return setError(errorMessage,
                            QStringLiteral("The plugin raised an unknown exception while processing a frame."));
        }
    }

private:
    static bool setError(QString *destination, const QString& message) {
        if (destination) *destination = message;
        return false;
    }

    bool prepareBuffers(const QImage& input, QString *errorMessage) {
        QImage converted = input.convertToFormat(QImage::Format_ARGB32);
        if (!sourceImage.allocate(converted.width(), converted.height(), 4))
            return setError(errorMessage, QStringLiteral("The plugin input frame is too large."));
        sourceImage.copyFrom(converted);
        if (!destinationImage.allocate(converted.width(), converted.height(), 4))
            return setError(errorMessage, QStringLiteral("The plugin output frame is too large."));
        destinationImage.copyFrom(converted);
        fillBitmap(sourceBitmap, sourceLayout, sourcePixmap,
                   sourceImage.image, frameNumber);
        fillBitmap(destinationBitmap, destinationLayout, destinationPixmap,
                   destinationImage.image, frameNumber);
        pixmapProvider.source = &sourcePixmap;
        pixmapProvider.destination = &destinationPixmap;

        long flags = FILTERPARAM_SWAP_BUFFERS;
        if (plugin->definition.paramProc)
            flags = plugin->definition.paramProc(activation.get(), &gFilterFunctions);
        if (flags == FILTERPARAM_NOT_SUPPORTED)
            return setError(errorMessage,
                QStringLiteral("The plugin does not support 32-bit RGB input."));
        const int lag = static_cast<int>(static_cast<unsigned long>(flags) >> 16);
        if (lag != 0)
            return setError(errorMessage,
                QStringLiteral("This plugin buffers frames internally; lagged plugins are not supported yet."));

        swapBuffers = (flags & FILTERPARAM_SWAP_BUFFERS) != 0;
        needsLast = (flags & FILTERPARAM_NEEDS_LAST) != 0;
        int alignment = 4;
        const long alignmentFlags = flags & 0x48;
        if (alignmentFlags == FILTERPARAM_ALIGN_SCANLINES_16) alignment = 16;
        else if (alignmentFlags == FILTERPARAM_ALIGN_SCANLINES_32) alignment = 32;
        else if (alignmentFlags == FILTERPARAM_ALIGN_SCANLINES_64) alignment = 64;

        int outputWidth = destinationLayout.w;
        int outputHeight = destinationLayout.h;
        if (destinationBitmap.w != sourceWidth || destinationBitmap.h != sourceHeight) {
            outputWidth = destinationBitmap.w;
            outputHeight = destinationBitmap.h;
        }
        if (outputWidth <= 0) outputWidth = sourceWidth;
        if (outputHeight <= 0) outputHeight = sourceHeight;
        if (!swapBuffers
            && (outputWidth != sourceWidth || outputHeight != sourceHeight)) {
            return setError(errorMessage,
                QStringLiteral("The plugin requested resized output without a separate destination buffer."));
        }
        if (destinationLayout.format != 0
            && destinationLayout.format != vd2::kPixFormat_XRGB8888) {
            return setError(errorMessage,
                QStringLiteral("The plugin requested an output pixel format that is not supported by the Qt host."));
        }

        if (!sourceImage.allocate(sourceWidth, sourceHeight, alignment)
            || !destinationImage.allocate(outputWidth, outputHeight, alignment)) {
            return setError(errorMessage, QStringLiteral("The plugin frame allocation is too large."));
        }
        sourceImage.copyFrom(converted);
        if (!swapBuffers && outputWidth == sourceWidth && outputHeight == sourceHeight)
            destinationImage.copyFrom(converted);
        if (needsLast) {
            if (!lastImage.allocate(sourceWidth, sourceHeight, alignment))
                return setError(errorMessage, QStringLiteral("The plugin history frame is too large."));
            lastImage.copyFrom(converted);
        }
        updateFrameBindings();
        return true;
    }

    void updateFrameBindings() {
        fillBitmap(sourceBitmap, sourceLayout, sourcePixmap,
                   sourceImage.image, frameNumber);
        QImage& outputBinding = swapBuffers ? destinationImage.image
                                            : sourceImage.image;
        fillBitmap(destinationBitmap, destinationLayout, destinationPixmap,
                   outputBinding, frameNumber);
        if (needsLast)
            fillBitmap(lastBitmap, lastLayout, lastPixmap,
                       lastImage.image, std::max<sint64>(0, frameNumber - 1));
        pixmapProvider.source = &sourcePixmap;
        pixmapProvider.destination = &destinationPixmap;
        activation->x2 = static_cast<uint32>(destinationImage.image.width());
        activation->y2 = static_cast<uint32>(destinationImage.image.height());
    }

    void shutdown() noexcept {
        if (activation) {
            try {
                if (started && plugin && plugin->definition.endProc)
                    plugin->definition.endProc(activation.get(), &gFilterFunctions);
            } catch (...) {}
            try {
                if (initCalled && plugin && plugin->definition.deinitProc)
                    plugin->definition.deinitProc(activation.get(), &gFilterFunctions);
            } catch (...) {}
        }
        initialized = false;
        started = false;
        initCalled = false;
        activation.reset();
        filterData.clear();
        sourceImage = {};
        destinationImage = {};
        lastImage = {};
    }

    QSharedPointer<PluginDefinition> plugin;
    QByteArray serializedConfiguration;
    std::vector<std::byte> filterData;
    std::unique_ptr<VDXFilterActivation> activation;
    VDXFBitmap sourceBitmap = {};
    VDXFBitmap destinationBitmap = {};
    VDXFBitmap lastBitmap = {};
    VDXPixmapLayout sourceLayout = {};
    VDXPixmapLayout destinationLayout = {};
    VDXPixmapLayout lastLayout = {};
    VDXPixmap sourcePixmap = {};
    VDXPixmap destinationPixmap = {};
    VDXPixmap lastPixmap = {};
    VDXFilterStateInfo stateInfo = {};
    FilterModActivation filterMod = {};
    FilterModPixmapProvider pixmapProvider;
    VDXFBitmap *sourceFrames[1] = {};
    VDXFBitmap *outputFrames[1] = {};
    VDXFBitmap *sourceStreams[1] = {};
    AlignedImage sourceImage;
    AlignedImage destinationImage;
    AlignedImage lastImage;
    int sourceWidth = 0;
    int sourceHeight = 0;
    sint64 frameNumber = 0;
    bool swapBuffers = true;
    bool needsLast = false;
    bool initCalled = false;
    bool started = false;
    bool initialized = false;
};

QString wideString(const wchar_t *value) {
    return value ? QString::fromWCharArray(value) : QString();
}

bool isWindowsPortableExecutable(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.read(2) == QByteArray("MZ", 2);
}

} // namespace

class VDQtPluginHost::Private {
public:
    ~Private() { unload(); }

    void ensureLoaded() {
        if (!loaded) load();
    }

    QStringList paths() const {
        QStringList result;
        const auto addEnvironment = [&](const char *name) {
            const QString value = qEnvironmentVariable(name);
            if (!value.isEmpty())
                result.append(value.split(QDir::listSeparator(), Qt::SkipEmptyParts));
        };
        addEnvironment("VIRTUALDUBQT_PLUGIN_PATH");
        addEnvironment("VIRTUALDUB_PLUGIN_PATH");
        if (QCoreApplication::instance())
            result.append(QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("plugins")));
        const QString appData = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        if (!appData.isEmpty())
            result.append(QDir(appData).filePath(QStringLiteral("plugins")));
        for (const QString& data : QStandardPaths::standardLocations(
                 QStandardPaths::GenericDataLocation)) {
            result.append(QDir(data).filePath(QStringLiteral("virtualdub2/plugins")));
        }
        result << QStringLiteral("/usr/local/lib/virtualdub2/plugins")
               << QStringLiteral("/usr/lib/virtualdub2/plugins");

        QStringList unique;
        QSet<QString> seen;
        for (const QString& path : result) {
            const QString absolute = QFileInfo(path).absoluteFilePath();
            if (!seen.contains(absolute)) {
                seen.insert(absolute);
                unique.append(absolute);
            }
        }
        return unique;
    }

    void load() {
        unload();
        loaded = true;
        reportLines << QStringLiteral(
            "Linux-native VirtualDub video filter modules are supported through "
            "VirtualdubFilterModuleInit2/FilterModModuleInit. Windows DLL and .vdplugin "
            "binaries cannot be loaded into this native Linux process.")
                    << QString();

        QSet<QString> files;
        for (const QString& path : paths()) {
            QDir directory(path);
            reportLines << QStringLiteral("[%1]").arg(path);
            if (!directory.exists()) {
                reportLines << QStringLiteral("  (directory not found)") << QString();
                continue;
            }
            const QFileInfoList entries = directory.entryInfoList(
                {QStringLiteral("*.so"), QStringLiteral("*.so.*"),
                 QStringLiteral("*.vdf"), QStringLiteral("*.vdplugin")},
                QDir::Files | QDir::Readable, QDir::Name);
            if (entries.isEmpty()) reportLines << QStringLiteral("  (no modules found)");
            for (const QFileInfo& entry : entries) {
                const QString canonical = entry.canonicalFilePath().isEmpty()
                    ? entry.absoluteFilePath() : entry.canonicalFilePath();
                if (files.contains(canonical)) continue;
                files.insert(canonical);
                loadModule(canonical);
            }
            reportLines << QString();
        }
        if (definitions.isEmpty())
            reportLines << QStringLiteral("No compatible native VDX video filters were loaded.");
    }

    void loadModule(const QString& path) {
        if (isWindowsPortableExecutable(path)) {
            reportLines << QStringLiteral("  %1 — Windows binary (unsupported on native Linux)")
                               .arg(QFileInfo(path).fileName());
            return;
        }

        auto module = QSharedPointer<PluginModule>::create();
        module->path = path;
        module->library = std::make_unique<QLibrary>(path);
        module->library->setLoadHints(QLibrary::ResolveAllSymbolsHint);
        if (!module->library->load()) {
            reportLines << QStringLiteral("  %1 — load failed: %2")
                               .arg(QFileInfo(path).fileName(), module->library->errorString());
            return;
        }

        auto init = reinterpret_cast<VDXFilterModuleInitProc>(
            module->library->resolve("VirtualdubFilterModuleInit2"));
        auto filterModInit = reinterpret_cast<FilterModModuleInitProc>(
            module->library->resolve("FilterModModuleInit"));
        module->deinitProc = reinterpret_cast<VDXFilterModuleDeinitProc>(
            module->library->resolve("VirtualdubFilterModuleDeinit"));
        auto getPluginInfo = reinterpret_cast<tpVDXGetPluginInfo>(
            module->library->resolve("VDGetPluginInfo"));

        bool recognized = false;
        if ((init || filterModInit) && module->deinitProc) {
            recognized = true;
            int versionHigh = VIRTUALDUB_FILTERDEF_VERSION;
            int versionLow = VIRTUALDUB_FILTERDEF_COMPATIBLE_COPYCTOR;
            int modHigh = FILTERMOD_VERSION;
            int modLow = 1;
            DiscoveryContext context;
            context.module = module;
            gDiscoveryContext = &context;
            int result = -1;
            try {
                if (filterModInit) {
                    result = filterModInit(
                        reinterpret_cast<VDXFilterModule *>(&module->token),
                        &gFilterModFunctions, versionHigh, versionLow,
                        modHigh, modLow);
                } else {
                    result = init(
                        reinterpret_cast<VDXFilterModule *>(&module->token),
                        &gFilterFunctions, versionHigh, versionLow);
                }
            } catch (const std::exception& error) {
                reportLines << QStringLiteral("  %1 — initialization exception: %2")
                                   .arg(QFileInfo(path).fileName(),
                                        QString::fromLocal8Bit(error.what()));
            } catch (...) {
                reportLines << QStringLiteral("  %1 — initialization raised an unknown exception")
                                   .arg(QFileInfo(path).fileName());
            }
            gDiscoveryContext = nullptr;

            if (result == 0 && versionHigh >= VIRTUALDUB_FILTERDEF_COMPATIBLE_COPYCTOR
                && versionLow <= VIRTUALDUB_FILTERDEF_VERSION
                && (!filterModInit || modLow <= FILTERMOD_VERSION)) {
                module->apiVersion = versionHigh;
                module->initialized = true;
                for (const auto& definition : context.definitions) {
                    definition->info.apiVersion = versionHigh;
                    module->filters.append(definition);
                    definitions.insert(definition->info.id, definition);
                }
                reportLines << QStringLiteral("  %1 — loaded %2 video filter(s), VDX API %3")
                                   .arg(QFileInfo(path).fileName())
                                   .arg(context.definitions.size())
                                   .arg(versionHigh);
            } else if (result != 0) {
                reportLines << QStringLiteral("  %1 — module initialization failed")
                                   .arg(QFileInfo(path).fileName());
            } else {
                reportLines << QStringLiteral("  %1 — incompatible VDX/FilterMod API range")
                                   .arg(QFileInfo(path).fileName());
            }
        }

        if (getPluginInfo) {
            recognized = true;
            try {
                const VDXPluginInfo *const *plugins = getPluginInfo();
                for (int index = 0; plugins && plugins[index] && index < 1024; ++index) {
                    const VDXPluginInfo *info = plugins[index];
                    const QString type = info->mType == kVDXPluginType_Audio
                        ? QStringLiteral("audio filter")
                        : info->mType == kVDXPluginType_Input
                            ? QStringLiteral("input driver")
                            : info->mType == kVDXPluginType_Output
                                ? QStringLiteral("output driver")
                                : info->mType == kVDXPluginType_AudioEnc
                                    ? QStringLiteral("audio encoder")
                                    : info->mType == kVDXPluginType_Tool
                                        ? QStringLiteral("tool")
                                        : QStringLiteral("video plugin");
                    module->metadata << QStringLiteral("%1 (%2, by %3)")
                        .arg(wideString(info->mpName), type,
                             wideString(info->mpAuthor));
                }
                for (const QString& entry : module->metadata)
                    reportLines << QStringLiteral("    metadata: %1").arg(entry);
                if (!module->metadata.isEmpty()) {
                    reportLines << QStringLiteral(
                        "    modern non-video VDX metadata is recognized; its specialized "
                        "input/output/audio runtime API is not yet connected");
                }
            } catch (...) {
                reportLines << QStringLiteral("    VDGetPluginInfo raised an exception");
            }
        }

        if (!recognized) {
            reportLines << QStringLiteral("  %1 — not a VirtualDub plugin module")
                               .arg(QFileInfo(path).fileName());
            module->library->unload();
            return;
        }
        modules.append(module);
    }

    void unload() {
        runtimes.clear();
        definitions.clear();
        for (auto it = modules.rbegin(); it != modules.rend(); ++it) {
            const auto& module = *it;
            if (module->initialized && module->deinitProc) {
                try {
                    module->deinitProc(
                        reinterpret_cast<VDXFilterModule *>(&module->token),
                        &gFilterFunctions);
                } catch (...) {}
            }
            if (module->library) module->library->unload();
        }
        modules.clear();
        reportLines.clear();
        loaded = false;
    }

    QMutex mutex;
    bool loaded = false;
    QList<QSharedPointer<PluginModule>> modules;
    QHash<QString, QSharedPointer<PluginDefinition>> definitions;
    QHash<QString, QSharedPointer<VideoFilterRuntime>> runtimes;
    QStringList reportLines;
};

VDQtPluginHost::VDQtPluginHost() : d(std::make_unique<Private>()) {}
VDQtPluginHost::~VDQtPluginHost() = default;

VDQtPluginHost& VDQtPluginHost::instance() {
    static VDQtPluginHost host;
    return host;
}

QList<VDQtPluginFilterInfo> VDQtPluginHost::videoFilters() {
    QMutexLocker locker(&d->mutex);
    d->ensureLoaded();
    QList<VDQtPluginFilterInfo> result;
    result.reserve(d->definitions.size());
    for (const auto& definition : std::as_const(d->definitions))
        result.append(definition->info);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        const int nameComparison = QString::compare(
            left.name, right.name, Qt::CaseInsensitive);
        return nameComparison != 0 ? nameComparison < 0 : left.id < right.id;
    });
    return result;
}

QString VDQtPluginHost::report() {
    QMutexLocker locker(&d->mutex);
    d->ensureLoaded();
    return d->reportLines.join(QLatin1Char('\n'));
}

QStringList VDQtPluginHost::searchPaths() const { return d->paths(); }

void VDQtPluginHost::reload() {
    QMutexLocker locker(&d->mutex);
    d->load();
}

bool VDQtPluginHost::processVideoFilter(
    const QString& filterId, const QString& instanceId,
    const QByteArray& serializedConfiguration, const QImage& input,
    QImage *output, QString *errorMessage) {
    if (!output) return false;
    QMutexLocker locker(&d->mutex);
    d->ensureLoaded();
    const auto definition = d->definitions.value(filterId);
    if (!definition) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The configured plugin filter is not installed.");
        return false;
    }
    const QString runtimeKey = filterId + QLatin1Char(':') + instanceId + QLatin1Char('@')
        + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
    auto runtime = d->runtimes.value(runtimeKey);
    if (!runtime || !runtime->matches(serializedConfiguration, input)) {
        runtime = QSharedPointer<VideoFilterRuntime>::create(definition);
        if (!runtime->initialize(serializedConfiguration, input, errorMessage))
            return false;
        d->runtimes.insert(runtimeKey, runtime);
    }
    return runtime->process(input, output, errorMessage);
}

void VDQtPluginHost::forgetInstance(const QString& instanceId) {
    QMutexLocker locker(&d->mutex);
    for (auto it = d->runtimes.begin(); it != d->runtimes.end();) {
        const QString marker = QLatin1Char(':') + instanceId + QLatin1Char('@');
        if (it.key().contains(marker))
            it = d->runtimes.erase(it);
        else ++it;
    }
}

void VDQtPluginHost::forgetAllInstances() {
    QMutexLocker locker(&d->mutex);
    d->runtimes.clear();
}

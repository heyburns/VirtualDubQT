// Minimal Linux-native VDX module used only by plugin_host_tests. Its behavior
// is deliberately simple and deterministic so failures implicate ABI/lifetime
// adaptation rather than a complex third-party filter.
#include <vd2/plugin/vdvideofilt.h>

#include <cstring>

namespace {

int gLiveInstances = 0;
int gRunCalls = 0;
sint64 gTiming[14] = {};
int gModuleInitializations = 0;
int gModuleShutdowns = 0;
int __cdecl initializeFilter(VDXFilterActivation *, const VDXFilterFunctions *) {
    ++gLiveInstances;
    return 0;
}
void __cdecl destroyFilter(VDXFilterActivation *, const VDXFilterFunctions *) {
    --gLiveInstances;
}

long __cdecl filterParameters(VDXFilterActivation *activation,
                              const VDXFilterFunctions *) {
    if (!activation || !activation->src.mpPixmapLayout
        || !activation->dst.mpPixmapLayout)
        return FILTERPARAM_NOT_SUPPORTED;
    if (activation->src.mpPixmapLayout->format != vd2::kPixFormat_XRGB8888)
        return FILTERPARAM_NOT_SUPPORTED;
    *activation->dst.mpPixmapLayout = *activation->src.mpPixmapLayout;
    gTiming[10] = activation->src.mFrameRateHi;
    gTiming[11] = activation->src.mFrameRateLo;
    gTiming[12] = activation->src.mFrameNumber;
    return FILTERPARAM_SWAP_BUFFERS | FILTERPARAM_SUPPORTS_ALTFORMATS
        | FILTERPARAM_PURE_TRANSFORM;
}

int __cdecl runFilter(const VDXFilterActivation *activation,
                      const VDXFilterFunctions *) {
    if (!activation || !activation->src.mpPixmap || !activation->dst.mpPixmap)
        return 1;
    ++gRunCalls;
    gTiming[0] = activation->src.mFrameRateHi;
    gTiming[1] = activation->src.mFrameRateLo;
    gTiming[2] = activation->src.mFrameNumber;
    gTiming[3] = activation->dst.mFrameNumber;
    gTiming[4] = activation->src.mFrameTimestampStart;
    gTiming[5] = activation->src.mFrameTimestampEnd;
    gTiming[6] = activation->pfsi->lCurrentFrame;
    gTiming[7] = activation->pfsi->lCurrentSourceFrame;
    gTiming[8] = activation->pfsi->lMicrosecsPerFrame;
    gTiming[9] = activation->pfsi->lSourceFrameMS;
    const VDXPixmap& source = *activation->src.mpPixmap;
    const VDXPixmap& destination = *activation->dst.mpPixmap;
    for (int y = 0; y < source.h; ++y) {
        const auto *src = static_cast<const unsigned char *>(source.data)
            + static_cast<ptrdiff_t>(y) * source.pitch;
        auto *dst = static_cast<unsigned char *>(destination.data)
            + static_cast<ptrdiff_t>(y) * destination.pitch;
        for (int x = 0; x < source.w; ++x) {
            dst[x * 4] = static_cast<unsigned char>(255 - src[x * 4]);
            dst[x * 4 + 1] = static_cast<unsigned char>(255 - src[x * 4 + 1]);
            dst[x * 4 + 2] = static_cast<unsigned char>(255 - src[x * 4 + 2]);
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    return 0;
}

VDXFilterDefinition gDefinition = {};
VDXFilterDefinition gPrefetchDefinition = {};
VDXFilterDefinition gLastDefinition = {};
VDXFilterDefinition gRateDefinition = {};

sint64 __cdecl futureFrame(const VDXFilterActivation *, const VDXFilterFunctions *, sint64 frame) {
    return frame + 1;
}
long __cdecl lastParameters(VDXFilterActivation *activation, const VDXFilterFunctions *functions) {
    return filterParameters(activation, functions) | FILTERPARAM_NEEDS_LAST;
}
int __cdecl lastRun(const VDXFilterActivation *activation, const VDXFilterFunctions *) {
    if (!activation || !activation->last || !activation->last->mpPixmap) return 1;
    gTiming[13] = activation->last->mFrameNumber;
    const auto& previous = *activation->last->mpPixmap;
    const auto& destination = *activation->dst.mpPixmap;
    for (int y = 0; y < previous.h; ++y)
        std::memcpy(static_cast<unsigned char *>(destination.data) + y * destination.pitch,
                    static_cast<const unsigned char *>(previous.data) + y * previous.pitch,
                    previous.w * 4);
    return 0;
}
long __cdecl rateParameters(VDXFilterActivation *activation, const VDXFilterFunctions *functions) {
    const long flags = filterParameters(activation, functions);
    activation->dst.mFrameRateHi *= 2;
    return flags;
}

} // namespace

extern "C" __attribute__((visibility("default")))
int VirtualdubFilterModuleInit2(VDXFilterModule *module,
                               const VDXFilterFunctions *functions,
                               int& version, int& compatibility) {
    version = VIRTUALDUB_FILTERDEF_VERSION;
    ++gModuleInitializations;
    compatibility = VIRTUALDUB_FILTERDEF_COMPATIBLE_COPYCTOR;
    gDefinition.name = "VDQt native test invert";
    gDefinition.desc =
        "Regression filter for the Linux-native VDX compatibility host.";
    gDefinition.maker = "VirtualDubQt";
    gDefinition.runProc = runFilter;
    gDefinition.initProc = initializeFilter;
    gDefinition.deinitProc = destroyFilter;
    gDefinition.paramProc = filterParameters;
    if (!functions || !functions->addFilter
        || !functions->addFilter(module, &gDefinition, sizeof gDefinition)) return 1;
    gPrefetchDefinition = gDefinition;
    gPrefetchDefinition.name = "VDQt unsupported future-frame test";
    gPrefetchDefinition.prefetchProc = futureFrame;
    gLastDefinition = gDefinition;
    gLastDefinition.name = "VDQt previous-frame test";
    gLastDefinition.paramProc = lastParameters;
    gLastDefinition.runProc = lastRun;
    gRateDefinition = gDefinition;
    gRateDefinition.name = "VDQt unsupported rate test";
    gRateDefinition.paramProc = rateParameters;
    return functions->addFilter(module, &gPrefetchDefinition, sizeof gPrefetchDefinition)
        && functions->addFilter(module, &gLastDefinition, sizeof gLastDefinition)
        && functions->addFilter(module, &gRateDefinition, sizeof gRateDefinition) ? 0 : 1;
}

extern "C" __attribute__((visibility("default")))
void VirtualdubFilterModuleDeinit(VDXFilterModule *,
                                  const VDXFilterFunctions *) { ++gModuleShutdowns; }

// Test-only observability; no production host API or third-party ABI changes.
extern "C" __attribute__((visibility("default")))
int VDQtTestLiveInstances() { return gLiveInstances; }

extern "C" __attribute__((visibility("default")))
int VDQtTestRunCalls() { return gRunCalls; }

extern "C" __attribute__((visibility("default")))
sint64 VDQtTestTiming(int field) { return field >= 0 && field < 14 ? gTiming[field] : -1; }
extern "C" __attribute__((visibility("default")))
int VDQtTestModuleBalance() { return gModuleInitializations - gModuleShutdowns; }

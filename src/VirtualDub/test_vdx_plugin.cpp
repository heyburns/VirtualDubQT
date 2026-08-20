#include <vd2/plugin/vdvideofilt.h>

#include <cstring>

namespace {

long __cdecl filterParameters(VDXFilterActivation *activation,
                              const VDXFilterFunctions *) {
    if (!activation || !activation->src.mpPixmapLayout
        || !activation->dst.mpPixmapLayout)
        return FILTERPARAM_NOT_SUPPORTED;
    if (activation->src.mpPixmapLayout->format != vd2::kPixFormat_XRGB8888)
        return FILTERPARAM_NOT_SUPPORTED;
    *activation->dst.mpPixmapLayout = *activation->src.mpPixmapLayout;
    return FILTERPARAM_SWAP_BUFFERS | FILTERPARAM_SUPPORTS_ALTFORMATS
        | FILTERPARAM_PURE_TRANSFORM;
}

int __cdecl runFilter(const VDXFilterActivation *activation,
                      const VDXFilterFunctions *) {
    if (!activation || !activation->src.mpPixmap || !activation->dst.mpPixmap)
        return 1;
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

} // namespace

extern "C" __attribute__((visibility("default")))
int VirtualdubFilterModuleInit2(VDXFilterModule *module,
                               const VDXFilterFunctions *functions,
                               int& version, int& compatibility) {
    version = VIRTUALDUB_FILTERDEF_VERSION;
    compatibility = VIRTUALDUB_FILTERDEF_COMPATIBLE_COPYCTOR;
    gDefinition.name = "VDQt native test invert";
    gDefinition.desc =
        "Regression filter for the Linux-native VDX compatibility host.";
    gDefinition.maker = "VirtualDubQt";
    gDefinition.runProc = runFilter;
    gDefinition.paramProc = filterParameters;
    return functions && functions->addFilter
        && functions->addFilter(module, &gDefinition, sizeof gDefinition)
        ? 0 : 1;
}

extern "C" __attribute__((visibility("default")))
void VirtualdubFilterModuleDeinit(VDXFilterModule *,
                                  const VDXFilterFunctions *) {}

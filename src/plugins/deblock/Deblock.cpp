#include <avisynth/avisynth.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

static const uint8_t alpha_table[52] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    4, 4, 5, 6, 7, 8, 9, 10, 12, 13, 15, 17, 20, 22,
    25, 28, 32, 36, 40, 45, 50, 56, 63, 71, 80, 90,
    101, 113, 127, 144, 162, 182, 203, 226, 255, 255
};

static const uint8_t beta_table[52] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
    16, 16, 17, 17, 18, 18
};

static const uint8_t tc0_table[52] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
    2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 7, 8, 9, 10,
    11, 13, 14, 16
};

class Deblock : public GenericVideoFilter {
    int m_quant;
    int m_aOffset;
    int m_bOffset;
    int m_planes;

    template<typename Pixel>
    void FilterPlane(uint8_t* dst, int pitch, int width, int height, int bits);

public:
    Deblock(PClip child, int quant, int aOffset, int bOffset, int planes, IScriptEnvironment* env);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override;
    int __stdcall SetCacheHints(int cachehints, int frame_range) override {
        return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    }
};

Deblock::Deblock(PClip child, int quant, int aOffset, int bOffset, int planes, IScriptEnvironment* env)
    : GenericVideoFilter(child),
      m_quant(std::clamp(quant, 0, 51)),
      m_aOffset(aOffset),
      m_bOffset(bOffset),
      m_planes(planes) {
    const int bits = vi.BitsPerComponent();
    if (!vi.IsPlanar() || !(vi.IsYUV() || vi.IsYUVA() || vi.IsY())
        || (bits != 8 && bits != 10 && bits != 12 && bits != 14 && bits != 16)) {
        env->ThrowError("Deblock: requires planar integer YUV/gray video (8, 10, 12, 14 or 16 bits).");
    }
}

template<typename Pixel>
void Deblock::FilterPlane(uint8_t* dst, int pitch, int width, int height, int bits) {
    const int qp_a = static_cast<int>(std::clamp<int64_t>(int64_t(m_quant) + m_aOffset, 0, 51));
    const int qp_b = static_cast<int>(std::clamp<int64_t>(int64_t(m_quant) + m_bOffset, 0, 51));

    const int scale = 1 << (bits - 8);
    const int alpha = alpha_table[qp_a] * scale;
    const int beta = beta_table[qp_b] * scale;
    const int tc0 = tc0_table[qp_a] * scale;
    const int maximum = (1 << bits) - 1;

    if (alpha == 0 && beta == 0) return;

    // 1. Vertical block edges (x = 8, 16, 24, ...)
    for (int x = 8; x + 1 < width; x += 8) {
        for (int y = 0; y < height; ++y) {
            Pixel* row = reinterpret_cast<Pixel*>(dst + y * pitch);
            int p1 = row[x - 2];
            int p0 = row[x - 1];
            int q0 = row[x + 0];
            int q1 = row[x + 1];

            if (std::abs(p0 - q0) < alpha && std::abs(p1 - p0) < beta && std::abs(q1 - q0) < beta) {
                int delta = ((q0 - p0) * 4 + (p1 - q1) + 4) >> 3;
                delta = std::clamp(delta, -tc0, tc0);

                row[x - 1] = static_cast<Pixel>(std::clamp(p0 + delta, 0, maximum));
                row[x + 0] = static_cast<Pixel>(std::clamp(q0 - delta, 0, maximum));
            }
        }
    }

    // 2. Horizontal block edges (y = 8, 16, 24, ...)
    for (int y = 8; y + 1 < height; y += 8) {
        Pixel* row_p1 = reinterpret_cast<Pixel*>(dst + (y - 2) * pitch);
        Pixel* row_p0 = reinterpret_cast<Pixel*>(dst + (y - 1) * pitch);
        Pixel* row_q0 = reinterpret_cast<Pixel*>(dst + (y + 0) * pitch);
        Pixel* row_q1 = reinterpret_cast<Pixel*>(dst + (y + 1) * pitch);

        for (int x = 0; x < width; ++x) {
            int p1 = row_p1[x];
            int p0 = row_p0[x];
            int q0 = row_q0[x];
            int q1 = row_q1[x];

            if (std::abs(p0 - q0) < alpha && std::abs(p1 - p0) < beta && std::abs(q1 - q0) < beta) {
                int delta = ((q0 - p0) * 4 + (p1 - q1) + 4) >> 3;
                delta = std::clamp(delta, -tc0, tc0);

                row_p0[x] = static_cast<Pixel>(std::clamp(p0 + delta, 0, maximum));
                row_q0[x] = static_cast<Pixel>(std::clamp(q0 - delta, 0, maximum));
            }
        }
    }
}

PVideoFrame __stdcall Deblock::GetFrame(int n, IScriptEnvironment* env) {
    PVideoFrame src = child->GetFrame(n, env);
    if (!src) return nullptr;

    // Copy-on-write preserves every byte of every plane (including alpha and
    // unselected planes) and frame properties. Plane row sizes are byte counts;
    // a 10-bit row needs twice as many bytes as its pixel width.
    env->MakeWritable(&src);
    const int bits = vi.BitsPerComponent();
    const int components = vi.IsY() ? 1 : 3;
    const int planeIds[] = {PLANAR_Y, PLANAR_U, PLANAR_V};
    for (int component = 0; component < components; ++component) {
        if (!(m_planes & (1 << component))) continue;
        const int plane = planeIds[component];
        const int width = src->GetRowSize(plane) / vi.ComponentSize();
        const int height = src->GetHeight(plane);
        if (bits == 8)
            FilterPlane<uint8_t>(src->GetWritePtr(plane), src->GetPitch(plane), width, height, bits);
        else
            FilterPlane<uint16_t>(src->GetWritePtr(plane), src->GetPitch(plane), width, height, bits);
    }

    return src;
}

AVSValue __cdecl Create_Deblock(AVSValue args, void* user_data, IScriptEnvironment* env) {
    PClip child = args[0].AsClip();
    int quant = args[1].AsInt(25);
    int aOffset = args[2].AsInt(0);
    int bOffset = args[3].AsInt(0);
    int planes = args[4].AsInt(7);

    return new Deblock(child, quant, aOffset, bOffset, planes, env);
}

__attribute__((visibility("hidden"))) const AVS_Linkage* AVS_linkage = nullptr;

extern "C" __attribute__((visibility("default"))) const char* AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;
    env->AddFunction("Deblock", "c[quant]i[aOffset]i[bOffset]i[planes]i", Create_Deblock, 0);
    return "Deblock plugin for Linux AviSynth+";
}

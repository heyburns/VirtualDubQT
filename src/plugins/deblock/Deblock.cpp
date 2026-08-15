#include <avisynth/avisynth.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>

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

    void FilterPlane(uint8_t* dst, int pitch, int width, int height);

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
}

void Deblock::FilterPlane(uint8_t* dst, int pitch, int width, int height) {
    int qp_a = std::clamp(m_quant + m_aOffset, 0, 51);
    int qp_b = std::clamp(m_quant + m_bOffset, 0, 51);

    int alpha = alpha_table[qp_a];
    int beta = beta_table[qp_b];
    int tc0 = tc0_table[qp_a];

    if (alpha == 0 && beta == 0) return;

    // 1. Vertical block edges (x = 8, 16, 24, ...)
    for (int x = 8; x < width; x += 8) {
        for (int y = 0; y < height; ++y) {
            uint8_t* row = dst + y * pitch;
            int p1 = row[x - 2];
            int p0 = row[x - 1];
            int q0 = row[x + 0];
            int q1 = row[x + 1];

            if (std::abs(p0 - q0) < alpha && std::abs(p1 - p0) < beta && std::abs(q1 - q0) < beta) {
                int delta = ((q0 - p0) * 4 + (p1 - q1) + 4) >> 3;
                delta = std::clamp(delta, -tc0, tc0);

                row[x - 1] = static_cast<uint8_t>(std::clamp(p0 + delta, 0, 255));
                row[x + 0] = static_cast<uint8_t>(std::clamp(q0 - delta, 0, 255));
            }
        }
    }

    // 2. Horizontal block edges (y = 8, 16, 24, ...)
    for (int y = 8; y < height; y += 8) {
        uint8_t* row_p1 = dst + (y - 2) * pitch;
        uint8_t* row_p0 = dst + (y - 1) * pitch;
        uint8_t* row_q0 = dst + (y + 0) * pitch;
        uint8_t* row_q1 = dst + (y + 1) * pitch;

        for (int x = 0; x < width; ++x) {
            int p1 = row_p1[x];
            int p0 = row_p0[x];
            int q0 = row_q0[x];
            int q1 = row_q1[x];

            if (std::abs(p0 - q0) < alpha && std::abs(p1 - p0) < beta && std::abs(q1 - q0) < beta) {
                int delta = ((q0 - p0) * 4 + (p1 - q1) + 4) >> 3;
                delta = std::clamp(delta, -tc0, tc0);

                row_p0[x] = static_cast<uint8_t>(std::clamp(p0 + delta, 0, 255));
                row_q0[x] = static_cast<uint8_t>(std::clamp(q0 - delta, 0, 255));
            }
        }
    }
}

PVideoFrame __stdcall Deblock::GetFrame(int n, IScriptEnvironment* env) {
    PVideoFrame src = child->GetFrame(n, env);
    if (!src) return nullptr;

    PVideoFrame dst = env->NewVideoFrame(vi);
    if (!dst) return src;

    if (vi.IsPlanar()) {
        int wY = vi.width;
        int hY = vi.height;

        env->BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y),
                    src->GetReadPtr(PLANAR_Y), src->GetPitch(PLANAR_Y),
                    wY, hY);
        FilterPlane(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y), wY, hY);

        if (vi.IsYUV()) {
            int subW = vi.GetPlaneWidthSubsampling(PLANAR_U);
            int subH = vi.GetPlaneHeightSubsampling(PLANAR_U);
            int wUV = vi.width >> subW;
            int hUV = vi.height >> subH;

            env->BitBlt(dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U),
                        src->GetReadPtr(PLANAR_U), src->GetPitch(PLANAR_U),
                        wUV, hUV);
            env->BitBlt(dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V),
                        src->GetReadPtr(PLANAR_V), src->GetPitch(PLANAR_V),
                        wUV, hUV);

            if (m_planes & 2) {
                FilterPlane(dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U), wUV, hUV);
            }
            if (m_planes & 4) {
                FilterPlane(dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V), wUV, hUV);
            }
        }
    } else {
        env->BitBlt(dst->GetWritePtr(), dst->GetPitch(),
                    src->GetReadPtr(), src->GetPitch(),
                    vi.width * (vi.BitsPerPixel() / 8), vi.height);
    }

    static int s_deblock_count = 0;
    if (++s_deblock_count <= 10 || s_deblock_count % 30 == 0) {
        printf("[Deblock] Active in pipeline -> Processing frame %d (quant=%d, planes=%d, count=%d)\n",
               n, m_quant, m_planes, s_deblock_count);
        fflush(stdout);
    }

    return dst;
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

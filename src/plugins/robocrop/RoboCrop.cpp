#include <avisynth/avisynth.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>

class RoboCrop : public GenericVideoFilter {
    int m_samples;
    float m_thresh;
    bool m_laced;
    int m_wmod;
    int m_hmod;
    int m_rlbt;
    bool m_debug;
    float m_ignore;
    int m_matrix;
    int m_baffle;
    bool m_scaleAutoThreshRGB;
    bool m_scaleAutoThreshYUV;
    int m_cropMode;
    bool m_blank;
    bool m_blankPC;
    bool m_align;
    bool m_show;
    std::string m_logFn;
    bool m_logAppend;
    float m_atm;
    int m_start;
    int m_end;
    int m_leftAdd, m_topAdd, m_rightAdd, m_botAdd;
    int m_leftSkip, m_topSkip, m_rightSkip, m_botSkip;
    float m_scanPerc;
    std::string m_prefix;

    int m_finalLeft;
    int m_finalTop;
    int m_finalRight;
    int m_finalBot;
    int m_croppedWidth;
    int m_croppedHeight;
    bool m_isCalculated;

    void CalculateCrop(IScriptEnvironment* env);

public:
    RoboCrop(PClip child, int samples, float thresh, bool laced, int wmod, int hmod, int rlbt,
             bool debug, float ignore, int matrix, int baffle, bool scaleRGB, bool scaleYUV,
             int cropMode, bool blank, bool blankPC, bool align, bool show, const char* logFn,
             bool logAppend, float atm, int start, int end, int leftAdd, int topAdd, int rightAdd,
             int botAdd, int leftSkip, int topSkip, int rightSkip, int botSkip, float scanPerc,
             const char* prefix, IScriptEnvironment* env);

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override;
    int __stdcall SetCacheHints(int cachehints, int frame_range) override {
        return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    }
};

RoboCrop::RoboCrop(PClip child, int samples, float thresh, bool laced, int wmod, int hmod, int rlbt,
                   bool debug, float ignore, int matrix, int baffle, bool scaleRGB, bool scaleYUV,
                   int cropMode, bool blank, bool blankPC, bool align, bool show, const char* logFn,
                   bool logAppend, float atm, int start, int end, int leftAdd, int topAdd, int rightAdd,
                   int botAdd, int leftSkip, int topSkip, int rightSkip, int botSkip, float scanPerc,
                   const char* prefix, IScriptEnvironment* env)
    : GenericVideoFilter(child),
      m_samples(samples > 0 ? samples : 64),
      m_thresh(thresh),
      m_laced(laced),
      m_wmod(wmod > 0 ? wmod : (child->GetVideoInfo().IsYV12() || child->GetVideoInfo().IsYUY2() ? 2 : 1)),
      m_hmod(hmod > 0 ? hmod : (laced ? 4 : (child->GetVideoInfo().IsYV12() ? 2 : 1))),
      m_rlbt(rlbt != 0 ? rlbt : 15),
      m_debug(debug),
      m_ignore(ignore),
      m_matrix(matrix),
      m_baffle(baffle > 0 ? baffle : 2),
      m_scaleAutoThreshRGB(scaleRGB),
      m_scaleAutoThreshYUV(scaleYUV),
      m_cropMode(cropMode),
      m_blank(blank),
      m_blankPC(blankPC),
      m_align(align),
      m_show(show),
      m_logFn(logFn ? logFn : ""),
      m_logAppend(logAppend),
      m_atm(atm > 0.0f ? atm : 1.0f),
      m_start(start >= 0 ? start : 0),
      m_end(end > 0 ? end : (child->GetVideoInfo().num_frames - 1)),
      m_leftAdd(leftAdd), m_topAdd(topAdd), m_rightAdd(rightAdd), m_botAdd(botAdd),
      m_leftSkip(leftSkip), m_topSkip(topSkip), m_rightSkip(rightSkip), m_botSkip(botSkip),
      m_scanPerc(scanPerc > 0.0f ? scanPerc : 100.0f),
      m_prefix(prefix && strlen(prefix) > 0 ? prefix : "ROBOCROP_"),
      m_finalLeft(0), m_finalTop(0), m_finalRight(0), m_finalBot(0),
      m_croppedWidth(0), m_croppedHeight(0), m_isCalculated(false)
{
    if (m_wmod <= 0) m_wmod = 2;
    if (m_hmod <= 0) m_hmod = (m_laced ? 4 : 2);
    if (m_thresh <= 0.0f) m_thresh = 40.0f * m_atm;

    CalculateCrop(env);

    if (m_cropMode == 0 && !m_blank) {
        vi.width = m_croppedWidth;
        vi.height = m_croppedHeight;
    }
}

void RoboCrop::CalculateCrop(IScriptEnvironment* env) {
    if (m_isCalculated) return;
    m_isCalculated = true;

    int origWidth = vi.width;
    int origHeight = vi.height;
    int totalFrames = vi.num_frames;

    if (m_start < 0) m_start = 0;
    if (m_end < m_start || m_end >= totalFrames) m_end = totalFrames - 1;

    int numSamples = std::max(8, std::min(m_samples, 64));

    int minLeft = origWidth;
    int minTop = origHeight;
    int minRight = origWidth;
    int minBot = origHeight;
    int activeFramesCount = 0;

    int effectiveThresh = static_cast<int>(std::round(m_thresh > 0.0f ? m_thresh : 40.0f * m_atm));

    for (int s = 0; s < numSamples; ++s) {
        int frameNum = m_start + (int)(((int64_t)(s * 2 + 1) * (m_end - m_start)) / (2 * numSamples));
        frameNum = std::clamp(frameNum, m_start, m_end);

        PVideoFrame frame;
        try {
            frame = child->GetFrame(frameNum, env);
        } catch (...) {
            continue;
        }
        if (!frame) continue;

        int frameLeft = 0;
        int frameTop = 0;
        int frameRight = 0;
        int frameBot = 0;
        bool frameHasActive = false;

        if (vi.IsPlanar()) {
            const uint8_t* srcY = frame->GetReadPtr(PLANAR_Y);
            int pitch = frame->GetPitch(PLANAR_Y);

            // Left Scan
            if (m_rlbt & 2) {
                for (int x = m_leftSkip; x < origWidth / 2; ++x) {
                    int count = 0;
                    for (int y = m_topSkip; y < origHeight - m_botSkip; ++y) {
                        if (srcY[y * pitch + x] > effectiveThresh) {
                            if (++count >= m_baffle) break;
                        }
                    }
                    if (count >= m_baffle) {
                        frameLeft = x;
                        frameHasActive = true;
                        break;
                    }
                }
            }

            // Right Scan
            if (m_rlbt & 1) {
                for (int x = origWidth - 1 - m_rightSkip; x >= origWidth / 2; --x) {
                    int count = 0;
                    for (int y = m_topSkip; y < origHeight - m_botSkip; ++y) {
                        if (srcY[y * pitch + x] > effectiveThresh) {
                            if (++count >= m_baffle) break;
                        }
                    }
                    if (count >= m_baffle) {
                        frameRight = (origWidth - 1 - x);
                        frameHasActive = true;
                        break;
                    }
                }
            }

            // Top Scan
            if (m_rlbt & 8) {
                for (int y = m_topSkip; y < origHeight / 2; ++y) {
                    int count = 0;
                    for (int x = m_leftSkip; x < origWidth - m_rightSkip; ++x) {
                        if (srcY[y * pitch + x] > effectiveThresh) {
                            if (++count >= m_baffle) break;
                        }
                    }
                    if (count >= m_baffle) {
                        frameTop = y;
                        frameHasActive = true;
                        break;
                    }
                }
            }

            // Bottom Scan
            if (m_rlbt & 4) {
                for (int y = origHeight - 1 - m_botSkip; y >= origHeight / 2; --y) {
                    int count = 0;
                    for (int x = m_leftSkip; x < origWidth - m_rightSkip; ++x) {
                        if (srcY[y * pitch + x] > effectiveThresh) {
                            if (++count >= m_baffle) break;
                        }
                    }
                    if (count >= m_baffle) {
                        frameBot = (origHeight - 1 - y);
                        frameHasActive = true;
                        break;
                    }
                }
            }
        } else if (vi.IsRGB32() || vi.IsRGB24()) {
            const uint8_t* src = frame->GetReadPtr();
            int pitch = frame->GetPitch();
            int bpp = vi.IsRGB32() ? 4 : 3;

            // Top Scan
            if (m_rlbt & 8) {
                for (int y = m_topSkip; y < origHeight / 2; ++y) {
                    int srcY = (origHeight - 1 - y);
                    const uint8_t* row = src + srcY * pitch;
                    int count = 0;
                    for (int x = m_leftSkip; x < origWidth - m_rightSkip; ++x) {
                        int b = row[x * bpp + 0];
                        int g = row[x * bpp + 1];
                        int r = row[x * bpp + 2];
                        int luma = (r * 77 + g * 150 + b * 29) >> 8;
                        if (luma > effectiveThresh) {
                            if (++count >= m_baffle) break;
                        }
                    }
                    if (count >= m_baffle) {
                        frameTop = y;
                        frameHasActive = true;
                        break;
                    }
                }
            }

            // Bottom Scan
            if (m_rlbt & 4) {
                for (int y = origHeight - 1 - m_botSkip; y >= origHeight / 2; --y) {
                    int srcY = (origHeight - 1 - y);
                    const uint8_t* row = src + srcY * pitch;
                    int count = 0;
                    for (int x = m_leftSkip; x < origWidth - m_rightSkip; ++x) {
                        int b = row[x * bpp + 0];
                        int g = row[x * bpp + 1];
                        int r = row[x * bpp + 2];
                        int luma = (r * 77 + g * 150 + b * 29) >> 8;
                        if (luma > effectiveThresh) {
                            if (++count >= m_baffle) break;
                        }
                    }
                    if (count >= m_baffle) {
                        frameBot = (origHeight - 1 - y);
                        frameHasActive = true;
                        break;
                    }
                }
            }

            // Left Scan
            if (m_rlbt & 2) {
                for (int x = m_leftSkip; x < origWidth / 2; ++x) {
                    int count = 0;
                    for (int y = m_topSkip; y < origHeight - m_botSkip; ++y) {
                        int srcY = (origHeight - 1 - y);
                        const uint8_t* pixel = src + srcY * pitch + x * bpp;
                        int luma = (pixel[2] * 77 + pixel[1] * 150 + pixel[0] * 29) >> 8;
                        if (luma > effectiveThresh) {
                            if (++count >= m_baffle) break;
                        }
                    }
                    if (count >= m_baffle) {
                        frameLeft = x;
                        frameHasActive = true;
                        break;
                    }
                }
            }

            // Right Scan
            if (m_rlbt & 1) {
                for (int x = origWidth - 1 - m_rightSkip; x >= origWidth / 2; --x) {
                    int count = 0;
                    for (int y = m_topSkip; y < origHeight - m_botSkip; ++y) {
                        int srcY = (origHeight - 1 - y);
                        const uint8_t* pixel = src + srcY * pitch + x * bpp;
                        int luma = (pixel[2] * 77 + pixel[1] * 150 + pixel[0] * 29) >> 8;
                        if (luma > effectiveThresh) {
                            if (++count >= m_baffle) break;
                        }
                    }
                    if (count >= m_baffle) {
                        frameRight = (origWidth - 1 - x);
                        frameHasActive = true;
                        break;
                    }
                }
            }
        }

        // If the frame had active content (not completely black/blank), accumulate crop limits
        if (frameHasActive) {
            minLeft = std::min(minLeft, frameLeft);
            minTop = std::min(minTop, frameTop);
            minRight = std::min(minRight, frameRight);
            minBot = std::min(minBot, frameBot);
            activeFramesCount++;
        }
    }

    if (activeFramesCount == 0 || minLeft > origWidth / 2 || minTop > origHeight / 2 || minRight > origWidth / 2 || minBot > origHeight / 2) {
        minLeft = 0;
        minTop = 0;
        minRight = 0;
        minBot = 0;
    }

    minLeft = std::max(0, minLeft + m_leftAdd);
    minTop = std::max(0, minTop + m_topAdd);
    minRight = std::max(0, minRight + m_rightAdd);
    minBot = std::max(0, minBot + m_botAdd);

    // Apply modulo alignment
    m_finalLeft = (minLeft / m_wmod) * m_wmod;
    m_finalTop = (minTop / m_hmod) * m_hmod;
    int finalRight = (minRight / m_wmod) * m_wmod;
    int finalBot = (minBot / m_hmod) * m_hmod;

    int candidateW = origWidth - m_finalLeft - finalRight;
    int candidateH = origHeight - m_finalTop - finalBot;

    if (candidateW < m_wmod || candidateH < m_hmod) {
        m_finalLeft = 0;
        m_finalTop = 0;
        m_croppedWidth = origWidth;
        m_croppedHeight = origHeight;
        m_finalRight = 0;
        m_finalBot = 0;
    } else {
        m_croppedWidth = (candidateW / m_wmod) * m_wmod;
        m_croppedHeight = (candidateH / m_hmod) * m_hmod;
        m_finalRight = origWidth - m_finalLeft - m_croppedWidth;
        m_finalBot = origHeight - m_finalTop - m_croppedHeight;
    }

    m_isCalculated = true;

    // Export Global Script Variables
    env->SetGlobalVar((m_prefix + "LEFT").c_str(), AVSValue(m_finalLeft));
    env->SetGlobalVar((m_prefix + "TOP").c_str(), AVSValue(m_finalTop));
    env->SetGlobalVar((m_prefix + "RIGHT").c_str(), AVSValue(m_finalRight));
    env->SetGlobalVar((m_prefix + "BOT").c_str(), AVSValue(m_finalBot));
    env->SetGlobalVar((m_prefix + "WIDTH").c_str(), AVSValue(m_croppedWidth));
    env->SetGlobalVar((m_prefix + "HEIGHT").c_str(), AVSValue(m_croppedHeight));

    if (m_debug) {
        printf("[RoboCrop] Crop rect: Left=%d, Top=%d, Width=%d, Height=%d (Right=%d, Bot=%d)\n",
               m_finalLeft, m_finalTop, m_croppedWidth, m_croppedHeight, m_finalRight, m_finalBot);
    }
}

PVideoFrame __stdcall RoboCrop::GetFrame(int n, IScriptEnvironment* env) {
    PVideoFrame src = child->GetFrame(n, env);
    if (!src) return nullptr;

    if (vi.width == child->GetVideoInfo().width && vi.height == child->GetVideoInfo().height && m_finalLeft == 0 && m_finalTop == 0) {
        return src;
    }

    PVideoFrame dst = env->NewVideoFrame(vi);
    if (!dst) return src;

    if (vi.IsPlanar()) {
        int compSize = vi.ComponentSize();
        const uint8_t* srcY = src->GetReadPtr(PLANAR_Y) + m_finalTop * src->GetPitch(PLANAR_Y) + m_finalLeft * compSize;
        env->BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y),
                    srcY, src->GetPitch(PLANAR_Y),
                    vi.width * compSize, vi.height);

        if (vi.IsYUV()) {
            int subW = vi.GetPlaneWidthSubsampling(PLANAR_U);
            int subH = vi.GetPlaneHeightSubsampling(PLANAR_U);
            int uvWidth = vi.width >> subW;
            int uvHeight = vi.height >> subH;
            int uvLeft = m_finalLeft >> subW;
            int uvTop = m_finalTop >> subH;

            const uint8_t* srcU = src->GetReadPtr(PLANAR_U) + uvTop * src->GetPitch(PLANAR_U) + uvLeft * compSize;
            env->BitBlt(dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U),
                        srcU, src->GetPitch(PLANAR_U),
                        uvWidth * compSize, uvHeight);

            const uint8_t* srcV = src->GetReadPtr(PLANAR_V) + uvTop * src->GetPitch(PLANAR_V) + uvLeft * compSize;
            env->BitBlt(dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V),
                        srcV, src->GetPitch(PLANAR_V),
                        uvWidth * compSize, uvHeight);
        }
    } else {
        int bpp = vi.BitsPerPixel() / 8;
        const uint8_t* srcPtr = src->GetReadPtr() + m_finalTop * src->GetPitch() + m_finalLeft * bpp;
        env->BitBlt(dst->GetWritePtr(), dst->GetPitch(),
                    srcPtr, src->GetPitch(),
                    vi.width * bpp, vi.height);
    }

    static int s_robocrop_count = 0;
    if (++s_robocrop_count <= 10 || s_robocrop_count % 30 == 0) {
        printf("[RoboCrop] Active in pipeline -> Output frame %d cropped: (%d,%d) %dx%d (count=%d)\n",
               n, m_finalLeft, m_finalTop, vi.width, vi.height, s_robocrop_count);
        fflush(stdout);
    }

    return dst;
}

AVSValue __cdecl Create_RoboCrop(AVSValue args, void* user_data, IScriptEnvironment* env) {
    PClip child = args[0].AsClip();
    int samples = args[1].AsInt(64);
    float thresh = (float)args[2].AsFloat(0.0f);
    bool laced = args[3].AsBool(false);
    int wmod = args[4].AsInt(4);
    int hmod = args[5].AsInt(4);
    int rlbt = args[6].AsInt(15);
    bool debug = args[7].AsBool(false);
    float ignore = (float)args[8].AsFloat(0.0f);
    int matrix = args[9].AsInt(0);
    int baffle = args[10].AsInt(2);
    bool scaleRGB = args[11].AsBool(true);
    bool scaleYUV = args[12].AsBool(true);
    int cropMode = args[13].AsInt(0);
    bool blank = args[14].AsBool(false);
    bool blankPC = args[15].AsBool(false);
    bool align = args[16].AsBool(false);
    bool show = args[17].AsBool(false);
    const char* logFn = args[18].AsString("");
    bool logAppend = args[19].AsBool(false);
    float atm = (float)args[20].AsFloat(1.0f);
    int start = args[21].AsInt(0);
    int end = args[22].AsInt(0);
    int leftAdd = args[23].AsInt(0);
    int topAdd = args[24].AsInt(0);
    int rightAdd = args[25].AsInt(0);
    int botAdd = args[26].AsInt(0);
    int leftSkip = args[27].AsInt(0);
    int topSkip = args[28].AsInt(0);
    int rightSkip = args[29].AsInt(0);
    int botSkip = args[30].AsInt(0);
    float scanPerc = (float)args[31].AsFloat(100.0f);
    const char* prefix = args[32].AsString("ROBOCROP_");

    return new RoboCrop(child, samples, thresh, laced, wmod, hmod, rlbt,
                        debug, ignore, matrix, baffle, scaleRGB, scaleYUV,
                        cropMode, blank, blankPC, align, show, logFn,
                        logAppend, atm, start, end, leftAdd, topAdd, rightAdd,
                        botAdd, leftSkip, topSkip, rightSkip, botSkip, scanPerc,
                        prefix, env);
}

__attribute__((visibility("hidden"))) const AVS_Linkage* AVS_linkage = nullptr;

extern "C" __attribute__((visibility("default"))) const char* AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;
    env->AddFunction("RoboCrop", "c[Samples]i[Thresh]f[Laced]b[wMod]i[hMod]i[RLBT]i[Debug]b[Ignore]f[Matrix]i[Baffle]i[ScaleAutoThreshRGB]b[ScaleAutoThreshYUV]b[CropMode]i[Blank]b[BlankPC]b[Align]b[Show]b[LogFn]s[LogAppend]b[ATM]f[Start]i[End]i[LeftAdd]i[TopAdd]i[RightAdd]i[BotAdd]i[LeftSkip]i[TopSkip]i[RightSkip]i[BotSkip]i[ScanPerc]f[Prefix]s", Create_RoboCrop, 0);
    return "RoboCrop plugin for Linux AviSynth+";
}

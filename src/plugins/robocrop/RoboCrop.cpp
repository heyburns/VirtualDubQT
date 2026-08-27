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
      m_ignore(ignore >= 0.0f ? ignore : 0.1f),
      m_matrix(matrix),
      m_baffle(baffle > 0 ? baffle : 4),
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

    if (origWidth <= 0 || origHeight <= 0 || totalFrames <= 0) return;

    if (m_start < 0) m_start = 0;
    if (m_end < m_start || m_end >= totalFrames) m_end = totalFrames - 1;

    int numSamples = std::max(8, std::min(m_samples, 128));

    std::vector<int> leftSamples;
    std::vector<int> topSamples;
    std::vector<int> rightSamples;
    std::vector<int> botSamples;

    leftSamples.reserve(numSamples);
    topSamples.reserve(numSamples);
    rightSamples.reserve(numSamples);
    botSamples.reserve(numSamples);

    int bitsPerComp = vi.BitsPerComponent();
    if (bitsPerComp <= 0) bitsPerComp = 8;
    int bitShift = bitsPerComp - 8;

    int bytesPerSample = vi.ComponentSize();
    if (bytesPerSample <= 0) bytesPerSample = 1;

    for (int s = 0; s < numSamples; ++s) {
        int frameNum = m_start + static_cast<int>(((static_cast<int64_t>(s * 2 + 1)) * (m_end - m_start)) / (2 * numSamples));
        frameNum = std::clamp(frameNum, m_start, m_end);

        PVideoFrame frame;
        try {
            frame = child->GetFrame(frameNum, env);
        } catch (...) {
            continue;
        }
        if (!frame) continue;

        // Auto-threshold determination based on noise floor in corner regions
        int thresh = 0;
        if (m_thresh > 0.0f) {
            thresh = static_cast<int>(std::round(m_thresh * (1 << bitShift)));
        } else {
            // Measure corner baseline black level
            int cornerSum = 0;
            int cornerCount = 0;
            int cornerSize = std::min(16, std::min(origWidth / 8, origHeight / 8));
            if (cornerSize < 2) cornerSize = 2;

            if (vi.IsPlanar()) {
                const uint8_t* srcY = frame->GetReadPtr(PLANAR_Y);
                int pitch = frame->GetPitch(PLANAR_Y);
                for (int cy = 0; cy < cornerSize; ++cy) {
                    for (int cx = 0; cx < cornerSize; ++cx) {
                        int vTL = (bytesPerSample == 2) ? reinterpret_cast<const uint16_t*>(srcY + cy * pitch)[cx] : srcY[cy * pitch + cx];
                        int vTR = (bytesPerSample == 2) ? reinterpret_cast<const uint16_t*>(srcY + cy * pitch)[origWidth - 1 - cx] : srcY[cy * pitch + origWidth - 1 - cx];
                        int vBL = (bytesPerSample == 2) ? reinterpret_cast<const uint16_t*>(srcY + (origHeight - 1 - cy) * pitch)[cx] : srcY[(origHeight - 1 - cy) * pitch + cx];
                        int vBR = (bytesPerSample == 2) ? reinterpret_cast<const uint16_t*>(srcY + (origHeight - 1 - cy) * pitch)[origWidth - 1 - cx] : srcY[(origHeight - 1 - cy) * pitch + origWidth - 1 - cx];
                        cornerSum += (vTL + vTR + vBL + vBR);
                        cornerCount += 4;
                    }
                }
            }

            int avgBlack = cornerCount > 0 ? (cornerSum / cornerCount) : (16 << bitShift);
            int deltaThresh = static_cast<int>(std::round(14.0f * m_atm * (1 << bitShift)));
            thresh = std::clamp(avgBlack + deltaThresh, 20 << bitShift, 70 << bitShift);
        }

        int frameLeft = 0;
        int frameTop = 0;
        int frameRight = 0;
        int frameBot = 0;
        bool frameHasActive = false;

        // Baffle calculation: at least m_baffle pixels OR 1.5% of scan dimension
        int baffleH = std::max(m_baffle, std::max(2, static_cast<int>((origHeight - m_topSkip - m_botSkip) * 0.015f)));
        int baffleW = std::max(m_baffle, std::max(2, static_cast<int>((origWidth - m_leftSkip - m_rightSkip) * 0.015f)));

        if (vi.IsPlanar()) {
            const uint8_t* srcY = frame->GetReadPtr(PLANAR_Y);
            int pitch = frame->GetPitch(PLANAR_Y);

            // Left Scan
            if (m_rlbt & 2) {
                for (int x = m_leftSkip; x < origWidth / 2; ++x) {
                    int count = 0;
                    for (int y = m_topSkip; y < origHeight - m_botSkip; ++y) {
                        int val = (bytesPerSample == 2) ? reinterpret_cast<const uint16_t*>(srcY + y * pitch)[x] : srcY[y * pitch + x];
                        if (val > thresh) {
                            if (++count >= baffleH) break;
                        }
                    }
                    if (count >= baffleH) {
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
                        int val = (bytesPerSample == 2) ? reinterpret_cast<const uint16_t*>(srcY + y * pitch)[x] : srcY[y * pitch + x];
                        if (val > thresh) {
                            if (++count >= baffleH) break;
                        }
                    }
                    if (count >= baffleH) {
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
                        int val = (bytesPerSample == 2) ? reinterpret_cast<const uint16_t*>(srcY + y * pitch)[x] : srcY[y * pitch + x];
                        if (val > thresh) {
                            if (++count >= baffleW) break;
                        }
                    }
                    if (count >= baffleW) {
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
                        int val = (bytesPerSample == 2) ? reinterpret_cast<const uint16_t*>(srcY + y * pitch)[x] : srcY[y * pitch + x];
                        if (val > thresh) {
                            if (++count >= baffleW) break;
                        }
                    }
                    if (count >= baffleW) {
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

            // Top Scan (visual row y -> buffer row origHeight - 1 - y)
            if (m_rlbt & 8) {
                for (int y = m_topSkip; y < origHeight / 2; ++y) {
                    int bufY = origHeight - 1 - y;
                    const uint8_t* row = src + bufY * pitch;
                    int count = 0;
                    for (int x = m_leftSkip; x < origWidth - m_rightSkip; ++x) {
                        int b = row[x * bpp + 0];
                        int g = row[x * bpp + 1];
                        int r = row[x * bpp + 2];
                        int luma = (r * 77 + g * 150 + b * 29) >> 8;
                        if (luma > thresh) {
                            if (++count >= baffleW) break;
                        }
                    }
                    if (count >= baffleW) {
                        frameTop = y;
                        frameHasActive = true;
                        break;
                    }
                }
            }

            // Bottom Scan (visual row origHeight - 1 - y -> buffer row y)
            if (m_rlbt & 4) {
                for (int y = origHeight - 1 - m_botSkip; y >= origHeight / 2; --y) {
                    int bufY = origHeight - 1 - y;
                    const uint8_t* row = src + bufY * pitch;
                    int count = 0;
                    for (int x = m_leftSkip; x < origWidth - m_rightSkip; ++x) {
                        int b = row[x * bpp + 0];
                        int g = row[x * bpp + 1];
                        int r = row[x * bpp + 2];
                        int luma = (r * 77 + g * 150 + b * 29) >> 8;
                        if (luma > thresh) {
                            if (++count >= baffleW) break;
                        }
                    }
                    if (count >= baffleW) {
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
                        int bufY = origHeight - 1 - y;
                        const uint8_t* pixel = src + bufY * pitch + x * bpp;
                        int luma = (pixel[2] * 77 + pixel[1] * 150 + pixel[0] * 29) >> 8;
                        if (luma > thresh) {
                            if (++count >= baffleH) break;
                        }
                    }
                    if (count >= baffleH) {
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
                        int bufY = origHeight - 1 - y;
                        const uint8_t* pixel = src + bufY * pitch + x * bpp;
                        int luma = (pixel[2] * 77 + pixel[1] * 150 + pixel[0] * 29) >> 8;
                        if (luma > thresh) {
                            if (++count >= baffleH) break;
                        }
                    }
                    if (count >= baffleH) {
                        frameRight = (origWidth - 1 - x);
                        frameHasActive = true;
                        break;
                    }
                }
            }
        }

        if (frameHasActive) {
            leftSamples.push_back(frameLeft);
            topSamples.push_back(frameTop);
            rightSamples.push_back(frameRight);
            botSamples.push_back(frameBot);
        }
    }

    int validSamples = static_cast<int>(leftSamples.size());
    int minLeft = 0;
    int minTop = 0;
    int minRight = 0;
    int minBot = 0;

    if (validSamples > 0) {
        std::sort(leftSamples.begin(), leftSamples.end());
        std::sort(topSamples.begin(), topSamples.end());
        std::sort(rightSamples.begin(), rightSamples.end());
        std::sort(botSamples.begin(), botSamples.end());

        // Outlier rejection quantile index (default discard lower ignore fraction)
        int idx = std::clamp(static_cast<int>(std::floor(validSamples * std::clamp(m_ignore, 0.0f, 0.45f))),
                             0, validSamples - 1);

        minLeft = leftSamples[idx];
        minTop = topSamples[idx];
        minRight = rightSamples[idx];
        minBot = botSamples[idx];
    }

    // Safety clamp: do not crop more than 45% of any edge
    if (minLeft > origWidth * 0.45f) minLeft = 0;
    if (minRight > origWidth * 0.45f) minRight = 0;
    if (minTop > origHeight * 0.45f) minTop = 0;
    if (minBot > origHeight * 0.45f) minBot = 0;

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
        printf("[RoboCrop] Crop rect: Left=%d, Top=%d, Width=%d, Height=%d (Right=%d, Bot=%d) [Samples=%d/%d, Thresh=%d]\n",
               m_finalLeft, m_finalTop, m_croppedWidth, m_croppedHeight, m_finalRight, m_finalBot, validSamples, numSamples,
               static_cast<int>(m_thresh));
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
        // AviSynth packed RGB is bottom-up DIB. The visual top row of the cropped image (row m_finalTop)
        // is at memory offset: (origHeight - 1 - (m_finalTop + vi.height - 1)) * pitch.
        int origHeight = child->GetVideoInfo().height;
        int bufY = origHeight - 1 - (m_finalTop + vi.height - 1);
        const uint8_t* srcPtr = src->GetReadPtr() + bufY * src->GetPitch() + m_finalLeft * bpp;
        env->BitBlt(dst->GetWritePtr(), dst->GetPitch(),
                    srcPtr, src->GetPitch(),
                    vi.width * bpp, vi.height);
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
    float ignore = (float)args[8].AsFloat(0.1f);
    int matrix = args[9].AsInt(0);
    int baffle = args[10].AsInt(4);
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

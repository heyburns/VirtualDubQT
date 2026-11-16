// Optional dependency regression probe. Compare a serial reference script with
// a Prefetch script, including reverse seeks, frame zero and retained frames.
// Both scripts must describe the same output. See docs/patches/README.md.
#include <avisynth.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

const AVS_Linkage* AVS_linkage = nullptr;

namespace {
bool pixels(const PVideoFrame& a, const PVideoFrame& b, const VideoInfo& vi) {
    std::vector<int> planes{0};
    if (vi.IsPlanarRGB() || vi.IsPlanarRGBA()) planes = {PLANAR_G, PLANAR_B, PLANAR_R};
    else if (vi.IsPlanar() && !vi.IsY()) planes = {PLANAR_Y, PLANAR_U, PLANAR_V};
    if (vi.IsYUVA() || vi.IsPlanarRGBA()) planes.push_back(PLANAR_A);
    for (int plane : planes) {
        if (a->GetRowSize(plane) != b->GetRowSize(plane) || a->GetHeight(plane) != b->GetHeight(plane))
            return false;
        for (int y = 0; y < a->GetHeight(plane); ++y)
            if (std::memcmp(a->GetReadPtr(plane) + y * a->GetPitch(plane),
                            b->GetReadPtr(plane) + y * b->GetPitch(plane), a->GetRowSize(plane)))
                return false;
    }
    return true;
}

bool properties(const PVideoFrame& a, const PVideoFrame& b, IScriptEnvironment* env) {
    const AVSMap* am = env->getFramePropsRO(a);
    const AVSMap* bm = env->getFramePropsRO(b);
    if (env->propNumKeys(am) != env->propNumKeys(bm)) return false;
    for (int k = 0; k < env->propNumKeys(am); ++k) {
        const char* key = env->propGetKey(am, k);
        const char type = env->propGetType(am, key);
        const int count = env->propNumElements(am, key);
        if (type != env->propGetType(bm, key) || count != env->propNumElements(bm, key)) return false;
        for (int i = 0; i < count; ++i) {
            int ae = 0, be = 0;
            bool equal = false;
            switch (type) {
            case 'i': equal = env->propGetInt(am, key, i, &ae) == env->propGetInt(bm, key, i, &be); break;
            case 'f': equal = env->propGetFloat(am, key, i, &ae) == env->propGetFloat(bm, key, i, &be); break;
            case 's': {
                const int bytes = env->propGetDataSize(am, key, i, &ae);
                const int otherBytes = env->propGetDataSize(bm, key, i, &be);
                if (ae || be || bytes != otherBytes) return false;
                const char* ad = env->propGetData(am, key, i, &ae);
                const char* bd = env->propGetData(bm, key, i, &be);
                equal = !ae && !be && !std::memcmp(ad, bd, bytes);
                break;
            }
            default:
                std::fprintf(stderr, "Unsupported fixture property: %s (%c)\n", key, type);
                return false;
            }
            if (ae || be || !equal) return false;
        }
    }
    return true;
}

bool exercise(const char* reference, const char* candidate, IScriptEnvironment* referenceEnv, IScriptEnvironment* env) {
    PClip serial = referenceEnv->Invoke("Import", reference).AsClip();
    PClip parallel = env->Invoke("Import", candidate).AsClip();
    const VideoInfo& vi = serial->GetVideoInfo();
    const VideoInfo& other = parallel->GetVideoInfo();
    if (vi.width != other.width || vi.height != other.height || vi.pixel_type != other.pixel_type
        || vi.num_frames != other.num_frames || vi.fps_numerator != other.fps_numerator
        || vi.fps_denominator != other.fps_denominator || vi.num_frames < 2) return false;

    // Retaining references must prevent a direct-rendering decoder from
    // overwriting an earlier frame when decoding and seeking again.
    PVideoFrame first = parallel->GetFrame(0, env);
    PVideoFrame firstCopy = env->NewVideoFrame(vi);
    for (int plane : std::vector<int>{0, PLANAR_U, PLANAR_V, PLANAR_A, PLANAR_R, PLANAR_G, PLANAR_B}) {
        // Copy only the actual planes, once each, into independent storage.
        bool valid = plane == 0;
        if (vi.IsPlanarRGB() || vi.IsPlanarRGBA())
            valid = plane == PLANAR_R || plane == PLANAR_G || plane == PLANAR_B;
        else if (vi.IsPlanar() && !vi.IsY())
            valid = plane == 0 || plane == PLANAR_U || plane == PLANAR_V;
        if ((vi.IsYUVA() || vi.IsPlanarRGBA()) && plane == PLANAR_A) valid = true;
        if (valid) env->BitBlt(firstCopy->GetWritePtr(plane), firstCopy->GetPitch(plane),
            first->GetReadPtr(plane), first->GetPitch(plane), first->GetRowSize(plane), first->GetHeight(plane));
    }
    std::vector<int> requests;
    for (int n = 0; n < std::min(vi.num_frames, 96); ++n) requests.push_back(n);
    for (int n : {vi.num_frames - 1, vi.num_frames - 2, 0, 1, vi.num_frames / 2,
                  vi.num_frames / 2 - 1, vi.num_frames / 2 + 1, 0}) requests.push_back(n);
    for (int i = 0; i < 24; ++i) requests.push_back(int((int64_t(i) * 7919) % vi.num_frames));
    for (int n : requests) {
        PVideoFrame expected = serial->GetFrame(n, referenceEnv);
        PVideoFrame actual = parallel->GetFrame(n, env);
        if (!pixels(expected, actual, vi) || !properties(expected, actual, env)) {
            std::fprintf(stderr, "FAIL frame pixels/properties at %d: %s\n", n, candidate);
            return false;
        }
    }
    if (!pixels(first, firstCopy, vi)) {
        std::fprintf(stderr, "FAIL retained frame zero was overwritten\n");
        return false;
    }
    if (vi.audio_samples_per_second != other.audio_samples_per_second || vi.sample_type != other.sample_type
        || vi.nchannels != other.nchannels || vi.num_audio_samples != other.num_audio_samples) return false;
    if (vi.HasAudio()) {
        constexpr int samples = 1024;
        std::vector<unsigned char> expected(vi.BytesFromAudioSamples(samples)), actual(expected.size());
        for (int64_t start : {int64_t(0), int64_t(37), vi.num_audio_samples / 2,
                              std::max<int64_t>(0, vi.num_audio_samples - samples), int64_t(0)}) {
            serial->GetAudio(expected.data(), start, samples, referenceEnv);
            parallel->GetAudio(actual.data(), start, samples, env);
            if (actual != expected) { std::fprintf(stderr, "FAIL audio at %lld\n", (long long)start); return false; }
        }
    }
    std::printf("PASS %zu requests, exact pixels/properties/audio, retained frame: %s\n", requests.size(), candidate);
    return true;
}
}

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "Usage: %s serial.avs prefetch.avs\n", argv[0]); return 2; }
    IScriptEnvironment* env = CreateScriptEnvironment(AVISYNTH_INTERFACE_VERSION);
    if (!env) return 1;
    AVS_linkage = env->GetAVSLinkage();
    IScriptEnvironment* referenceEnv = CreateScriptEnvironment(AVISYNTH_INTERFACE_VERSION);
    if (!referenceEnv) { env->DeleteScriptEnvironment(); return 1; }
    bool passed = false;
    try { passed = exercise(argv[1], argv[2], referenceEnv, env); }
    catch (const AvisynthError& error) { std::fprintf(stderr, "FAIL %s\n", error.msg); }
    env->DeleteScriptEnvironment();
    referenceEnv->DeleteScriptEnvironment();
    return passed ? 0 : 1;
}

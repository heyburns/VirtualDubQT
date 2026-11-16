// Dependency regression test, built against the matching AviSynth source tree.
// Use ResampleAudio::Create directly: an outer AviSynth cache or MT guard must
// not accidentally provide the serialization we are testing inside GetAudio.
// See docs/patches/README.md. No installed plugins or media files are needed.
#include <avisynth.h>
#include "core/internal.h"
#define AVS_UNUSED(value) (void)(value)
#include "core/audio.h"
#undef AVS_UNUSED

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

const AVS_Linkage* AVS_linkage = nullptr;

namespace {
struct Activity {
    std::atomic<int> audio{0}, video{0}, audioPeak{0}, videoPeak{0};
    std::atomic<bool> overlap{false};
};
void peak(std::atomic<int>& maximum, int value) {
    int previous = maximum.load();
    while (previous < value && !maximum.compare_exchange_weak(previous, value)) {}
}

class Source final : public IClip {
    VideoInfo info{};
    std::shared_ptr<Activity> activity;
public:
    Source(int format, int rate, std::shared_ptr<Activity> state)
        : activity(std::move(state)) {
        info.width = 64; info.height = 48; info.pixel_type = VideoInfo::CS_BGR32;
        info.num_frames = 240; info.fps_numerator = 24; info.fps_denominator = 1;
        info.audio_samples_per_second = rate; info.sample_type = format;
        info.nchannels = 2; info.num_audio_samples = rate * 10;
    }
    const VideoInfo& __stdcall GetVideoInfo() override { return info; }
    bool __stdcall GetParity(int) override { return false; }
    int __stdcall SetCacheHints(int hint, int) override {
        return hint == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    }
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {
        peak(activity->videoPeak, ++activity->video);
        if (activity->audio.load()) activity->overlap = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        PVideoFrame result = env->NewVideoFrame(info);
        for (int y = 0; y < result->GetHeight(); ++y)
            std::memset(result->GetWritePtr() + y * result->GetPitch(), n & 255, result->GetRowSize());
        --activity->video;
        return result;
    }
    void __stdcall GetAudio(void* output, int64_t start, int64_t count, IScriptEnvironment*) override {
        peak(activity->audioPeak, ++activity->audio);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for (int64_t i = 0; i < count; ++i) {
            const int64_t sample = start + i;
            const int value = sample >= 0 && sample < info.num_audio_samples
                ? int((sample * 17) % 2048) - 1024 : 0;
            for (int channel = 0; channel < info.nchannels; ++channel) {
                const int64_t offset = i * info.nchannels + channel;
                if (info.sample_type == SAMPLE_FLOAT)
                    static_cast<float*>(output)[offset] = value / 2048.0F;
                else if (info.sample_type == SAMPLE_INT16)
                    static_cast<int16_t*>(output)[offset] = int16_t(value * 16);
                else
                    static_cast<int32_t*>(output)[offset] = value * 1048576;
            }
        }
        --activity->audio;
    }
};

PClip resample(IScriptEnvironment* env, int format, int rate, const std::shared_ptr<Activity>& activity) {
    AVSValue args[] = {PClip(new Source(format, rate, activity)), 48000, 1};
    return ResampleAudio::Create(AVSValue(args, 3), nullptr, env).AsClip();
}

bool exercise(IScriptEnvironment* env, int format, int rate) {
    auto state = std::make_shared<Activity>();
    PClip actual = resample(env, format, rate, state);
    PClip reference = resample(env, format, rate, std::make_shared<Activity>());
    if (actual->SetCacheHints(CACHE_GET_MTMODE, 0) != MT_NICE_FILTER) {
        std::fprintf(stderr, "FAIL: ResampleAudio still serializes the video path\n");
        return false;
    }
    constexpr int windows = 32, samples = 1024, audioThreads = 4, videoThreads = 8;
    std::vector<std::vector<unsigned char>> expected(windows);
    for (int i = 0; i < windows; ++i) {
        expected[i].resize(actual->GetVideoInfo().BytesFromAudioSamples(samples));
        // Closely spaced windows also exercise overlapping retained input.
        reference->GetAudio(expected[i].data(), i * 37, samples, env);
    }
    std::atomic<bool> go{false}, passed{true};
    std::atomic<int> ready{0};
    std::vector<std::thread> workers;
    for (int thread = 0; thread < audioThreads; ++thread) {
        workers.emplace_back([&, thread] {
            ++ready;
            while (!go) std::this_thread::yield();
            for (int i = 0; i < windows; ++i) {
                const int window = (i * 13 + thread * 7) % windows;
                std::vector<unsigned char> bytes(expected[window].size());
                actual->GetAudio(bytes.data(), window * 37, samples, env);
                if (bytes != expected[window]) passed = false;
            }
        });
    }
    for (int thread = 0; thread < videoThreads; ++thread) {
        workers.emplace_back([&, thread] {
            ++ready;
            while (!go) std::this_thread::yield();
            for (int i = 0; i < 12; ++i) {
                const int frame = thread * 12 + i;
                PVideoFrame image = actual->GetFrame(frame, env);
                if (!image || image->GetReadPtr()[0] != (frame & 255)) passed = false;
            }
        });
    }
    while (ready != audioThreads + videoThreads) std::this_thread::yield();
    go = true;
    for (auto& worker : workers) worker.join();
    const bool good = passed && state->audioPeak == 1 && state->videoPeak > 1 && state->overlap;
    std::printf("%s format=%d rate=%d audio_peak=%d video_peak=%d overlap=%d exact_pcm=%d\n",
        good ? "PASS" : "FAIL", format, rate, state->audioPeak.load(), state->videoPeak.load(),
        int(state->overlap.load()), int(passed.load()));
    return good;
}
}

int main() {
    IScriptEnvironment* env = CreateScriptEnvironment(AVISYNTH_INTERFACE_VERSION);
    if (!env) return 1;
    AVS_linkage = env->GetAVSLinkage();
    bool passed = true;
    try {
        passed &= exercise(env, SAMPLE_FLOAT, 44100);
        passed &= exercise(env, SAMPLE_INT16, 44100);
        passed &= exercise(env, SAMPLE_FLOAT, 96000); // downsampling
        passed &= exercise(env, SAMPLE_FLOAT, 48000); // skip_conversion
        passed &= exercise(env, SAMPLE_INT16, 48000);
        passed &= exercise(env, SAMPLE_INT32, 48000); // directly-owned ConvertAudio
    } catch (const AvisynthError& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.msg); passed = false;
    }
    env->DeleteScriptEnvironment();
    return passed ? 0 : 1;
}

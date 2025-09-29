#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include "audio_engine.pb.h"
#include "EdlStore.h"

namespace juceaudioservice {

/**
 * Compiles validated EDL proto into internal timeline representation.
 *
 * Converts EDL structure into sorted timeline with precomputed gains,
 * fade specifications, and crossfade detection for efficient rendering.
 */
class EdlCompiler {
public:
    enum class FadeShape {
        Linear,
        EqualPower
    };

    struct FadeSpec {
        int64_t length_samples = 0;
        FadeShape shape = FadeShape::Linear;

        bool isEmpty() const { return length_samples == 0; }
    };

    struct CompiledClip {
        const audio_engine::Clip* clip = nullptr;
        const audio_engine::AudioRef* media = nullptr;
        int64_t t0 = 0;                // timeline start (samples)
        int64_t t1 = 0;                // timeline end (exclusive)
        float gain_linear = 1.0f;      // from gain_db
        FadeSpec fade_in;
        FadeSpec fade_out;
    };

    struct CompiledTrack {
        std::vector<CompiledClip> clips;
        float gain_linear = 1.0f;
        bool muted = false;
    };

    struct CompiledEdl {
        int sample_rate = 0;
        std::vector<CompiledTrack> tracks;
    };

    EdlCompiler();
    ~EdlCompiler() = default;

    /**
     * Compile an EDL snapshot into internal representation.
     *
     * @param snapshot Validated EDL snapshot from EdlStore
     * @param compiled Output parameter for compiled EDL
     * @param error Output parameter for compilation error message
     * @return true if compilation succeeded
     */
    bool compile(const EdlStore::Snapshot& snapshot, CompiledEdl& compiled, std::string& error);

private:
    // Helper methods
    float dbToLinear(float db);
    FadeSpec convertFade(const audio_engine::Fade& fade);
    const audio_engine::AudioRef* findMediaById(const audio_engine::Edl& edl, const std::string& mediaId);
    bool compileTrack(const audio_engine::Track& track, const audio_engine::Edl& edl,
                     CompiledTrack& compiledTrack, std::string& error);
    void sortClipsByTimeline(std::vector<CompiledClip>& clips);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EdlCompiler)
};

} // namespace juceaudioservice
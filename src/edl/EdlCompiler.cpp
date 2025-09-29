#include "EdlCompiler.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace juceaudioservice {

EdlCompiler::EdlCompiler() = default;

bool EdlCompiler::compile(const EdlStore::Snapshot& snapshot, CompiledEdl& compiled, std::string& error) {
    const auto& edl = snapshot.edl;

    std::cout << "[EDL][Compile] Starting compilation for EDL: " << edl.id()
              << " revision: " << snapshot.revision << std::endl;

    // Initialize compiled EDL
    compiled.sample_rate = edl.sample_rate();
    compiled.tracks.clear();
    compiled.tracks.reserve(edl.tracks().size());

    // Compile each track
    for (const auto& track : edl.tracks()) {
        CompiledTrack compiledTrack;
        if (!compileTrack(track, edl, compiledTrack, error)) {
            return false;
        }
        compiled.tracks.push_back(std::move(compiledTrack));
    }

    std::cout << "[EDL][Compile] Successfully compiled " << compiled.tracks.size()
              << " tracks for EDL: " << edl.id() << std::endl;

    return true;
}

bool EdlCompiler::compileTrack(const audio_engine::Track& track, const audio_engine::Edl& edl,
                              CompiledTrack& compiledTrack, std::string& error) {

    // Set track properties
    compiledTrack.gain_linear = dbToLinear(track.gain_db());
    compiledTrack.muted = track.muted();
    compiledTrack.clips.clear();
    compiledTrack.clips.reserve(track.clips().size());

    // Compile each clip
    for (const auto& clip : track.clips()) {
        const audio_engine::AudioRef* media = findMediaById(edl, clip.media_id());
        if (!media) {
            error = "Media not found for clip " + clip.id() + ": " + clip.media_id();
            return false;
        }

        CompiledClip compiledClip;
        compiledClip.clip = &clip;
        compiledClip.media = media;
        compiledClip.t0 = clip.start_in_timeline();
        compiledClip.t1 = clip.start_in_timeline() + clip.duration();
        compiledClip.gain_linear = dbToLinear(clip.gain_db());

        // Convert fades
        if (clip.has_fade_in()) {
            compiledClip.fade_in = convertFade(clip.fade_in());
        }
        if (clip.has_fade_out()) {
            compiledClip.fade_out = convertFade(clip.fade_out());
        }

        compiledTrack.clips.push_back(compiledClip);
    }

    // Sort clips by timeline position
    sortClipsByTimeline(compiledTrack.clips);

    return true;
}

float EdlCompiler::dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

EdlCompiler::FadeSpec EdlCompiler::convertFade(const audio_engine::Fade& fade) {
    FadeSpec spec;
    spec.length_samples = fade.duration_samples();

    switch (fade.shape()) {
        case audio_engine::Fade::LINEAR:
            spec.shape = FadeShape::Linear;
            break;
        case audio_engine::Fade::EQUAL_POWER:
            spec.shape = FadeShape::EqualPower;
            break;
        default:
            spec.shape = FadeShape::Linear;
            break;
    }

    return spec;
}

const audio_engine::AudioRef* EdlCompiler::findMediaById(const audio_engine::Edl& edl, const std::string& mediaId) {
    for (const auto& media : edl.media()) {
        if (media.id() == mediaId) {
            return &media;
        }
    }
    return nullptr;
}

void EdlCompiler::sortClipsByTimeline(std::vector<CompiledClip>& clips) {
    std::stable_sort(clips.begin(), clips.end(),
        [](const CompiledClip& a, const CompiledClip& b) {
            return a.t0 < b.t0;
        });
}

} // namespace juceaudioservice
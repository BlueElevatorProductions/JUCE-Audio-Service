#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include "audio_engine.pb.h"
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace juceaudioservice {

/**
 * Thread-safe storage for EDL data with validation and versioning.
 *
 * Manages the current active EDL, validates incoming EDL data,
 * and provides atomic read/write operations for concurrent access.
 */
class EdlStore {
public:
    struct Snapshot {
        audio_engine::Edl edl;
        std::string revision;
        int track_count;
        int clip_count;
    };

    EdlStore();
    ~EdlStore() = default;

    /**
     * Replace the current EDL with validation.
     *
     * @param edl The new EDL to store
     * @param out_snapshot Output parameter for the validated snapshot
     * @param error Output parameter for validation error message
     * @return true if validation succeeded and EDL was stored
     */
    bool replace(const audio_engine::Edl& edl, Snapshot& out_snapshot, std::string& error);

    /**
     * Get the current EDL snapshot.
     *
     * @return Optional snapshot of current EDL, empty if none set
     */
    std::optional<Snapshot> get() const;

    /**
     * Check if an EDL is currently loaded.
     *
     * @return true if an EDL is available
     */
    bool hasEdl() const;

private:
    mutable std::mutex mutex_;
    std::optional<Snapshot> current_;
    juce::AudioFormatManager formatManager_;

    // Validation methods
    bool validateEdl(const audio_engine::Edl& edl, std::string& error);
    bool validateSampleRate(int32_t sampleRate, std::string& error);
    bool validateMedia(const audio_engine::Edl& edl, std::string& error);
    bool validateTracks(const audio_engine::Edl& edl, std::string& error);
    bool validateClip(const audio_engine::Clip& clip, const audio_engine::Edl& edl, std::string& error);
    bool validateFade(const audio_engine::Fade& fade, const std::string& fadeType, std::string& error);

    // Helper methods
    std::string calculateRevision(const audio_engine::Edl& edl);
    std::string calculateSHA256(const std::string& data);
    const audio_engine::AudioRef* findMediaById(const audio_engine::Edl& edl, const std::string& mediaId);
    juce::int64 getMediaLengthInSamples(const audio_engine::AudioRef& media);
    void countTracksAndClips(const audio_engine::Edl& edl, int& trackCount, int& clipCount);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EdlStore)
};

} // namespace juceaudioservice
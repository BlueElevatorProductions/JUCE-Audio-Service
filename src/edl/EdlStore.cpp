#include "EdlStore.h"
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <google/protobuf/util/json_util.h>

namespace juceaudioservice {

EdlStore::EdlStore() {
    formatManager_.registerBasicFormats();
}

bool EdlStore::replace(const audio_engine::Edl& edl, Snapshot& out_snapshot, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Validate the EDL
    if (!validateEdl(edl, error)) {
        return false;
    }

    // Create new snapshot
    Snapshot newSnapshot;
    newSnapshot.edl = edl;
    newSnapshot.revision = calculateRevision(edl);
    countTracksAndClips(edl, newSnapshot.track_count, newSnapshot.clip_count);

    // Update revision if it was empty or different content
    if (edl.revision().empty() || edl.revision() != newSnapshot.revision) {
        newSnapshot.edl.set_revision(newSnapshot.revision);
    }

    // Store the new snapshot
    current_ = newSnapshot;
    out_snapshot = newSnapshot;

    return true;
}

std::optional<EdlStore::Snapshot> EdlStore::get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

bool EdlStore::hasEdl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_.has_value();
}

bool EdlStore::validateEdl(const audio_engine::Edl& edl, std::string& error) {
    // Check EDL ID
    if (edl.id().empty()) {
        error = "EDL ID cannot be empty";
        return false;
    }

    // Validate sample rate
    if (!validateSampleRate(edl.sample_rate(), error)) {
        return false;
    }

    // Validate media references
    if (!validateMedia(edl, error)) {
        return false;
    }

    // Validate tracks
    if (!validateTracks(edl, error)) {
        return false;
    }

    return true;
}

bool EdlStore::validateSampleRate(int32_t sampleRate, std::string& error) {
    if (sampleRate != 44100 && sampleRate != 48000 && sampleRate != 96000) {
        error = "Sample rate must be 44100, 48000, or 96000 Hz, got " + std::to_string(sampleRate);
        return false;
    }
    return true;
}

bool EdlStore::validateMedia(const audio_engine::Edl& edl, std::string& error) {
    if (edl.media().empty()) {
        error = "EDL must contain at least one media reference";
        return false;
    }

    for (const auto& media : edl.media()) {
        if (media.id().empty()) {
            error = "Media ID cannot be empty";
            return false;
        }

        if (media.path().empty()) {
            error = "Media path cannot be empty for media ID: " + media.id();
            return false;
        }

        // Check if file exists
        juce::File file(media.path());
        if (!file.existsAsFile()) {
            error = "Media file not found: " + media.path();
            return false;
        }

        // Validate audio format and get properties
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
        if (!reader) {
            error = "Unsupported or unreadable audio file: " + media.path();
            return false;
        }

        // Check sample rate consistency
        int32_t fileSampleRate = static_cast<int32_t>(reader->sampleRate);
        if (media.sample_rate() != 0 && media.sample_rate() != fileSampleRate) {
            error = "Media sample rate mismatch for " + media.id() +
                   ": specified " + std::to_string(media.sample_rate()) +
                   " but file is " + std::to_string(fileSampleRate);
            return false;
        }

        // All media must match EDL sample rate
        if (fileSampleRate != edl.sample_rate()) {
            error = "Media sample rate mismatch for " + media.id() +
                   ": file is " + std::to_string(fileSampleRate) +
                   " but EDL requires " + std::to_string(edl.sample_rate());
            return false;
        }
    }

    return true;
}

bool EdlStore::validateTracks(const audio_engine::Edl& edl, std::string& error) {
    if (edl.tracks().empty()) {
        error = "EDL must contain at least one track";
        return false;
    }

    for (const auto& track : edl.tracks()) {
        if (track.id().empty()) {
            error = "Track ID cannot be empty";
            return false;
        }

        for (const auto& clip : track.clips()) {
            if (!validateClip(clip, edl, error)) {
                return false;
            }
        }
    }

    return true;
}

bool EdlStore::validateClip(const audio_engine::Clip& clip, const audio_engine::Edl& edl, std::string& error) {
    if (clip.id().empty()) {
        error = "Clip ID cannot be empty";
        return false;
    }

    if (clip.media_id().empty()) {
        error = "Clip media_id cannot be empty for clip: " + clip.id();
        return false;
    }

    // Find the referenced media
    const audio_engine::AudioRef* media = findMediaById(edl, clip.media_id());
    if (!media) {
        error = "Media not found for clip " + clip.id() + ": " + clip.media_id();
        return false;
    }

    // Validate timing
    if (clip.start_in_media() < 0) {
        error = "Clip start_in_media must be non-negative for clip: " + clip.id();
        return false;
    }

    if (clip.duration() <= 0) {
        error = "Clip duration must be positive for clip: " + clip.id();
        return false;
    }

    if (clip.start_in_timeline() < 0) {
        error = "Clip start_in_timeline must be non-negative for clip: " + clip.id();
        return false;
    }

    // Check bounds against media length
    juce::int64 mediaLength = getMediaLengthInSamples(*media);
    if (clip.start_in_media() + clip.duration() > mediaLength) {
        error = "Clip extends beyond media end for clip " + clip.id() +
               ": start=" + std::to_string(clip.start_in_media()) +
               " duration=" + std::to_string(clip.duration()) +
               " but media length=" + std::to_string(mediaLength);
        return false;
    }

    // Validate fades
    if (clip.has_fade_in() && !validateFade(clip.fade_in(), "fade_in", error)) {
        error = "Invalid fade_in for clip " + clip.id() + ": " + error;
        return false;
    }

    if (clip.has_fade_out() && !validateFade(clip.fade_out(), "fade_out", error)) {
        error = "Invalid fade_out for clip " + clip.id() + ": " + error;
        return false;
    }

    return true;
}

bool EdlStore::validateFade(const audio_engine::Fade& fade, const std::string& fadeType, std::string& error) {
    if (fade.duration_samples() < 0) {
        error = fadeType + " duration must be non-negative";
        return false;
    }

    if (fade.shape() != audio_engine::Fade::LINEAR && fade.shape() != audio_engine::Fade::EQUAL_POWER) {
        error = fadeType + " shape must be LINEAR or EQUAL_POWER";
        return false;
    }

    return true;
}

std::string EdlStore::calculateRevision(const audio_engine::Edl& edl) {
    // Convert to JSON for stable hashing
    std::string jsonString;
    auto status = google::protobuf::util::MessageToJsonString(edl, &jsonString);
    (void)status; // Suppress unused variable warning

    std::string hash = calculateSHA256(jsonString);
    return hash.substr(0, 12); // First 12 characters
}

std::string EdlStore::calculateSHA256(const std::string& data) {
    unsigned char hash[32]; // SHA256 produces 32 bytes
    unsigned int hashLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data.c_str(), data.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &hashLen) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < hashLen; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

const audio_engine::AudioRef* EdlStore::findMediaById(const audio_engine::Edl& edl, const std::string& mediaId) {
    for (const auto& media : edl.media()) {
        if (media.id() == mediaId) {
            return &media;
        }
    }
    return nullptr;
}

juce::int64 EdlStore::getMediaLengthInSamples(const audio_engine::AudioRef& media) {
    juce::File file(media.path());
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (!reader) {
        return 0;
    }
    return reader->lengthInSamples;
}

void EdlStore::countTracksAndClips(const audio_engine::Edl& edl, int& trackCount, int& clipCount) {
    trackCount = static_cast<int>(edl.tracks().size());
    clipCount = 0;

    for (const auto& track : edl.tracks()) {
        clipCount += static_cast<int>(track.clips().size());
    }
}

} // namespace juceaudioservice
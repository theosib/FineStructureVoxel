#pragma once

/**
 * @file audio_engine.hpp
 * @brief Audio engine wrapping miniaudio with 3D spatialization
 *
 * Uses pimpl pattern to hide miniaudio from the header.
 * Drains SoundEventQueue each frame and plays sounds via ma_engine.
 */

#include "finevox/core/sound_event.hpp"
#include "finevox/core/sound_registry.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace finevox::audio {

struct AudioConfig {
    float masterVolume = 1.0f;
    float effectsVolume = 1.0f;
    float musicVolume = 0.5f;
    float ambientVolume = 0.7f;
    float uiVolume = 1.0f;
    uint32_t maxSimultaneousSounds = 32;
    float maxSoundDistance = 64.0f;  // Sounds beyond this are culled
};

class AudioEngine {
public:
    explicit AudioEngine(SoundEventQueue& eventQueue,
                         const SoundRegistry& registry);
    ~AudioEngine();

    // Non-copyable, non-movable
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// Initialize the audio device and engine
    bool initialize(const AudioConfig& config = {});

    /// Shut down the audio engine
    void shutdown();

    /// Called each frame — drains event queue, updates listener, plays sounds
    void update(glm::dvec3 listenerWorldPos, glm::vec3 forward, glm::vec3 up);

    /// Set volume for a sound category (0.0 - 1.0)
    void setVolume(SoundCategory category, float volume);

    /// Get volume for a sound category
    [[nodiscard]] float volume(SoundCategory category) const;

    /// Play background music (streaming)
    void playMusic(const std::string& trackPath, bool loop = true, float fadeInSeconds = 2.0f);

    /// Stop background music
    void stopMusic(float fadeOutSeconds = 2.0f);

    /// Check if music is playing
    [[nodiscard]] bool isMusicPlaying() const;

    /// Get number of currently active sounds
    [[nodiscard]] size_t activeSoundCount() const;

    /// Check if engine is initialized
    [[nodiscard]] bool isInitialized() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace finevox::audio

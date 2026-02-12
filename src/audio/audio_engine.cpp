#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "finevox/audio/audio_engine.hpp"

#include <random>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace finevox::audio {

// ============================================================================
// AudioEngine::Impl - Hidden implementation
// ============================================================================

struct AudioEngine::Impl {
    ma_engine engine{};
    bool engineInitialized = false;

    SoundEventQueue& eventQueue;
    const SoundRegistry& registry;
    AudioConfig config;

    // Listener world position (updated each frame)
    glm::dvec3 listenerWorldPos{0.0};

    // Volume levels per category
    float volumes[5] = {1.0f, 1.0f, 0.5f, 0.7f, 1.0f};

    // Active sounds and cleanup queue
    std::vector<ma_sound*> activeSoundList;
    std::vector<ma_sound*> finishedSounds;
    std::mutex finishedMutex;

    // Random engine for variant selection and pitch variation
    std::mt19937 rng{std::random_device{}()};

    Impl(SoundEventQueue& queue, const SoundRegistry& reg)
        : eventQueue(queue), registry(reg) {}

    ~Impl() {
        if (engineInitialized) {
            // Stop and clean up all active sounds before engine shutdown
            for (auto* s : activeSoundList) {
                ma_sound_uninit(s);
                delete s;
            }
            activeSoundList.clear();

            std::lock_guard lock(finishedMutex);
            for (auto* s : finishedSounds) {
                ma_sound_uninit(s);
                delete s;
            }
            finishedSounds.clear();

            ma_engine_uninit(&engine);
        }
    }

    void cleanupFinished() {
        // Move finished sounds out of the lock
        std::vector<ma_sound*> toClean;
        {
            std::lock_guard lock(finishedMutex);
            toClean.swap(finishedSounds);
        }

        // Uninit and delete on the main thread (safe)
        for (auto* s : toClean) {
            ma_sound_uninit(s);
            delete s;
        }

        // Also remove from active list
        activeSoundList.erase(
            std::remove_if(activeSoundList.begin(), activeSoundList.end(),
                [](ma_sound* s) { return ma_sound_at_end(s); }),
            activeSoundList.end());
    }

    float getCategoryVolume(SoundCategory cat) const {
        return volumes[static_cast<size_t>(SoundCategory::Master)] *
               volumes[static_cast<size_t>(cat)];
    }

    void processEvent(const SoundEvent& event) {
        if (!engineInitialized) return;

        // Look up the sound set definition
        auto* def = registry.getSoundSet(event.soundSet);
        if (!def) return;

        // Find the action group
        auto* group = def->getAction(event.action);
        if (!group || group->empty()) return;

        // Select a random variant
        std::uniform_int_distribution<size_t> dist(0, group->size() - 1);
        const auto& variant = group->variants[dist(rng)];

        // Compute relative position (listener at origin)
        glm::vec3 relativePos = event.position() - glm::vec3(listenerWorldPos);
        float distance = glm::length(relativePos);

        // Distance culling
        if (event.positional && distance > config.maxSoundDistance) {
            return;
        }

        // Compute final volume
        float categoryVol = getCategoryVolume(event.category);
        float finalVolume = event.volume * categoryVol * variant.volumeScale * def->volume;

        // Apply pitch variance
        std::uniform_real_distribution<float> pitchDist(
            -def->pitchVariance, def->pitchVariance);
        float finalPitch = event.pitch * variant.pitchScale + pitchDist(rng);
        finalPitch = std::clamp(finalPitch, 0.5f, 2.0f);

        // Play the sound
        // For positional sounds, we use ma_engine_play_sound_ex (fire-and-forget)
        // miniaudio handles spatialization based on listener/sound positions

        // Create a sound inline (fire-and-forget)
        ma_sound* sound = new ma_sound;
        ma_result result = ma_sound_init_from_file(
            &engine, variant.path.c_str(),
            MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,
            nullptr, nullptr, sound);

        if (result != MA_SUCCESS) {
            delete sound;
            return;
        }

        ma_sound_set_volume(sound, finalVolume);
        ma_sound_set_pitch(sound, finalPitch);

        if (event.positional) {
            ma_sound_set_spatialization_enabled(sound, MA_TRUE);
            ma_sound_set_position(sound, relativePos.x, relativePos.y, relativePos.z);
            ma_sound_set_min_distance(sound, 1.0f);
            ma_sound_set_max_distance(sound, config.maxSoundDistance);
            ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
        } else {
            ma_sound_set_spatialization_enabled(sound, MA_FALSE);
        }

        // End callback queues the sound for main-thread cleanup
        // (ma_sound_uninit from the audio thread deadlocks)
        ma_sound_set_end_callback(sound, [](void* pUserData, ma_sound* pSound) {
            auto* impl = static_cast<Impl*>(pUserData);
            std::lock_guard lock(impl->finishedMutex);
            impl->finishedSounds.push_back(pSound);
        }, this);

        ma_sound_start(sound);
        activeSoundList.push_back(sound);
    }
};

// ============================================================================
// AudioEngine
// ============================================================================

AudioEngine::AudioEngine(SoundEventQueue& eventQueue, const SoundRegistry& registry)
    : impl_(std::make_unique<Impl>(eventQueue, registry)) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize(const AudioConfig& config) {
    if (impl_->engineInitialized) return true;

    impl_->config = config;

    // Apply initial volumes
    impl_->volumes[static_cast<size_t>(SoundCategory::Master)] = config.masterVolume;
    impl_->volumes[static_cast<size_t>(SoundCategory::Effects)] = config.effectsVolume;
    impl_->volumes[static_cast<size_t>(SoundCategory::Music)] = config.musicVolume;
    impl_->volumes[static_cast<size_t>(SoundCategory::Ambient)] = config.ambientVolume;
    impl_->volumes[static_cast<size_t>(SoundCategory::UI)] = config.uiVolume;

    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.listenerCount = 1;

    ma_result result = ma_engine_init(&engineConfig, &impl_->engine);
    if (result != MA_SUCCESS) {
        return false;
    }

    impl_->engineInitialized = true;

    // Set listener to origin
    ma_engine_listener_set_position(&impl_->engine, 0, 0.0f, 0.0f, 0.0f);

    return true;
}

void AudioEngine::shutdown() {
    if (impl_ && impl_->engineInitialized) {
        ma_engine_uninit(&impl_->engine);
        impl_->engineInitialized = false;
    }
}

void AudioEngine::update(glm::dvec3 listenerWorldPos, glm::vec3 forward, glm::vec3 up) {
    if (!impl_->engineInitialized) return;

    // Update listener state
    impl_->listenerWorldPos = listenerWorldPos;

    // Listener is always at origin in audio space
    ma_engine_listener_set_position(&impl_->engine, 0, 0.0f, 0.0f, 0.0f);
    ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&impl_->engine, 0, up.x, up.y, up.z);

    // Clean up sounds that finished playing
    impl_->cleanupFinished();

    // Drain and process all queued sound events
    auto events = impl_->eventQueue.drainAll();
    for (const auto& event : events) {
        impl_->processEvent(event);
    }
}

void AudioEngine::setVolume(SoundCategory category, float volume) {
    impl_->volumes[static_cast<size_t>(category)] = std::clamp(volume, 0.0f, 1.0f);

    // Update master volume on the engine
    if (category == SoundCategory::Master && impl_->engineInitialized) {
        ma_engine_set_volume(&impl_->engine, volume);
    }
}

float AudioEngine::volume(SoundCategory category) const {
    return impl_->volumes[static_cast<size_t>(category)];
}

void AudioEngine::playMusic(const std::string& trackPath, bool loop, float /*fadeInSeconds*/) {
    if (!impl_->engineInitialized) return;

    // Simple implementation: fire and forget with looping
    ma_engine_play_sound(&impl_->engine, trackPath.c_str(), nullptr);
    (void)loop;  // TODO: proper music track management with fade
}

void AudioEngine::stopMusic(float /*fadeOutSeconds*/) {
    // TODO: proper music track management with fade
}

bool AudioEngine::isMusicPlaying() const {
    return false;  // TODO: proper music track management
}

size_t AudioEngine::activeSoundCount() const {
    return impl_->activeSoundList.size();
}

bool AudioEngine::isInitialized() const {
    return impl_ && impl_->engineInitialized;
}

}  // namespace finevox::audio

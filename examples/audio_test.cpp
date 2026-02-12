/**
 * @file audio_test.cpp
 * @brief Minimal audio test - generates a WAV tone and plays it through AudioEngine
 *
 * Build with: cmake -DFINEVOX_BUILD_AUDIO=ON ..
 * Run from build directory: ./finevox_audio_test
 */

#include <finevox/audio/audio_engine.hpp>
#include <finevox/core/sound_event.hpp>
#include <finevox/core/sound_registry.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>

using namespace finevox;
using namespace finevox::audio;

// ============================================================================
// WAV file generator
// ============================================================================

struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;  // PCM
    uint16_t numChannels = 1;
    uint32_t sampleRate = 44100;
    uint32_t byteRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};

/// Generate a WAV file with a sine wave tone
bool generateWav(const std::string& path, float frequency, float durationSec, float amplitude = 0.5f) {
    const uint32_t sampleRate = 44100;
    const uint16_t bitsPerSample = 16;
    const uint16_t numChannels = 1;
    const uint32_t numSamples = static_cast<uint32_t>(sampleRate * durationSec);
    const uint32_t dataSize = numSamples * numChannels * (bitsPerSample / 8);

    WavHeader header;
    header.numChannels = numChannels;
    header.sampleRate = sampleRate;
    header.bitsPerSample = bitsPerSample;
    header.blockAlign = numChannels * (bitsPerSample / 8);
    header.byteRate = sampleRate * header.blockAlign;
    header.dataSize = dataSize;
    header.fileSize = sizeof(WavHeader) - 8 + dataSize;

    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write sine wave samples with fade-in/fade-out to avoid clicks
    const float fadeTime = 0.01f;  // 10ms fade
    const uint32_t fadeSamples = static_cast<uint32_t>(sampleRate * fadeTime);

    for (uint32_t i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float sample = amplitude * std::sin(2.0f * 3.14159265f * frequency * t);

        // Fade in
        if (i < fadeSamples) {
            sample *= static_cast<float>(i) / fadeSamples;
        }
        // Fade out
        if (i > numSamples - fadeSamples) {
            sample *= static_cast<float>(numSamples - i) / fadeSamples;
        }

        int16_t pcm = static_cast<int16_t>(sample * 32767.0f);
        file.write(reinterpret_cast<const char*>(&pcm), sizeof(pcm));
    }

    return file.good();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== FineVox Audio Test ===\n\n";

    // Create temp directory for generated sounds
    std::filesystem::path soundDir = "test_sounds";
    std::filesystem::create_directories(soundDir);

    // Generate test WAV files
    std::cout << "Generating test WAV files...\n";

    std::string tonePath = (soundDir / "tone_440hz.wav").string();
    std::string highPath = (soundDir / "tone_880hz.wav").string();
    std::string lowPath  = (soundDir / "tone_220hz.wav").string();
    std::string chordPath = (soundDir / "tone_554hz.wav").string();

    generateWav(tonePath, 440.0f, 0.5f);   // A4 - half second
    generateWav(highPath, 880.0f, 0.3f);    // A5 - short
    generateWav(lowPath,  220.0f, 0.4f);    // A3
    generateWav(chordPath, 554.37f, 0.3f);  // C#5

    std::cout << "  Created: " << tonePath << "\n";
    std::cout << "  Created: " << highPath << "\n";
    std::cout << "  Created: " << lowPath << "\n";
    std::cout << "  Created: " << chordPath << "\n\n";

    // Register a sound set
    SoundSetDefinition testSet;
    testSet.name = "test:tone";
    testSet.volume = 1.0f;
    testSet.pitchVariance = 0.0f;  // No random pitch for testing

    // "Place" action has the A4 tone
    SoundGroup placeGroup;
    placeGroup.variants.push_back({tonePath, 1.0f, 1.0f});
    testSet.actions[SoundAction::Place] = std::move(placeGroup);

    // "Break" action has the high tone
    SoundGroup breakGroup;
    breakGroup.variants.push_back({highPath, 1.0f, 1.0f});
    testSet.actions[SoundAction::Break] = std::move(breakGroup);

    // "Step" action has two variants (will randomly pick)
    SoundGroup stepGroup;
    stepGroup.variants.push_back({lowPath, 0.8f, 1.0f});
    stepGroup.variants.push_back({chordPath, 0.8f, 1.0f});
    testSet.actions[SoundAction::Step] = std::move(stepGroup);

    SoundRegistry::global().registerSoundSet("test:tone", std::move(testSet));
    auto soundSetId = SoundSetId::fromName("test:tone");
    std::cout << "Registered sound set: test:tone\n\n";

    // Create audio engine
    SoundEventQueue eventQueue;
    AudioEngine engine(eventQueue, SoundRegistry::global());

    AudioConfig config;
    config.masterVolume = 1.0f;
    config.effectsVolume = 1.0f;

    if (!engine.initialize(config)) {
        std::cerr << "Failed to initialize audio engine!\n";
        return 1;
    }
    std::cout << "Audio engine initialized.\n\n";

    // Test 1: Play a simple "place" sound
    std::cout << "[Test 1] Playing 'place' sound (440 Hz, 0.5s)...\n";
    eventQueue.push(SoundEvent::blockPlace(soundSetId, BlockPos{0, 0, 0}));
    engine.update(glm::dvec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    // Test 2: Play a "break" sound
    std::cout << "[Test 2] Playing 'break' sound (880 Hz, 0.3s)...\n";
    eventQueue.push(SoundEvent::blockBreak(soundSetId, BlockPos{0, 0, 0}));
    engine.update(glm::dvec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Test 3: Rapid footsteps (random variant selection)
    std::cout << "[Test 3] Playing 4 footstep sounds (random 220/554 Hz)...\n";
    for (int i = 0; i < 4; ++i) {
        eventQueue.push(SoundEvent::footstep(soundSetId, glm::vec3(0)));
        engine.update(glm::dvec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Test 4: 3D spatialization - sound moves from left to right
    std::cout << "[Test 4] 3D spatialization: sound panning left to right...\n";
    for (int i = -5; i <= 5; ++i) {
        SoundEvent event;
        event.soundSet = soundSetId;
        event.action = SoundAction::Place;
        event.category = SoundCategory::Effects;
        event.posX = static_cast<float>(i * 3);  // Move from -15 to +15 blocks on X
        event.posY = 0.0f;
        event.posZ = -5.0f;  // 5 blocks in front
        event.volume = 1.0f;
        event.pitch = 1.0f;
        event.positional = true;

        eventQueue.push(event);
        engine.update(glm::dvec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    std::cout << "\nAll tests complete. Shutting down...\n";

    // Brief pause to let last sound finish
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    engine.shutdown();

    // Cleanup test sounds
    std::filesystem::remove_all(soundDir);
    std::cout << "Cleaned up test sounds.\n";

    return 0;
}

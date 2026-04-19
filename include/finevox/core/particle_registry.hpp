#pragma once

/**
 * @file particle_registry.hpp
 * @brief Global registries for ParticleTypeDef and EmitterPresetDef.
 *
 * Same pattern as LootRegistry / FluidRegistry — singleton, shared_mutex,
 * loaded once during startup via ParticleLoader.
 */

#include "finevox/core/particle_type.hpp"

#include <functional>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>

namespace finevox {

// ============================================================================
// ParticleTypeRegistry
// ============================================================================

class ParticleTypeRegistry {
public:
    static ParticleTypeRegistry& global();

    /// Register a particle type by name. Returns false if already registered.
    bool registerType(std::string_view name, ParticleTypeDef def);

    [[nodiscard]] const ParticleTypeDef* get(ParticleTypeId id) const;
    [[nodiscard]] const ParticleTypeDef* get(std::string_view name) const;
    [[nodiscard]] ParticleTypeId getId(std::string_view name) const;
    [[nodiscard]] bool has(ParticleTypeId id) const;
    [[nodiscard]] bool has(std::string_view name) const;

    [[nodiscard]] size_t size() const;
    void clear();

    void forEach(const std::function<void(ParticleTypeId, const ParticleTypeDef&)>& fn) const;

    /// Patch the atlas UV rect on an already-registered particle type.
    /// Called by the renderer (ParticleAtlas::applyToRegistry) at startup so
    /// the simulator stamps correct UVs onto particles at spawn time.
    /// Returns true if the type was found and updated.
    bool setAtlasUV(ParticleTypeId id, glm::vec4 uv);
    bool setAtlasUV(std::string_view name, glm::vec4 uv);

    ParticleTypeRegistry(const ParticleTypeRegistry&) = delete;
    ParticleTypeRegistry& operator=(const ParticleTypeRegistry&) = delete;

private:
    ParticleTypeRegistry() = default;
    mutable std::shared_mutex mutex_;
    std::unordered_map<ParticleTypeId, ParticleTypeDef> types_;
};

// ============================================================================
// EmitterPresetRegistry
// ============================================================================

class EmitterPresetRegistry {
public:
    static EmitterPresetRegistry& global();

    bool registerPreset(std::string_view name, EmitterPresetDef def);

    [[nodiscard]] const EmitterPresetDef* get(EmitterPresetId id) const;
    [[nodiscard]] const EmitterPresetDef* get(std::string_view name) const;
    [[nodiscard]] EmitterPresetId getId(std::string_view name) const;
    [[nodiscard]] bool has(EmitterPresetId id) const;
    [[nodiscard]] bool has(std::string_view name) const;

    [[nodiscard]] size_t size() const;
    void clear();

    void forEach(const std::function<void(EmitterPresetId, const EmitterPresetDef&)>& fn) const;

    EmitterPresetRegistry(const EmitterPresetRegistry&) = delete;
    EmitterPresetRegistry& operator=(const EmitterPresetRegistry&) = delete;

private:
    EmitterPresetRegistry() = default;
    mutable std::shared_mutex mutex_;
    std::unordered_map<EmitterPresetId, EmitterPresetDef> presets_;
};

// ============================================================================
// Loader — parses .particle / .emitter files via ConfigParser
// ============================================================================

class ParticleLoader {
public:
    // Particle types
    [[nodiscard]] static std::optional<ParticleTypeDef> loadTypeFromFile(const std::string& path);
    [[nodiscard]] static std::optional<ParticleTypeDef> loadTypeFromString(std::string_view content);
    static size_t loadTypesFromDirectory(const std::string& dirPath);

    // Emitter presets
    [[nodiscard]] static std::optional<EmitterPresetDef> loadPresetFromFile(const std::string& path);
    [[nodiscard]] static std::optional<EmitterPresetDef> loadPresetFromString(std::string_view content);
    static size_t loadPresetsFromDirectory(const std::string& dirPath);
};

} // namespace finevox

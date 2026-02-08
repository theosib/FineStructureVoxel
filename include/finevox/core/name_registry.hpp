#pragma once

/**
 * @file name_registry.hpp
 * @brief Per-world stable name↔ID mapping for persistence and networking
 *
 * Design: Phase 13 Inventory & Items
 *
 * Reuses the same pattern as StringInterner (vector<string> + unordered_map)
 * but as a non-singleton, serializable instance. Each world owns one.
 *
 * - ID 0 is reserved (empty/none)
 * - IDs assigned starting from 1, never reused
 * - Thread-safe (shared_mutex)
 * - Serialized to/from DataContainer as array of strings
 *
 * Translation flow:
 *   Runtime: ItemTypeId (from StringInterner::global()) for fast comparison
 *   Disk:    NameRegistry::PersistentId via World::nameRegistry()
 *   Save:    nameRegistry.getOrAssign(itemTypeId.name()) → writes uint32_t
 *   Load:    nameRegistry.getName(persistentId) → ItemTypeId::fromName(name)
 */

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

class DataContainer;

class NameRegistry {
public:
    using PersistentId = uint32_t;
    static constexpr PersistentId EMPTY_ID = 0;

    NameRegistry();
    ~NameRegistry() = default;

    // Move-only (contains mutex)
    NameRegistry(NameRegistry&& other);
    NameRegistry& operator=(NameRegistry&& other);
    NameRegistry(const NameRegistry&) = delete;
    NameRegistry& operator=(const NameRegistry&) = delete;

    /// Get or assign a persistent ID for a name.
    /// Thread-safe. Returns same ID for duplicate names.
    /// New names get the next sequential ID.
    PersistentId getOrAssign(std::string_view name);

    /// Look up name by persistent ID.
    /// Returns empty string_view if ID is unknown.
    [[nodiscard]] std::string_view getName(PersistentId id) const;

    /// Look up persistent ID by name.
    /// Returns nullopt if not assigned.
    [[nodiscard]] std::optional<PersistentId> find(std::string_view name) const;

    /// Number of assigned IDs (including the reserved ID 0)
    [[nodiscard]] size_t size() const;

    /// Save the full name→id mapping to a parent DataContainer under a key.
    /// Serialized as an array of strings indexed by ID.
    void saveTo(DataContainer& dc, std::string_view key) const;

    /// Load a NameRegistry from a parent DataContainer.
    /// Returns a new registry with IDs matching the saved mapping.
    static NameRegistry loadFrom(const DataContainer& dc, std::string_view key);

private:
    mutable std::shared_mutex mutex_;
    std::vector<std::string> names_;                    // Index = PersistentId
    std::unordered_map<std::string, PersistentId> lookup_;  // Reverse lookup
};

}  // namespace finevox

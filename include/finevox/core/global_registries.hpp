#pragma once

/**
 * @file global_registries.hpp
 * @brief One list of the process-wide registries a game's content fills, to empty them all
 *
 * Blocks, items, sounds, biomes and the rest are process-wide singletons that content fills as
 * it loads. Each joins this list when it is first made, with a way to empty itself, so a single
 * call puts the process back as it was before any game loaded, and another game (or the same
 * one again) can load into it.
 *
 * Not on the list, because they don't belong to a game:
 * - StringInterner: ids it has given out live on in statics (symbol tables in the script
 *   bindings), and a name has the same id whatever loads. Interning is append-only.
 * - ConfigManager: the user's settings. ResourceLocator: where the engine, the game and the
 *   user's files are, set by the program for the whole run.
 * - The script bindings' tables of field and event symbols: interned ids only.
 */

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace finevox {

class GlobalRegistries {
public:
    /// Join the list. `name` is the registry's class name, for tests and diagnostics; `size`
    /// counts what content has put in it (0 when cleared). Called once by each registry when it
    /// is made; thread-safe.
    static void join(std::string name, std::function<void()> clear, std::function<size_t()> size);

    /// Empty every registry on the list, in the order they joined.
    ///
    /// Only once nothing of the old game runs or is kept: its sessions' threads stopped, and its
    /// worlds, renderers, script engines and content loaders gone. Block types are read without a
    /// lock and by reference (BlockRegistry::getType), so clearing frees what such a reader still
    /// holds.
    static void clearAll();

    /// The class names of the registries that have joined, in the order they joined.
    [[nodiscard]] static std::vector<std::string> names();

    /// Each registry's name and how much content it holds, in the order they joined.
    [[nodiscard]] static std::vector<std::pair<std::string, size_t>> sizes();
};

}  // namespace finevox

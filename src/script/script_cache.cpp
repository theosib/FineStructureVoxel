#include "finevox/script/script_cache.hpp"
#include <iostream>

namespace finevox::script {

ScriptCache::ScriptCache(finescript::ScriptEngine& engine)
    : engine_(engine) {}

finescript::CompiledScript* ScriptCache::load(const std::string& path) {
    namespace fs = std::filesystem;

    fs::path fsPath(path);
    if (!fs::exists(fsPath)) {
        return nullptr;
    }

    auto mtime = fs::last_write_time(fsPath);
    std::string canonical = fs::canonical(fsPath).string();

    auto it = entries_.find(canonical);
    if (it != entries_.end() && it->second.lastModified == mtime) {
        // Already cached and up-to-date — engine's loadScript returns cached version
        return engine_.loadScript(canonical);
    }

    // New or changed — invalidate engine cache and reload
    if (it != entries_.end()) {
        engine_.invalidateCache(canonical);
    }

    auto* script = engine_.loadScript(canonical);
    if (script) {
        entries_[canonical] = {mtime, canonical};
    }
    return script;
}

size_t ScriptCache::reloadChanged() {
    namespace fs = std::filesystem;
    size_t count = 0;

    for (auto it = entries_.begin(); it != entries_.end(); ) {
        const auto& path = it->second.resolvedPath;

        if (!fs::exists(path)) {
            engine_.invalidateCache(path);
            it = entries_.erase(it);
            ++count;
            continue;
        }

        auto mtime = fs::last_write_time(path);
        if (mtime != it->second.lastModified) {
            engine_.invalidateCache(path);
            auto* script = engine_.loadScript(path);
            if (script) {
                it->second.lastModified = mtime;
                ++count;
            } else {
                it = entries_.erase(it);
                ++count;
                continue;
            }
        }
        ++it;
    }

    return count;
}

void ScriptCache::invalidate(const std::string& path) {
    namespace fs = std::filesystem;

    std::string canonical = path;
    if (fs::exists(path)) {
        canonical = fs::canonical(path).string();
    }

    engine_.invalidateCache(canonical);
    entries_.erase(canonical);
}

}  // namespace finevox::script

#include "finevox/core/global_registries.hpp"

#include <mutex>

namespace finevox {

namespace {

struct Entry {
    std::string name;
    std::function<void()> clear;
    std::function<size_t()> size;
};

struct List {
    std::mutex mutex;
    std::vector<Entry> entries;
};

// Made by the first join, which runs inside the first registry's construction: so it is
// destroyed after every registry, and none outlives it.
List& list() {
    static List instance;
    return instance;
}

}  // namespace

void GlobalRegistries::join(std::string name, std::function<void()> clear,
                            std::function<size_t()> size) {
    List& l = list();
    std::lock_guard lock(l.mutex);
    l.entries.push_back(Entry{std::move(name), std::move(clear), std::move(size)});
}

namespace {

// A copy, so what a registry does when cleared or counted may itself make (and so join) another
std::vector<Entry> snapshot() {
    List& l = list();
    std::lock_guard lock(l.mutex);
    return l.entries;
}

}  // namespace

void GlobalRegistries::clearAll() {
    for (const Entry& entry : snapshot()) entry.clear();
}

std::vector<std::pair<std::string, size_t>> GlobalRegistries::sizes() {
    std::vector<std::pair<std::string, size_t>> out;
    for (const Entry& entry : snapshot()) out.emplace_back(entry.name, entry.size());
    return out;
}

std::vector<std::string> GlobalRegistries::names() {
    List& l = list();
    std::lock_guard lock(l.mutex);
    std::vector<std::string> out;
    out.reserve(l.entries.size());
    for (const Entry& entry : l.entries) out.push_back(entry.name);
    return out;
}

}  // namespace finevox

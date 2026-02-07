/**
 * @file clipboard_manager.cpp
 * @brief Session clipboard for copy/paste operations
 *
 * Design: [21-clipboard-schematic.md] Section 21.7
 */

#include "finevox/worldgen/clipboard_manager.hpp"

#include <algorithm>

namespace finevox::worldgen {

ClipboardManager& ClipboardManager::instance() {
    static ClipboardManager inst;
    return inst;
}

void ClipboardManager::setClipboard(Schematic schematic) {
    std::lock_guard<std::mutex> lock(mutex_);
    clipboard_ = std::make_unique<Schematic>(std::move(schematic));
}

const Schematic* ClipboardManager::clipboard() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clipboard_.get();
}

void ClipboardManager::clearClipboard() {
    std::lock_guard<std::mutex> lock(mutex_);
    clipboard_.reset();
}

void ClipboardManager::setNamed(std::string_view name, Schematic schematic) {
    std::lock_guard<std::mutex> lock(mutex_);
    namedClipboards_.insert_or_assign(std::string(name), std::move(schematic));
}

const Schematic* ClipboardManager::getNamed(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = namedClipboards_.find(std::string(name));
    if (it != namedClipboards_.end()) {
        return &it->second;
    }
    return nullptr;
}

void ClipboardManager::clearNamed(std::string_view name) {
    std::lock_guard<std::mutex> lock(mutex_);
    namedClipboards_.erase(std::string(name));
}

void ClipboardManager::clearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    clipboard_.reset();
    namedClipboards_.clear();
    history_.clear();
}

void ClipboardManager::pushHistory(Schematic schematic) {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.push_front(std::move(schematic));
    while (history_.size() > maxHistorySize_) {
        history_.pop_back();
    }
}

const Schematic* ClipboardManager::historyAt(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < history_.size()) {
        return &history_[index];
    }
    return nullptr;
}

size_t ClipboardManager::historySize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_.size();
}

void ClipboardManager::clearHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.clear();
}

void ClipboardManager::setMaxHistorySize(size_t max) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxHistorySize_ = max;
    while (history_.size() > maxHistorySize_) {
        history_.pop_back();
    }
}

}  // namespace finevox::worldgen

/**
 * @file clipboard_manager.hpp
 * @brief Session clipboard for copy/paste operations
 *
 * Design: [21-clipboard-schematic.md] Section 21.7
 */

#pragma once

#include "finevox/schematic.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace finevox {

/// Session clipboard for copy/paste operations (thread-safe)
class ClipboardManager {
public:
    static ClipboardManager& instance();

    // ---- Primary clipboard ----

    void setClipboard(Schematic schematic);
    [[nodiscard]] const Schematic* clipboard() const;
    void clearClipboard();

    // ---- Named clipboards ----

    void setNamed(std::string_view name, Schematic schematic);
    [[nodiscard]] const Schematic* getNamed(std::string_view name) const;
    void clearNamed(std::string_view name);
    void clearAll();

    // ---- History ----

    void pushHistory(Schematic schematic);
    [[nodiscard]] const Schematic* historyAt(size_t index) const;
    [[nodiscard]] size_t historySize() const;
    void clearHistory();
    void setMaxHistorySize(size_t max);

private:
    ClipboardManager() = default;

    mutable std::mutex mutex_;
    std::unique_ptr<Schematic> clipboard_;
    std::unordered_map<std::string, Schematic> namedClipboards_;
    std::deque<Schematic> history_;
    size_t maxHistorySize_ = 10;
};

}  // namespace finevox

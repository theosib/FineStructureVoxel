#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace finevox {

/// Which factions fight each other. A faction is on its own until it's given a team, and factions
/// on the same team are allies: they don't fight, and each sees what the others see. Faction 0
/// belongs to nobody, and is nobody's enemy or ally.
///
/// A game sets teams as it starts, and they aren't saved: a game puts them back on resuming.
/// Reads take no lock, as every unit's search for a target asks.
class Alliances {
public:
    static constexpr size_t MAX_FACTIONS = 256;

    Alliances() {
        for (auto& team : teams_) team.store(0, std::memory_order_relaxed);
    }

    /// 0 puts a faction back on its own
    void setTeam(uint16_t faction, uint16_t team) {
        if (faction < MAX_FACTIONS) teams_[faction].store(team, std::memory_order_relaxed);
    }

    [[nodiscard]] uint16_t team(uint16_t faction) const {
        return faction < MAX_FACTIONS ? teams_[faction].load(std::memory_order_relaxed) : 0;
    }

    [[nodiscard]] bool allied(uint16_t a, uint16_t b) const {
        if (a == 0 || b == 0) return false;
        if (a == b) return true;
        uint16_t team = this->team(a);
        return team != 0 && team == this->team(b);
    }

    [[nodiscard]] bool hostile(uint16_t a, uint16_t b) const {
        return a != 0 && b != 0 && !allied(a, b);
    }

private:
    std::array<std::atomic<uint16_t>, MAX_FACTIONS> teams_;
};

}  // namespace finevox

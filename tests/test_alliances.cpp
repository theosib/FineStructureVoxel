#include <gtest/gtest.h>
#include "finevox/core/alliances.hpp"

using namespace finevox;

TEST(Alliances, EveryoneStartsOnTheirOwn) {
    Alliances a;
    EXPECT_TRUE(a.allied(3, 3));
    EXPECT_TRUE(a.hostile(3, 4));
    EXPECT_FALSE(a.allied(0, 0)) << "faction 0 is nobody's";
    EXPECT_FALSE(a.hostile(0, 4));
    EXPECT_FALSE(a.hostile(4, 0));
}

// Swept over every pair of a few factions on a few teams, against the rule written out plainly
TEST(Alliances, ATeamIsAllied) {
    Alliances a;
    const uint16_t teams[] = {0, 1, 1, 2, 0, 2, 1, 3};
    for (uint16_t f = 1; f < 8; ++f) a.setTeam(f, teams[f]);
    for (uint16_t x = 0; x < 8; ++x) {
        for (uint16_t y = 0; y < 8; ++y) {
            bool allied = x != 0 && y != 0 && (x == y || (teams[x] != 0 && teams[x] == teams[y]));
            EXPECT_EQ(a.allied(x, y), allied) << x << " " << y;
            EXPECT_EQ(a.hostile(x, y), x != 0 && y != 0 && !allied) << x << " " << y;
            EXPECT_EQ(a.allied(x, y), a.allied(y, x));
        }
    }

    a.setTeam(2, 0);
    EXPECT_TRUE(a.hostile(1, 2)) << "back on its own";
    EXPECT_FALSE(a.hostile(5000, 5000));
    EXPECT_TRUE(a.hostile(5000, 5001)) << "past the table, on its own";
}

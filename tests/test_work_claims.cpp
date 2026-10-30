#include <gtest/gtest.h>
#include "finevox/core/work_claims.hpp"

using namespace finevox;

namespace {
const BlockCoord TREE{4, 70, 9};
}

TEST(WorkClaims, OnlyOneAgentGetsATarget) {
    WorkClaimRegistry claims;
    EXPECT_TRUE(claims.claim(1, TREE, 100));
    EXPECT_FALSE(claims.claim(1, TREE, 101));
    EXPECT_EQ(claims.ownerOf(1, TREE), 100u);
}

TEST(WorkClaims, ClaimingAgainRefreshes) {
    WorkClaimRegistry claims;
    EXPECT_TRUE(claims.claim(1, TREE, 100, 10));
    claims.setTick(8);
    EXPECT_TRUE(claims.claim(1, TREE, 100, 10));
    claims.setTick(15);
    EXPECT_TRUE(claims.isClaimed(1, TREE)) << "refresh must extend the deadline";
}

TEST(WorkClaims, ScopesDoNotInterfere) {
    WorkClaimRegistry claims;
    EXPECT_TRUE(claims.claim(1, TREE, 100));
    EXPECT_TRUE(claims.claim(2, TREE, 200));
}

TEST(WorkClaims, OnlyTheOwnerCanRelease) {
    WorkClaimRegistry claims;
    claims.claim(1, TREE, 100);
    EXPECT_FALSE(claims.release(1, TREE, 101));
    EXPECT_TRUE(claims.isClaimed(1, TREE));
    EXPECT_TRUE(claims.release(1, TREE, 100));
    EXPECT_FALSE(claims.isClaimed(1, TREE));
}

TEST(WorkClaims, ExpiredClaimsCanBeTaken) {
    WorkClaimRegistry claims;
    claims.claim(1, TREE, 100, 5);
    claims.setTick(4);
    EXPECT_FALSE(claims.claim(1, TREE, 101));
    claims.setTick(5);
    EXPECT_FALSE(claims.isClaimed(1, TREE));
    EXPECT_TRUE(claims.claim(1, TREE, 101));
}

TEST(WorkClaims, ReleaseAllFreesADeadAgentsWork) {
    WorkClaimRegistry claims;
    claims.claim(1, TREE, 100);
    claims.claim(1, {5, 70, 9}, 100);
    claims.claim(2, {6, 70, 9}, 100);
    claims.claim(1, {7, 70, 9}, 200);
    EXPECT_EQ(claims.releaseAll(100), 3u);
    EXPECT_EQ(claims.size(), 1u);
}

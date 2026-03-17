#include <gtest/gtest.h>
#include "finevox/core/entity_spatial_index.hpp"

using namespace finevox;

TEST(EntitySpatialIndexTest, InsertAndQueryRadius) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(5.0f, 64.0f, 5.0f));
    index.insert(2, Vec3(8.0f, 64.0f, 5.0f));
    index.insert(3, Vec3(100.0f, 64.0f, 100.0f));

    auto nearby = index.queryRadius(Vec3(5.0f, 64.0f, 5.0f), 10.0f);
    EXPECT_EQ(nearby.size(), 2u);  // entities 1 and 2

    auto all = index.queryRadius(Vec3(50.0f, 64.0f, 50.0f), 200.0f);
    EXPECT_EQ(all.size(), 3u);
}

TEST(EntitySpatialIndexTest, QueryAABB) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(5.0f, 64.0f, 5.0f));
    index.insert(2, Vec3(8.0f, 64.0f, 5.0f));
    index.insert(3, Vec3(100.0f, 64.0f, 100.0f));

    auto result = index.queryAABB(Vec3(0.0f, 60.0f, 0.0f), Vec3(20.0f, 70.0f, 20.0f));
    EXPECT_EQ(result.size(), 2u);

    auto empty = index.queryAABB(Vec3(50.0f, 0.0f, 50.0f), Vec3(60.0f, 10.0f, 60.0f));
    EXPECT_EQ(empty.size(), 0u);
}

TEST(EntitySpatialIndexTest, FindNearest) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(5.0f, 64.0f, 5.0f));
    index.insert(2, Vec3(8.0f, 64.0f, 5.0f));
    index.insert(3, Vec3(20.0f, 64.0f, 20.0f));

    EntityId nearest = index.findNearest(Vec3(6.0f, 64.0f, 5.0f), 50.0f);
    EXPECT_EQ(nearest, 1u);  // entity 1 is closer to (6,64,5)
}

TEST(EntitySpatialIndexTest, FindNearestWithFilter) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(5.0f, 64.0f, 5.0f));
    index.insert(2, Vec3(8.0f, 64.0f, 5.0f));

    // Filter out entity 1
    EntityId nearest = index.findNearest(Vec3(6.0f, 64.0f, 5.0f), 50.0f,
        [](EntityId id) { return id != 1; });
    EXPECT_EQ(nearest, 2u);
}

TEST(EntitySpatialIndexTest, FindNearestOutOfRange) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(100.0f, 64.0f, 100.0f));

    EntityId nearest = index.findNearest(Vec3(0.0f, 64.0f, 0.0f), 10.0f);
    EXPECT_EQ(nearest, INVALID_ENTITY_ID);
}

TEST(EntitySpatialIndexTest, Remove) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(5.0f, 64.0f, 5.0f));
    index.insert(2, Vec3(8.0f, 64.0f, 5.0f));
    EXPECT_EQ(index.size(), 2u);

    index.remove(1);
    EXPECT_EQ(index.size(), 1u);

    auto result = index.queryRadius(Vec3(5.0f, 64.0f, 5.0f), 50.0f);
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], 2u);
}

TEST(EntitySpatialIndexTest, UpdatePosition) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(5.0f, 64.0f, 5.0f));

    // Move entity far away
    index.update(1, Vec3(100.0f, 64.0f, 100.0f));

    // Should no longer be near origin
    auto nearOrigin = index.queryRadius(Vec3(5.0f, 64.0f, 5.0f), 10.0f);
    EXPECT_EQ(nearOrigin.size(), 0u);

    // Should be near new position
    auto nearNew = index.queryRadius(Vec3(100.0f, 64.0f, 100.0f), 10.0f);
    EXPECT_EQ(nearNew.size(), 1u);
}

TEST(EntitySpatialIndexTest, UpdateWithinSameCell) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(5.0f, 64.0f, 5.0f));

    // Small move within same cell
    index.update(1, Vec3(6.0f, 64.0f, 6.0f));

    // Should still be findable
    auto result = index.queryRadius(Vec3(6.0f, 64.0f, 6.0f), 2.0f);
    EXPECT_EQ(result.size(), 1u);
}

TEST(EntitySpatialIndexTest, Clear) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(5.0f, 64.0f, 5.0f));
    index.insert(2, Vec3(8.0f, 64.0f, 5.0f));
    EXPECT_EQ(index.size(), 2u);

    index.clear();
    EXPECT_EQ(index.size(), 0u);

    auto result = index.queryRadius(Vec3(5.0f, 64.0f, 5.0f), 100.0f);
    EXPECT_EQ(result.size(), 0u);
}

TEST(EntitySpatialIndexTest, CrossCellBoundaryQuery) {
    EntitySpatialIndex index;
    // Place entities in different cells (cell size = 16)
    index.insert(1, Vec3(15.0f, 64.0f, 15.0f));  // Cell (0,4,0)
    index.insert(2, Vec3(17.0f, 64.0f, 17.0f));  // Cell (1,4,1)

    // Query that spans the boundary should find both
    auto result = index.queryRadius(Vec3(16.0f, 64.0f, 16.0f), 5.0f);
    EXPECT_EQ(result.size(), 2u);
}

TEST(EntitySpatialIndexTest, ManyEntitiesSameCell) {
    EntitySpatialIndex index;
    for (int i = 0; i < 50; ++i) {
        index.insert(i + 1, Vec3(5.0f + i * 0.1f, 64.0f, 5.0f));
    }
    EXPECT_EQ(index.size(), 50u);

    auto result = index.queryRadius(Vec3(5.0f, 64.0f, 5.0f), 10.0f);
    EXPECT_EQ(result.size(), 50u);
}

TEST(EntitySpatialIndexTest, RemoveNonexistent) {
    EntitySpatialIndex index;
    index.insert(1, Vec3(5.0f, 64.0f, 5.0f));

    // Removing nonexistent entity should not crash
    index.remove(999);
    EXPECT_EQ(index.size(), 1u);
}

TEST(EntitySpatialIndexTest, UpdateNonexistent) {
    EntitySpatialIndex index;

    // Updating nonexistent entity should not crash
    index.update(999, Vec3(10.0f, 64.0f, 10.0f));
    EXPECT_EQ(index.size(), 0u);
}

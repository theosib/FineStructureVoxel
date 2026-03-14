#include <gtest/gtest.h>
#include "finevox/core/entity_state.hpp"
#include "finevox/core/data_container.hpp"

using namespace finevox;

TEST(EntityStateTest, DefaultConstruction) {
    EntityState state;
    EXPECT_EQ(state.id, INVALID_ENTITY_ID);
    EXPECT_EQ(state.entityType, 0);
    EXPECT_EQ(state.position, glm::dvec3(0.0));
    EXPECT_EQ(state.velocity, glm::dvec3(0.0));
    EXPECT_FALSE(state.onGround);
    EXPECT_FLOAT_EQ(state.yaw, 0.0f);
    EXPECT_FLOAT_EQ(state.pitch, 0.0f);
    EXPECT_EQ(state.animationId, 0);
    EXPECT_FLOAT_EQ(state.animationTime, 0.0f);
    EXPECT_EQ(state.inputSequence, 0u);
    EXPECT_EQ(state.extra, nullptr);
}

TEST(EntityStateTest, CopyWithoutExtra) {
    EntityState orig;
    orig.id = 42;
    orig.entityType = 3;
    orig.position = glm::dvec3(1.0, 2.0, 3.0);
    orig.velocity = glm::dvec3(0.5, -1.0, 0.0);
    orig.onGround = true;
    orig.yaw = 1.5f;
    orig.pitch = -0.3f;
    orig.inputSequence = 99;

    EntityState copy(orig);
    EXPECT_EQ(copy.id, 42u);
    EXPECT_EQ(copy.entityType, 3);
    EXPECT_EQ(copy.position, glm::dvec3(1.0, 2.0, 3.0));
    EXPECT_TRUE(copy.onGround);
    EXPECT_FLOAT_EQ(copy.yaw, 1.5f);
    EXPECT_EQ(copy.extra, nullptr);
}

TEST(EntityStateTest, CopyWithExtra) {
    EntityState orig;
    orig.id = 7;
    orig.extra = std::make_unique<DataContainer>();
    orig.extra->set<int64_t>("health", 20);
    orig.extra->set<std::string>("name", "TestEntity");

    EntityState copy(orig);
    EXPECT_NE(copy.extra, nullptr);
    EXPECT_NE(copy.extra.get(), orig.extra.get()); // Deep copy
    EXPECT_EQ(copy.extra->get<int64_t>("health"), 20);
    EXPECT_EQ(copy.extra->get<std::string>("name"), "TestEntity");

    // Modify copy, original unaffected
    copy.extra->set<int64_t>("health", 10);
    EXPECT_EQ(orig.extra->get<int64_t>("health"), 20);
}

TEST(EntityStateTest, CopyAssignment) {
    EntityState orig;
    orig.id = 5;
    orig.extra = std::make_unique<DataContainer>();
    orig.extra->set<double>("speed", 4.5);

    EntityState copy;
    copy = orig;
    EXPECT_EQ(copy.id, 5u);
    EXPECT_NE(copy.extra, nullptr);
    EXPECT_DOUBLE_EQ(copy.extra->get<double>("speed"), 4.5);
}

TEST(EntityStateTest, MoveConstruction) {
    EntityState orig;
    orig.id = 10;
    orig.extra = std::make_unique<DataContainer>();
    orig.extra->set<int64_t>("armor", 5);
    auto* extraPtr = orig.extra.get();

    EntityState moved(std::move(orig));
    EXPECT_EQ(moved.id, 10u);
    EXPECT_EQ(moved.extra.get(), extraPtr); // Pointer transferred, not cloned
}

TEST(EntityStateTest, CBORRoundTrip_Basic) {
    EntityState orig;
    orig.id = 123;
    orig.entityType = 5;
    orig.position = glm::dvec3(100.5, 64.0, -200.25);
    orig.velocity = glm::dvec3(1.0, -2.0, 0.5);
    orig.onGround = true;
    orig.yaw = 3.14f;
    orig.pitch = -1.0f;
    orig.animationId = 2;
    orig.animationTime = 0.75f;
    orig.inputSequence = 42;

    auto bytes = orig.toCBOR();
    EXPECT_GT(bytes.size(), 0u);

    auto decoded = EntityState::fromCBOR(bytes);
    EXPECT_EQ(decoded.id, 123u);
    EXPECT_EQ(decoded.entityType, 5);
    EXPECT_DOUBLE_EQ(decoded.position.x, 100.5);
    EXPECT_DOUBLE_EQ(decoded.position.y, 64.0);
    EXPECT_DOUBLE_EQ(decoded.position.z, -200.25);
    EXPECT_DOUBLE_EQ(decoded.velocity.x, 1.0);
    EXPECT_DOUBLE_EQ(decoded.velocity.y, -2.0);
    EXPECT_DOUBLE_EQ(decoded.velocity.z, 0.5);
    EXPECT_TRUE(decoded.onGround);
    EXPECT_FLOAT_EQ(decoded.yaw, 3.14f);
    EXPECT_FLOAT_EQ(decoded.pitch, -1.0f);
    EXPECT_EQ(decoded.animationId, 2);
    EXPECT_FLOAT_EQ(decoded.animationTime, 0.75f);
    EXPECT_EQ(decoded.inputSequence, 42u);
}

TEST(EntityStateTest, CBORRoundTrip_WithExtra) {
    EntityState orig;
    orig.id = 99;
    orig.position = glm::dvec3(10.0, 20.0, 30.0);
    orig.extra = std::make_unique<DataContainer>();
    orig.extra->set<int64_t>("health", 100);
    orig.extra->set<double>("speed", 4.317);
    orig.extra->set<std::string>("status", "poisoned");

    auto bytes = orig.toCBOR();
    auto decoded = EntityState::fromCBOR(bytes);

    EXPECT_EQ(decoded.id, 99u);
    EXPECT_NE(decoded.extra, nullptr);
    EXPECT_EQ(decoded.extra->get<int64_t>("health"), 100);
    EXPECT_DOUBLE_EQ(decoded.extra->get<double>("speed"), 4.317);
    EXPECT_EQ(decoded.extra->get<std::string>("status"), "poisoned");
}

TEST(EntityStateTest, CBORRoundTrip_NoExtra) {
    EntityState orig;
    orig.id = 1;
    orig.onGround = false;

    auto bytes = orig.toCBOR();
    auto decoded = EntityState::fromCBOR(bytes);

    EXPECT_EQ(decoded.id, 1u);
    EXPECT_FALSE(decoded.onGround);
    EXPECT_EQ(decoded.extra, nullptr);
}

TEST(EntityStateTest, CBORRoundTrip_EmptyBytes) {
    std::vector<uint8_t> empty;
    auto decoded = EntityState::fromCBOR(empty);
    EXPECT_EQ(decoded.id, INVALID_ENTITY_ID);
}

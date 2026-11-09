#include <gtest/gtest.h>
#include "finevox/net/entity_wire.hpp"

using namespace finevox::net;

namespace {

// One move record as revision 0 of protocol 2 lays it out, written field by field rather than with
// the encoder under test. `extra` stands for fields a later revision appends; `withHealth` false
// stands for a sender from before health.
std::vector<uint8_t> moveRecord(uint64_t id, float x, bool withHealth, uint8_t health, size_t extra) {
    std::vector<uint8_t> record;
    ByteWriter w(record);
    w.u64(id);
    w.f32(x);
    w.f32(2.0f);
    w.f32(3.0f);
    w.f32(0.1f);
    w.f32(0.2f);
    w.f32(0.3f);
    w.f32(1.5f);
    w.f32(-0.5f);
    w.u8(4);
    w.u8(1);
    if (withHealth) w.u8(health);
    for (size_t i = 0; i < extra; ++i) w.u8(0xAB);
    return record;
}

std::vector<uint8_t> batchOf(const std::vector<std::vector<uint8_t>>& records) {
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.u64(77);
    w.var(records.size());
    for (const auto& record : records) w.bytes(record);
    return body;
}

}  // namespace

TEST(EntityWire, ABatchRoundTrips) {
    EntityBatchWire batch;
    batch.tick = 12;
    EntityMoveWire move;
    move.id = 9;
    move.position = {1.0f, 2.0f, 3.0f};
    move.velocity = {0.5f, 0.0f, -0.5f};
    move.yaw = 1.0f;
    move.pitch = -0.25f;
    move.animation = 3;
    move.onGround = true;
    move.health = 100;
    batch.entities.push_back(move);

    auto back = decodeEntityBatch(encodeEntityBatch(batch));
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->tick, 12u);
    ASSERT_EQ(back->entities.size(), 1u);
    const auto& got = back->entities[0];
    EXPECT_EQ(got.id, 9u);
    EXPECT_EQ(got.position, move.position);
    EXPECT_EQ(got.velocity, move.velocity);
    EXPECT_EQ(got.yaw, 1.0f);
    EXPECT_EQ(got.pitch, -0.25f);
    EXPECT_EQ(got.animation, 3);
    EXPECT_TRUE(got.onGround);
    EXPECT_EQ(got.health, 100);
}

TEST(EntityWire, FieldsANewerSenderAddsAreSkipped) {
    auto body = batchOf({moveRecord(1, 10.0f, true, 50, 7), moveRecord(2, 20.0f, true, 60, 0)});
    auto batch = decodeEntityBatch(body);
    ASSERT_TRUE(batch.has_value());
    ASSERT_EQ(batch->entities.size(), 2u);
    EXPECT_EQ(batch->entities[0].id, 1u);
    EXPECT_EQ(batch->entities[0].position.x, 10.0f);
    EXPECT_EQ(batch->entities[0].health, 50);
    // The second starts where the first said it ended, not where this reader's fields did
    EXPECT_EQ(batch->entities[1].id, 2u);
    EXPECT_EQ(batch->entities[1].position.x, 20.0f);
    EXPECT_EQ(batch->entities[1].health, 60);
}

TEST(EntityWire, FieldsAnOlderSenderLacksTakeTheirDefault) {
    auto batch = decodeEntityBatch(batchOf({moveRecord(5, 1.0f, false, 0, 0)}));
    ASSERT_TRUE(batch.has_value());
    ASSERT_EQ(batch->entities.size(), 1u);
    EXPECT_EQ(batch->entities[0].id, 5u);
    EXPECT_EQ(batch->entities[0].animation, 4);
    EXPECT_EQ(batch->entities[0].health, FULL_HEALTH);
}

TEST(EntityWire, ARecordShortOfTheCoreIsDroppedAlone) {
    std::vector<uint8_t> stub = {1, 2, 3};
    auto batch = decodeEntityBatch(batchOf({stub, moveRecord(8, 1.0f, true, 9, 0)}));
    ASSERT_TRUE(batch.has_value());
    ASSERT_EQ(batch->entities.size(), 1u);
    EXPECT_EQ(batch->entities[0].id, 8u);
}

TEST(EntityWire, ADamagedBatchIsRefused) {
    auto body = batchOf({moveRecord(1, 1.0f, true, 9, 0)});
    body.resize(body.size() - 4);  // Cut off mid-record
    EXPECT_FALSE(decodeEntityBatch(body).has_value());

    std::vector<uint8_t> lying;
    ByteWriter w(lying);
    w.u64(1);
    w.var(1'000'000);  // Far more records than it could hold
    EXPECT_FALSE(decodeEntityBatch(lying).has_value());
}

TEST(EntityWire, ASpawnFromBeforeHealthIsUnhurt) {
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.u64(3);
    w.var(42);
    w.u16(2);
    w.f64(1.0);
    w.f64(2.0);
    w.f64(3.0);
    w.f32(0.5f);
    w.f32(0.25f);
    auto spawn = decodeEntitySpawn(body);
    ASSERT_TRUE(spawn.has_value());
    EXPECT_EQ(spawn->type, 42u);
    EXPECT_EQ(spawn->faction, 2);
    EXPECT_EQ(spawn->position.z, 3.0);
    EXPECT_EQ(spawn->health, FULL_HEALTH);

    // And one from after it, with more again
    w.u8(30);
    w.u32(0xDEADBEEF);
    spawn = decodeEntitySpawn(body);
    ASSERT_TRUE(spawn.has_value());
    EXPECT_EQ(spawn->health, 30);

    body.resize(10);
    EXPECT_FALSE(decodeEntitySpawn(body).has_value());
}

TEST(EntityWire, ProjectilesSkipWhatTheyDontKnow) {
    std::vector<uint8_t> record;
    ByteWriter rw(record);
    for (float v : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}) rw.f32(v);
    rw.u16(7);  // Say, a faction, in some later revision
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.var(2);
    w.bytes(record);
    w.bytes(record);
    auto shots = decodeProjectiles(body);
    ASSERT_TRUE(shots.has_value());
    ASSERT_EQ(shots->size(), 2u);
    EXPECT_EQ((*shots)[1].position.x, 1.0f);
    EXPECT_EQ((*shots)[1].velocity.z, 6.0f);
}

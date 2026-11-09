#include <gtest/gtest.h>
#include "finevox/core/cbor.hpp"
#include "finevox/net/messages.hpp"
#include "finevox/net/wire.hpp"

#include <cmath>
#include <random>

using namespace finevox;
using namespace finevox::net;

// Messages from other versions are built with core/cbor.hpp, not the writer under test, so that a
// mistake shared by the writer and reader can't hide

namespace {

void key(std::vector<uint8_t>& out, int64_t k) { cbor::encodeInt(out, k); }

// An EntityBatch entry as a sender might write it: `older` leaves out health, as one from before it
// would, and `newer` adds keys this reader has never heard of, of every kind
std::vector<uint8_t> batchFrom(bool older, bool newer) {
    std::vector<uint8_t> out;
    cbor::encodeMapHeader(out, newer ? 3 : 2);
    key(out, 0);
    cbor::encodeInt(out, 77);
    key(out, 1);
    cbor::encodeArrayHeader(out, 2);
    for (int id : {5, 6}) {
        int fields = 5 + (older ? 0 : 1) + (newer ? 4 : 0);
        cbor::encodeMapHeader(out, fields);
        if (newer) {
            key(out, 40);
            cbor::encodeString(out, "a field from later");
            key(out, 41);
            cbor::encodeArrayHeader(out, 2);
            cbor::encodeInt(out, -3);
            cbor::encodeMapHeader(out, 1);
            key(out, 0);
            cbor::encodeBool(out, true);
        }
        key(out, 0);
        cbor::encodeInt(out, id);
        key(out, 1);
        cbor::encodeDouble(out, 1.5);  // Any width of float will do
        key(out, 2);
        cbor::encodeInt(out, 64);      // An integer too
        key(out, 3);
        cbor::encodeDouble(out, -2.25);
        key(out, 9);
        cbor::encodeInt(out, 2);
        if (!older) {
            key(out, 11);
            cbor::encodeInt(out, 100);
        }
        if (newer) {
            cbor::encodeString(out, "text key");  // Not an integer: skipped with its value
            cbor::encodeBytes(out, std::vector<uint8_t>{1, 2, 3});
            key(out, 42);
            cbor::encodeNull(out);
        }
    }
    if (newer) {
        key(out, 99);
        cbor::encodeBytes(out, std::vector<uint8_t>(10, 0xEE));
    }
    return out;
}

}  // namespace

TEST(Wire, EachMessageRoundTrips) {
    EntityBatchWire batch;
    batch.tick = 1234567;
    EntityMoveWire move;
    move.id = 900000;
    move.position = {-91.53f, 62.0f, 10.47f};
    move.velocity = {2.1f, 0.0f, -1.3f};
    move.yaw = 1.2f;
    move.animation = 3;
    move.onGround = true;
    move.health = 40;
    batch.entities = {move, EntityMoveWire{}};
    auto back = decodeEntityBatch(encodeEntityBatch(batch));
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->tick, batch.tick);
    ASSERT_EQ(back->entities.size(), 2u);
    EXPECT_EQ(back->entities[0].id, move.id);
    EXPECT_EQ(back->entities[0].position, move.position);
    EXPECT_EQ(back->entities[0].velocity, move.velocity);
    EXPECT_EQ(back->entities[0].yaw, move.yaw);
    EXPECT_EQ(back->entities[0].pitch, 0.0f);
    EXPECT_EQ(back->entities[0].animation, 3);
    EXPECT_TRUE(back->entities[0].onGround);
    EXPECT_EQ(back->entities[0].health, 40);
    EXPECT_EQ(back->entities[1].health, FULL_HEALTH);
    EXPECT_FALSE(back->entities[1].onGround);

    EntitySpawnWire spawn{12, 7, 3, {1e6 + 0.25, -64.0, 3.5}, 0.5f, -0.25f, 200};
    auto s = decodeEntitySpawn(encodeEntitySpawn(spawn));
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->id, 12u);
    EXPECT_EQ(s->type, 7u);
    EXPECT_EQ(s->faction, 3);
    EXPECT_EQ(s->position, spawn.position);  // Doubles, far from the origin
    EXPECT_EQ(s->pitch, -0.25f);
    EXPECT_EQ(s->health, 200);

    auto gone = decodeEntityGone(encodeEntityGone({44, true}));
    ASSERT_TRUE(gone.has_value());
    EXPECT_EQ(gone->id, 44u);
    EXPECT_TRUE(gone->died);

    auto name = decodeName(encodeName({300, "keystone:timber_stack_3"}));
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(name->id, 300u);
    EXPECT_EQ(name->name, "keystone:timber_stack_3");

    auto time = decodeTime(encodeTime({99999, false, true}));
    ASSERT_TRUE(time.has_value());
    EXPECT_EQ(time->ticks, 99999u);
    EXPECT_FALSE(time->held);
    EXPECT_TRUE(time->paused);

    std::vector<GhostWire> ghosts = {{0.5f, {{-3, 64, 9, 17}, {100000, -5, 0, 2}}}, {1.0f, {}}};
    auto g = decodeGhosts(encodeGhosts(ghosts));
    ASSERT_TRUE(g.has_value());
    ASSERT_EQ(g->size(), 2u);
    EXPECT_EQ((*g)[0].built, 0.5f);
    ASSERT_EQ((*g)[0].cells.size(), 2u);
    EXPECT_EQ((*g)[0].cells[1].x, 100000);
    EXPECT_EQ((*g)[0].cells[1].y, -5);
    EXPECT_EQ((*g)[0].cells[0].block, 17u);
    EXPECT_TRUE((*g)[1].cells.empty());

    VisionWire vision;
    vision.cells.assign(5000, 1);
    vision.cells[17] = 2;
    auto v = decodeVision(encodeVision(vision), 1 << 20);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->cells, vision.cells);
    EXPECT_FALSE(decodeVision(encodeVision(vision), 100).has_value()) << "past the limit";

    auto p = decodePlayerState(encodePlayerState({{1, 2, 3}, {0, -9.5f, 0}, 0.1f, 0.2f, true}));
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->velocity.y, -9.5f);
    EXPECT_TRUE(p->onGround);

    auto change = decodeExpectChange(encodeExpectChange({-1, -70, 5}));
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->y, -70);

    auto hint = decodeViewHint(encodeViewHint({3.5f, -8.0f}));
    ASSERT_TRUE(hint.has_value());
    EXPECT_EQ(hint->z, -8.0f);

    std::vector<uint8_t> data(1000, 9);
    ContentChunkWire chunk;
    chunk.sha256.fill(0xAB);
    chunk.offset = 256 * 1024;
    chunk.total = 1 << 20;
    chunk.data = data;
    auto body = encodeContentChunk(chunk);
    auto c = decodeContentChunk(body);
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->sha256, chunk.sha256);
    EXPECT_EQ(c->offset, chunk.offset);
    EXPECT_EQ(std::vector<uint8_t>(c->data.begin(), c->data.end()), data);

    auto shots = decodeProjectiles(encodeProjectiles({{{1, 2, 3}, {4, 5, 6}}}));
    ASSERT_TRUE(shots.has_value());
    ASSERT_EQ(shots->size(), 1u);
    EXPECT_EQ((*shots)[0].velocity.z, 6.0f);
}

TEST(Wire, ANewerSendersFieldsAreSkipped) {
    auto batch = decodeEntityBatch(batchFrom(false, true));
    ASSERT_TRUE(batch.has_value());
    EXPECT_EQ(batch->tick, 77u);
    ASSERT_EQ(batch->entities.size(), 2u);
    EXPECT_EQ(batch->entities[1].id, 6u);
    EXPECT_EQ(batch->entities[1].position, glm::vec3(1.5f, 64.0f, -2.25f));
    EXPECT_EQ(batch->entities[1].animation, 2);
    EXPECT_EQ(batch->entities[1].health, 100);
}

TEST(Wire, AnOlderSendersMissingFieldsTakeTheirDefault) {
    auto batch = decodeEntityBatch(batchFrom(true, false));
    ASSERT_TRUE(batch.has_value());
    ASSERT_EQ(batch->entities.size(), 2u);
    EXPECT_EQ(batch->entities[0].health, FULL_HEALTH);
    EXPECT_EQ(batch->entities[0].velocity, glm::vec3(0.0f));
    EXPECT_FALSE(batch->entities[0].onGround);
}

TEST(Wire, AnEntryWithoutWhatItNeedsIsDroppedAlone) {
    std::vector<uint8_t> out;
    cbor::encodeMapHeader(out, 2);
    key(out, 0);
    cbor::encodeInt(out, 1);
    key(out, 1);
    cbor::encodeArrayHeader(out, 2);
    cbor::encodeMapHeader(out, 1);  // An id, and nowhere
    key(out, 0);
    cbor::encodeInt(out, 3);
    cbor::encodeMapHeader(out, 4);
    for (int k = 0; k < 4; ++k) {
        key(out, k);
        cbor::encodeInt(out, 8);
    }
    auto batch = decodeEntityBatch(out);
    ASSERT_TRUE(batch.has_value());
    ASSERT_EQ(batch->entities.size(), 1u);
    EXPECT_EQ(batch->entities[0].id, 8u);
}

TEST(Wire, AWrongTypeRefusesTheMessage) {
    std::vector<uint8_t> out;
    cbor::encodeMapHeader(out, 2);
    key(out, 0);
    cbor::encodeString(out, "not a number");
    key(out, 1);
    cbor::encodeString(out, "x");
    EXPECT_FALSE(decodeName(out).has_value());
}

TEST(Wire, EveryTruncationIsRefused) {
    auto body = batchFrom(false, true);
    for (size_t length = 0; length < body.size(); ++length) {
        EXPECT_FALSE(decodeEntityBatch(std::span(body).first(length)).has_value()) << length;
    }
}

TEST(Wire, GarbageNeverCrashes) {
    std::mt19937 random(9);
    for (int round = 0; round < 20000; ++round) {
        std::vector<uint8_t> garbage(random() % 200);
        for (auto& byte : garbage) byte = static_cast<uint8_t>(random());
        (void)decodeEntityBatch(garbage);
        (void)decodeGhosts(garbage);
        (void)decodeEntitySpawn(garbage);
        (void)decodeContentChunk(garbage);
    }
}

TEST(Wire, CountsThatCantBeTrueAreRefused) {
    std::vector<uint8_t> out;
    cbor::encodeMapHeader(out, 2);
    key(out, 0);
    cbor::encodeInt(out, 1);
    key(out, 1);
    cbor::encodeArrayHeader(out, 4'000'000'000ull);  // Far more than there are bytes
    EXPECT_FALSE(decodeEntityBatch(out).has_value());

    std::vector<uint8_t> map;
    cbor::encodeMapHeader(map, 1'000'000);
    EXPECT_FALSE(decodeTime(map).has_value());
}

TEST(Wire, NestingTooDeepIsRefused) {
    std::vector<uint8_t> out;
    cbor::encodeMapHeader(out, 2);
    key(out, 0);
    cbor::encodeInt(out, 1);
    key(out, 50);  // Unknown, so skipped, and skipping has a limit
    for (int i = 0; i < 100; ++i) cbor::encodeArrayHeader(out, 1);
    cbor::encodeInt(out, 0);
    EXPECT_FALSE(decodeTime(out).has_value());
}

TEST(Wire, HalfFloatsAreNumbers) {
    // 0xF9 then 0x3E00: 1.5 as a half
    std::vector<uint8_t> out = {0xF9, 0x3E, 0x00, 0xF9, 0xC4, 0x00};
    CborIn in(out);
    EXPECT_EQ(in.number(), 1.5);
    EXPECT_EQ(in.number(), -4.0);
    EXPECT_TRUE(in.ok());
}

TEST(Wire, AWalkingUnitCostsWhatBinaryDid) {
    EntityBatchWire batch;
    EntityMoveWire walking;
    walking.id = 1234;
    walking.position = {-91.53f, 62.0f, 10.47f};
    walking.velocity = {2.1f, 0.0f, -1.3f};
    walking.yaw = 1.2f;
    walking.animation = 1;
    walking.onGround = true;
    EntityMoveWire standing = walking;
    standing.velocity = glm::vec3(0.0f);
    standing.animation = 0;
    batch.entities = {walking};
    size_t one = encodeEntityBatch(batch).size();
    batch.entities = {walking, standing};
    size_t two = encodeEntityBatch(batch).size();
    // The fixed records they replace were 45 bytes each, length included
    EXPECT_LE(two - one, 45u - 10u) << "standing";
    batch.entities = {walking, walking};
    EXPECT_LE(encodeEntityBatch(batch).size() - one, 45u) << "walking";
}

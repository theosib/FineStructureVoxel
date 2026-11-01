#include <gtest/gtest.h>

#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/entity_state.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/net/client_world.hpp"
#include "finevox/net/content.hpp"
#include "finevox/net/protocol.hpp"
#include "finevox/net/subchunk_codec.hpp"

#include <filesystem>
#include <fstream>
#include <random>

using namespace finevox;
using namespace finevox::net;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

SubchunkData mixedSubchunk(int paletteSize) {
    SubchunkData data;
    data.pos = ChunkPos(-3, 2, 7);
    for (int i = 0; i < paletteSize; ++i) data.palette.push_back(static_cast<uint32_t>(i * 3));
    data.indices.resize(SubchunkData::CELLS);
    std::mt19937 random(11);
    for (auto& index : data.indices) index = static_cast<uint16_t>(random() % paletteSize);
    for (size_t i = 0; i < data.light.size(); ++i) data.light[i] = static_cast<uint8_t>(i % 251);
    return data;
}

void expectSame(const SubchunkData& a, const SubchunkData& b) {
    EXPECT_EQ(a.pos, b.pos);
    EXPECT_EQ(a.palette, b.palette);
    EXPECT_EQ(a.indices, b.indices);
    EXPECT_EQ(a.light, b.light);
    ASSERT_EQ(a.fluids.size(), b.fluids.size());
    for (size_t i = 0; i < a.fluids.size(); ++i) {
        EXPECT_EQ(a.fluids[i].index, b.fluids[i].index);
        EXPECT_EQ(a.fluids[i].id, b.fluids[i].id);
        EXPECT_EQ(a.fluids[i].level, b.fluids[i].level);
    }
}

}  // namespace

TEST(ByteCodecTest, VariableLengthIntegersRoundTrip) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    const uint64_t values[] = {0, 1, 127, 128, 16383, 16384, 0xFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull};
    const int64_t signedValues[] = {0, -1, 1, -64, 64, INT32_MIN, INT32_MAX, INT64_MIN, INT64_MAX};
    for (uint64_t v : values) w.var(v);
    for (int64_t v : signedValues) w.svar(v);

    ByteReader r(out);
    for (uint64_t v : values) EXPECT_EQ(r.var(), v);
    for (int64_t v : signedValues) EXPECT_EQ(r.svar(), v);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.remaining(), 0u);
}

TEST(ByteCodecTest, ReadingPastTheEndFailsAndStaysFailed) {
    std::vector<uint8_t> bytes = {1, 2};
    ByteReader r(bytes);
    EXPECT_EQ(r.u32(), 0u);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.u8(), 0u) << "there were bytes, but a broken message stays broken";
}

TEST(ByteCodecTest, ALengthPastTheEndDoesntRead) {
    std::vector<uint8_t> out;
    ByteWriter w(out);
    w.var(1000);
    out.push_back('x');
    ByteReader r(out);
    EXPECT_TRUE(r.str().empty());
    EXPECT_FALSE(r.ok());
}

TEST(ByteCodecTest, AVarintThatNeverEndsFails) {
    std::vector<uint8_t> bytes(20, 0xFF);
    ByteReader r(bytes);
    r.var();
    EXPECT_FALSE(r.ok());
}

TEST(SubchunkCodecTest, RoundTripsEveryPaletteWidth) {
    for (int paletteSize : {2, 3, 16, 256, 257, 4096}) {
        for (bool compress : {false, true}) {
            SubchunkData data = mixedSubchunk(paletteSize);
            std::vector<uint8_t> body;
            encodeSubchunk(data, body, compress);

            SubchunkData back;
            ASSERT_TRUE(decodeSubchunk(body, back)) << paletteSize;
            expectSame(data, back);
        }
    }
}

TEST(SubchunkCodecTest, AUniformSubchunkIsTiny) {
    SubchunkData data;
    data.pos = ChunkPos(1, 2, 3);
    data.palette = {0};
    data.light.fill(0xF0);

    std::vector<uint8_t> body;
    encodeSubchunk(data, body, false);
    EXPECT_LT(body.size(), 12u) << "open sky is most of a world";

    SubchunkData back;
    ASSERT_TRUE(decodeSubchunk(body, back));
    expectSame(data, back);
}

TEST(SubchunkCodecTest, FluidsRoundTrip) {
    SubchunkData data = mixedSubchunk(4);
    data.fluids = {{0, 5, 15}, {4095, 7, 1}, {123, 300, 8}};
    for (bool compress : {false, true}) {
        std::vector<uint8_t> body;
        encodeSubchunk(data, body, compress);
        SubchunkData back;
        ASSERT_TRUE(decodeSubchunk(body, back));
        expectSame(data, back);
    }
}

TEST(SubchunkCodecTest, CompressionPaysOnRealisticData) {
    SubchunkData data;
    data.palette = {0, 5};
    data.indices.resize(SubchunkData::CELLS);
    for (int i = 0; i < 2048; ++i) data.indices[i] = 1;
    data.light.fill(0);
    std::fill(data.light.begin() + 2048, data.light.end(), uint8_t(0xF0));

    std::vector<uint8_t> raw, compressed;
    encodeSubchunk(data, raw, false);
    encodeSubchunk(data, compressed, true);
    EXPECT_LT(compressed.size() * 10, raw.size());
}

TEST(SubchunkCodecTest, EveryTruncationIsRefusedWithoutCrashing) {
    SubchunkData data = mixedSubchunk(4);
    for (bool compress : {false, true}) {
        std::vector<uint8_t> body;
        encodeSubchunk(data, body, compress);
        for (size_t length = 0; length < body.size(); length += 7) {
            SubchunkData back;
            EXPECT_FALSE(decodeSubchunk(std::span(body).first(length), back)) << length;
        }
    }
}

TEST(SubchunkCodecTest, RandomGarbageIsRefusedWithoutCrashing) {
    std::mt19937 random(5);
    for (int round = 0; round < 2000; ++round) {
        std::vector<uint8_t> garbage(random() % 6000);
        for (auto& byte : garbage) byte = static_cast<uint8_t>(random());
        SubchunkData back;
        (void)decodeSubchunk(garbage, back);
    }
}

TEST(SubchunkCodecTest, AnIndexPastThePaletteIsRefused) {
    SubchunkData data = mixedSubchunk(2);
    std::vector<uint8_t> body;
    encodeSubchunk(data, body, false);

    // Position, flags, palette count, palette, width: then the first index
    constexpr size_t FIRST_INDEX = 3 + 1 + 1 + 2 + 1;
    body[FIRST_INDEX] = 200;
    SubchunkData back;
    EXPECT_FALSE(decodeSubchunk(body, back));
}

TEST(SubchunkCodecTest, ACompressionBombIsRefused) {
    std::vector<uint8_t> raw(1000), body;
    encodeCompressed(raw, body);
    std::vector<uint8_t> out;
    EXPECT_FALSE(decodeCompressed(body, out, 100));
}

TEST(EntityStateTest, CopyingKeepsEverything) {
    EntityState state;
    state.id = 9;
    state.faction = 3;
    state.position = glm::dvec3(1.0, 2.0, 3.0);

    EntityState constructed(state);
    EXPECT_EQ(constructed.faction, 3) << "a body that never moves is only ever copied";

    EntityState assigned;
    assigned = state;
    EXPECT_EQ(assigned.faction, 3);
}

class ClientWorldTest : public ::testing::Test {
protected:
    void SetUp() override {
        stone = BlockTypeId::fromName("net_stone");
        marker = BlockTypeId::fromName("net_marker");
        BlockType type;
        type.setCollisionShape(CollisionShape::FULL_BLOCK);
        BlockRegistry::global().registerOrUpdateType(stone, type);
        BlockRegistry::global().registerOrUpdateType(marker, type);
        world.nameBlock(1, "net_stone");
        world.nameBlock(2, "net_marker");
    }

    /// All stone at subchunk (0,0,0), but for one cell
    SubchunkData stoneWith(BlockCoord cell, uint32_t id) {
        SubchunkData data;
        data.pos = ChunkPos(0, 0, 0);
        data.palette = {1, id};
        data.indices.assign(SubchunkData::CELLS, 0);
        data.indices[cell.x + cell.z * 16 + cell.y * 256] = 1;
        data.light.fill(0xF0);
        return data;
    }

    ClientWorld world;
    BlockTypeId stone, marker;
    BlockCoord spot{3, 4, 5};
};

TEST_F(ClientWorldTest, ASubchunkArrivesWhole) {
    world.apply(stoneWith(spot, 2));
    EXPECT_EQ(world.world().getBlock(spot), marker);
    EXPECT_EQ(world.world().getBlock(BlockCoord(0, 0, 0)), stone);
}

TEST_F(ClientWorldTest, IndexOrderIsTheEnginesOwn) {
    SubchunkData data = stoneWith(BlockCoord(1, 2, 3), 2);
    world.apply(data);
    EXPECT_EQ(world.world().getBlock(BlockCoord(1, 2, 3)), marker);
    EXPECT_EQ(world.world().getBlock(BlockCoord(3, 2, 1)), stone);
    EXPECT_EQ(LocalBlockCoord(1, 2, 3).toIndex(), 1 + 3 * 16 + 2 * 256) << "the spec's x + z*16 + y*256";
}

TEST_F(ClientWorldTest, LightComesWithIt) {
    SubchunkData data = stoneWith(spot, 2);
    data.light.fill(0x73);
    world.apply(data);
    EXPECT_EQ(world.packedLight(BlockCoord(0, 0, 0)), 0x73);
}

TEST_F(ClientWorldTest, WhatIsntKnownIsOpenSky) {
    EXPECT_EQ(world.packedLight(BlockCoord(1000, 0, 1000)), 0xF0);
}

TEST_F(ClientWorldTest, ALaterSubchunkReplacesAnEarlierOne) {
    world.apply(stoneWith(spot, 2));
    world.apply(stoneWith(BlockCoord(9, 9, 9), 2));
    EXPECT_EQ(world.world().getBlock(spot), stone);
    EXPECT_EQ(world.world().getBlock(BlockCoord(9, 9, 9)), marker);
}

TEST_F(ClientWorldTest, FluidGoesWhenTheServerSaysItsGone) {
    auto water = FluidTypeId::fromName("net_water");
    world.nameFluid(5, "net_water");

    SubchunkData wet = stoneWith(spot, 2);
    wet.fluids = {{10, 5, 8}};
    world.apply(wet);
    const SubChunk* subchunk = world.world().getSubChunk(ChunkPos(0, 0, 0));
    ASSERT_NE(subchunk, nullptr);
    EXPECT_EQ(subchunk->getFluid(10), water);

    world.apply(stoneWith(spot, 2));
    EXPECT_TRUE(subchunk->getFluid(10).isEmpty());
}

TEST_F(ClientWorldTest, APredictionShowsAtOnceAndStaysIfTheServerAgrees) {
    world.apply(stoneWith(BlockCoord(15, 15, 15), 2));
    world.predict(spot, marker, 100ms);
    EXPECT_EQ(world.world().getBlock(spot), marker);

    world.apply(stoneWith(spot, 2));
    world.settle(ClientWorld::Clock::now() + 200ms);
    EXPECT_EQ(world.world().getBlock(spot), marker);
}

TEST_F(ClientWorldTest, APredictionTheServerNeverAgreedToGoesBack) {
    world.apply(stoneWith(BlockCoord(15, 15, 15), 2));
    world.predict(spot, marker, 100ms);

    world.settle(ClientWorld::Clock::now());
    EXPECT_EQ(world.world().getBlock(spot), marker) << "it hasn't had its time";

    world.settle(ClientWorld::Clock::now() + 200ms);
    EXPECT_EQ(world.world().getBlock(spot), stone);
}

TEST_F(ClientWorldTest, AnotherChangeNearbyDoesntUndoAPrediction) {
    world.apply(stoneWith(BlockCoord(15, 15, 15), 2));
    world.predict(spot, marker, 100ms);

    // A worker next door changes this subchunk before the server's seen the command
    world.apply(stoneWith(BlockCoord(14, 15, 15), 2));
    EXPECT_EQ(world.world().getBlock(spot), marker);
    EXPECT_EQ(world.world().getBlock(BlockCoord(14, 15, 15)), marker);
}

TEST_F(ClientWorldTest, TheServersLastWordWinsWhenAPredictionEnds) {
    world.apply(stoneWith(BlockCoord(15, 15, 15), 2));
    world.predict(spot, marker, 100ms);

    SubchunkData air = stoneWith(BlockCoord(15, 15, 15), 2);
    air.palette = {1, 2, 0};
    air.indices[spot.x + spot.z * 16 + spot.y * 256] = 2;
    world.apply(air);
    EXPECT_EQ(world.world().getBlock(spot), marker);

    world.settle(ClientWorld::Clock::now() + 200ms);
    EXPECT_TRUE(world.world().getBlock(spot).isAir());
}

TEST(ContentTest, Sha256MatchesKnownVectors) {
    EXPECT_EQ(toHex(sha256({})), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::string abc = "abc";
    EXPECT_EQ(toHex(sha256(std::span(reinterpret_cast<const uint8_t*>(abc.data()), abc.size()))),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string longer(1000000, 'a');
    EXPECT_EQ(toHex(sha256(std::span(reinterpret_cast<const uint8_t*>(longer.data()), longer.size()))),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(ContentTest, HexRoundTrips) {
    Sha256 hash = sha256({});
    EXPECT_EQ(fromHex(toHex(hash)), hash);
    EXPECT_FALSE(fromHex("abc").has_value());
    EXPECT_FALSE(fromHex(std::string(64, 'z')).has_value());
}

TEST(ContentTest, PathsThatLeaveTheRootAreRefused) {
    for (const char* path : {"", "/etc/passwd", "../up", "a/../../up", "a//b", "a/./b", ".", "..", "a/", "a\\b",
                             "..\\up"}) {
        EXPECT_FALSE(isSafeContentPath(path)) << path;
    }
    for (const char* path : {"game.conf", "ui/main.fs", "textures/blocks/oak.png", "a.b/c..d"}) {
        EXPECT_TRUE(isSafeContentPath(path)) << path;
    }
}

class ContentSyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        base = fs::temp_directory_path() / ("finevox_content_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        server = base / "server";
        client = base / "client";
        cache = base / "cache";
        write(server / "game.conf", "name: Test\n");
        write(server / "ui/main.fs", "print 1\n");
        write(server / "scripts/secret.fsc", "the rules\n");
        write(client / "game.conf", "name: Test\n");
        write(client / "ui/main.fs", "print 1\n");
    }

    void TearDown() override { fs::remove_all(base); }

    void write(const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }

    std::string read(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    /// As a server would, a file at a time
    bool deliver(ContentSync& sync, const std::vector<ContentFile>& files, size_t chunk = 4) {
        for (const Sha256& hash : sync.missing()) {
            for (const ContentFile& file : files) {
                if (file.hash != hash) continue;
                std::string data = read(server / file.path);
                for (size_t offset = 0; offset < data.size(); offset += chunk) {
                    size_t take = std::min(chunk, data.size() - offset);
                    auto piece = std::span(reinterpret_cast<const uint8_t*>(data.data() + offset), take);
                    if (!sync.receive(hash, offset, data.size(), piece)) return false;
                }
                break;
            }
        }
        return true;
    }

    fs::path base, server, client, cache;
};

TEST_F(ContentSyncTest, TheManifestLeavesOutWhatsNotListed) {
    auto files = buildManifest(server, {"game.conf", "ui"});
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].path, "game.conf");
    EXPECT_EQ(files[1].path, "ui/main.fs");
}

TEST_F(ContentSyncTest, TheSameGameNeedsNothing) {
    auto files = buildManifest(server, {"game.conf", "ui"});
    ContentSync sync(client, cache);
    ASSERT_TRUE(sync.begin(files));
    EXPECT_TRUE(sync.done());
    EXPECT_EQ(sync.finish(), client) << "runs from its own game";
    EXPECT_FALSE(fs::exists(cache)) << "and touches nothing";
}

TEST_F(ContentSyncTest, ADifferentFileIsFetchedAndTheRestReused) {
    write(server / "ui/main.fs", "print 2\n");
    auto files = buildManifest(server, {"game.conf", "ui"});

    ContentSync sync(client, cache);
    ASSERT_TRUE(sync.begin(files));
    ASSERT_EQ(sync.missing().size(), 1u);
    ASSERT_TRUE(deliver(sync, files));
    ASSERT_TRUE(sync.done());

    fs::path root = sync.finish();
    EXPECT_NE(root, client);
    EXPECT_EQ(read(root / "ui/main.fs"), "print 2\n");
    EXPECT_EQ(read(root / "game.conf"), "name: Test\n");
    EXPECT_EQ(read(client / "ui/main.fs"), "print 1\n") << "the client's own copy is left alone";
}

TEST_F(ContentSyncTest, WhatsCachedIsntFetchedAgain) {
    write(server / "ui/main.fs", "print 2\n");
    auto files = buildManifest(server, {"game.conf", "ui"});
    {
        ContentSync first(client, cache);
        ASSERT_TRUE(first.begin(files));
        ASSERT_TRUE(deliver(first, files));
        (void)first.finish();
    }
    ContentSync second(client, cache);
    ASSERT_TRUE(second.begin(files));
    EXPECT_TRUE(second.done());
    EXPECT_EQ(read(second.finish() / "ui/main.fs"), "print 2\n");
}

TEST_F(ContentSyncTest, AGenericClientFetchesEverything) {
    auto files = buildManifest(server, {"game.conf", "ui"});
    ContentSync sync("", cache);
    ASSERT_TRUE(sync.begin(files));
    EXPECT_EQ(sync.missing().size(), 2u);
    ASSERT_TRUE(deliver(sync, files));
    EXPECT_EQ(read(sync.finish() / "game.conf"), "name: Test\n");
}

TEST_F(ContentSyncTest, AFileThatDoesntMatchItsHashIsRefused) {
    write(server / "ui/main.fs", "print 2\n");
    auto files = buildManifest(server, {"game.conf", "ui"});
    ContentSync sync(client, cache);
    ASSERT_TRUE(sync.begin(files));

    std::string forged = "print 3\n";
    auto piece = std::span(reinterpret_cast<const uint8_t*>(forged.data()), forged.size());
    EXPECT_FALSE(sync.receive(sync.missing()[0], 0, forged.size(), piece));
    EXPECT_FALSE(sync.done());
    EXPECT_FALSE(fs::exists(cache / "content" / toHex(sync.missing()[0])));
}

TEST_F(ContentSyncTest, AHashNotOnTheListIsRefused) {
    auto files = buildManifest(server, {"game.conf"});
    write(server / "ui/main.fs", "print 2\n");
    files = buildManifest(server, {"ui"});
    ContentSync sync(client, cache);
    ASSERT_TRUE(sync.begin(files));
    uint8_t byte = 0;
    EXPECT_FALSE(sync.receive(sha256({}), 0, 1, std::span(&byte, 1)));
}

TEST_F(ContentSyncTest, PiecesOutOfOrderAreRefused) {
    write(server / "ui/main.fs", "print 2\n");
    auto files = buildManifest(server, {"ui"});
    ContentSync sync(client, cache);
    ASSERT_TRUE(sync.begin(files));
    uint8_t byte = 'x';
    EXPECT_FALSE(sync.receive(sync.missing()[0], 4, 8, std::span(&byte, 1)));
}

TEST_F(ContentSyncTest, AManifestReachingOutsideTheRootIsRefused) {
    ContentFile evil;
    evil.path = "../../../.bashrc";
    ContentSync sync(client, cache);
    EXPECT_FALSE(sync.begin({evil}));
}

TEST_F(ContentSyncTest, ADamagedCacheEntryIsNotBelieved) {
    write(server / "ui/main.fs", "print 2\n");
    auto files = buildManifest(server, {"ui"});
    {
        ContentSync first(client, cache);
        ASSERT_TRUE(first.begin(files));
        ASSERT_TRUE(deliver(first, files));
    }
    write(cache / "content" / toHex(files[0].hash), "print 9\n");

    ContentSync second(client, cache);
    ASSERT_TRUE(second.begin(files));
    EXPECT_FALSE(second.done());
}

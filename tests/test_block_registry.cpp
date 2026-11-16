// The block registry: types read without a lock, from many threads, while one is written

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "finevox/core/block_type.hpp"

using namespace finevox;

TEST(BlockRegistryReads, ATypeReplacedLeavesTheOldOneGood) {
    auto id = BlockTypeId::fromName("registry_replaced");
    BlockType first;
    first.setHardness(1.5f);
    BlockRegistry::global().registerOrUpdateType(id, first);
    const BlockType& held = BlockRegistry::global().getType(id);

    BlockType second;
    second.setHardness(7.0f);
    BlockRegistry::global().registerOrUpdateType(id, second);

    EXPECT_FLOAT_EQ(held.hardness(), 1.5f) << "a reader's reference still reads what it read";
    EXPECT_FLOAT_EQ(BlockRegistry::global().getType(id).hardness(), 7.0f) << "and a new read sees the new";
}

TEST(BlockRegistryReads, UnregisteredIsTheDefault) {
    auto id = BlockTypeId::fromName("registry_never_registered");
    EXPECT_FALSE(BlockRegistry::global().hasType(id));
    EXPECT_EQ(&BlockRegistry::global().getType(id), &BlockRegistry::defaultType());
}

TEST(BlockRegistryReads, RegisterTypeDoesntOverwrite) {
    auto id = BlockTypeId::fromName("registry_once");
    size_t before = BlockRegistry::global().size();
    BlockType a;
    a.setHardness(2.0f);
    EXPECT_TRUE(BlockRegistry::global().registerType(id, a));
    BlockType b;
    b.setHardness(3.0f);
    EXPECT_FALSE(BlockRegistry::global().registerType(id, b));
    EXPECT_FLOAT_EQ(BlockRegistry::global().getType(id).hardness(), 2.0f);
    EXPECT_EQ(BlockRegistry::global().size(), before + 1);
}

// Readers on several threads while a writer keeps replacing and adding types: every read is a
// whole version (one the writer published), never a torn or missing one. Run it under TSan too.
TEST(BlockRegistryReads, ManyReadersOneWriter) {
    auto id = BlockTypeId::fromName("registry_contended");
    BlockType start;
    start.setHardness(1.0f);
    BlockRegistry::global().registerOrUpdateType(id, start);

    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 6; ++t) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                float h = BlockRegistry::global().getType(id).hardness();
                // The writer only ever publishes whole numbers from 1 to 200
                if (h < 1.0f || h > 200.0f || h != static_cast<float>(static_cast<int>(h))) bad.fetch_add(1);
            }
        });
    }
    for (int v = 2; v <= 200; ++v) {
        BlockType next;
        next.setHardness(static_cast<float>(v));
        BlockRegistry::global().registerOrUpdateType(id, next);
        BlockRegistry::global().registerOrUpdateType(
            BlockTypeId::fromName("registry_added_" + std::to_string(v)), BlockType());
    }
    stop = true;
    for (auto& r : readers) r.join();
    EXPECT_EQ(bad.load(), 0);
    EXPECT_FLOAT_EQ(BlockRegistry::global().getType(id).hardness(), 200.0f);
}

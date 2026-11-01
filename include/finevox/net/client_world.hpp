#pragma once

/**
 * @file client_world.hpp
 * @brief The world as a client knows it: what the server has sent, and nothing else
 *
 * A subchunk it was never sent isn't here to be drawn, found or cheated at. Writes come from the
 * session's sync thread, and from the render thread for a prediction. Mesh workers and the render
 * thread read it, which world storage allows without a lock.
 */

#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/world.hpp"
#include "finevox/net/subchunk_codec.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox::net {

class ClientWorld {
public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] World& world() { return world_; }
    [[nodiscard]] const World& world() const { return world_; }

    void nameBlock(uint32_t wireId, std::string_view name);
    void nameFluid(uint32_t wireId, std::string_view name);

    /// Take a subchunk from the server, whole. Cells under prediction keep their predicted block
    /// and the server's is held until the prediction ends.
    void apply(const SubchunkData& data);

    /// Show the block the player's own action should leave here, now. If the server hasn't said
    /// otherwise by the end of `hold`, its word stands, which may mean going back.
    void predict(BlockCoord pos, BlockTypeId block, std::chrono::milliseconds hold);

    /// End predictions that have had their time. Call now and then.
    void settle(Clock::time_point now = Clock::now());

    /// Sky << 4 | block. A subchunk nobody's seen counts as open sky: the edge of what's known
    /// shouldn't draw black.
    [[nodiscard]] uint8_t packedLight(BlockCoord pos) const;

    [[nodiscard]] size_t subchunksReceived() const { return subchunksReceived_; }

private:
    struct Prediction {
        BlockTypeId before;     ///< What's there if the server never says anything
        std::optional<BlockTypeId> serverSays;
        Clock::time_point until;
    };

    void remesh(ChunkPos chunk, bool neighbours);

    World world_;

    // Sync thread only
    std::unordered_map<uint32_t, BlockTypeId> blockNames_;
    std::unordered_map<uint32_t, FluidTypeId> fluidNames_;
    size_t subchunksReceived_ = 0;

    std::mutex predictionMutex_;
    std::unordered_map<BlockCoord, Prediction> predictions_;
};

}  // namespace finevox::net

#include "finevox/net/subchunk_codec.hpp"

#include "finevox/net/wire.hpp"

#include <lz4.h>

#include <algorithm>
#include <optional>

namespace finevox::net {

namespace {

// The message: where it is, and what's there, LZ4-compressed when `raw_size` says how big it was
enum : uint32_t { K_X = 0, K_Y = 1, K_Z = 2, K_CELLS = 3, K_RAW_SIZE = 4 };
// What's there
enum : uint32_t {
    K_PALETTE = 0,
    K_INDEX_WIDTH = 1,
    K_INDICES = 2,
    K_LIGHT = 3,
    K_LIGHT_ALL = 4,
    K_FLUIDS = 5,
    K_ROTATIONS = 6,
};

/// A palette can't usefully be longer than there are cells
constexpr uint64_t MAX_PALETTE = SubchunkData::CELLS;
/// What a subchunk's contents can come to undone: indices, light, rotations, fluids, and slack
constexpr size_t MAX_RAW = SubchunkData::CELLS * 16;

std::vector<uint8_t> writeCells(const SubchunkData& data) {
    MapWriter cells;
    cells.array(K_PALETTE, data.palette.size(), [&](CborOut& out) {
        for (uint32_t id : data.palette) out.uint(id);
    });

    if (data.palette.size() > 1) {
        uint8_t width = data.palette.size() > 256 ? 2 : 1;
        std::vector<uint8_t> indices;
        indices.reserve(SubchunkData::CELLS * width);
        for (uint16_t index : data.indices) {
            indices.push_back(static_cast<uint8_t>(index));
            if (width == 2) indices.push_back(static_cast<uint8_t>(index >> 8));
        }
        if (width != 1) cells.uint(K_INDEX_WIDTH, width);
        cells.bytes(K_INDICES, indices);
    }

    bool uniform = std::all_of(data.light.begin(), data.light.end(),
                               [&](uint8_t v) { return v == data.light[0]; });
    if (uniform) {
        cells.uint(K_LIGHT_ALL, data.light[0]);
    } else {
        cells.bytes(K_LIGHT, data.light);
    }

    if (!data.fluids.empty()) {
        cells.array(K_FLUIDS, data.fluids.size() * 3, [&](CborOut& out) {
            for (const auto& fluid : data.fluids) {
                out.uint(fluid.index);
                out.uint(fluid.id);
                out.uint(fluid.level);
            }
        });
    }
    return cells.finish();
}

bool readCells(std::span<const uint8_t> body, SubchunkData& data) {
    CborIn in(body);
    uint64_t width = 1;
    std::span<const uint8_t> indices;
    bool haveLight = false;
    data.palette.clear();
    data.indices.clear();
    data.fluids.clear();

    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case K_PALETTE: {
                uint64_t count = in.array();
                if (count > MAX_PALETTE) return false;
                data.palette.resize(count);
                for (auto& id : data.palette) id = static_cast<uint32_t>(in.uint());
                return true;
            }
            case K_INDEX_WIDTH: width = in.uint(); return true;
            case K_INDICES: indices = in.bytes(); return true;
            case K_LIGHT: {
                auto light = in.bytes();
                if (light.size() != SubchunkData::CELLS) return true;
                std::copy(light.begin(), light.end(), data.light.begin());
                haveLight = true;
                return true;
            }
            case K_LIGHT_ALL:
                data.light.fill(static_cast<uint8_t>(in.uint()));
                haveLight = true;
                return true;
            case K_FLUIDS: {
                uint64_t count = in.array();
                if (count % 3 != 0 || count / 3 > SubchunkData::CELLS) return false;
                data.fluids.resize(count / 3);
                for (auto& fluid : data.fluids) {
                    fluid.index = static_cast<uint16_t>(in.uint());
                    fluid.id = static_cast<uint32_t>(in.uint());
                    fluid.level = static_cast<uint8_t>(in.uint());
                }
                return true;
            }
            default: return false;  // K_ROTATIONS too: nothing uses them yet
        }
    });
    if (!read || data.palette.empty() || !haveLight) return false;
    for (const auto& fluid : data.fluids) {
        if (fluid.index >= SubchunkData::CELLS) return false;
    }
    if (data.palette.size() == 1) return true;

    if ((width != 1 && width != 2) || indices.size() != SubchunkData::CELLS * width) return false;
    data.indices.resize(SubchunkData::CELLS);
    for (size_t i = 0; i < SubchunkData::CELLS; ++i) {
        uint16_t index = width == 2 ? static_cast<uint16_t>(indices[i * 2] | (indices[i * 2 + 1] << 8)) : indices[i];
        if (index >= data.palette.size()) return false;
        data.indices[i] = index;
    }
    return true;
}

}  // namespace

std::vector<uint8_t> encodeSubchunk(const SubchunkData& data, bool compress) {
    MapWriter message;
    message.sint(K_X, data.pos.x);
    message.sint(K_Y, data.pos.y);
    message.sint(K_Z, data.pos.z);
    std::vector<uint8_t> cells = writeCells(data);
    if (compress) {
        message.bytes(K_CELLS, lz4Compress(cells));
        message.uint(K_RAW_SIZE, cells.size());
    } else {
        message.bytes(K_CELLS, cells);
    }
    return message.finish();
}

bool decodeSubchunk(std::span<const uint8_t> body, SubchunkData& data) {
    CborIn in(body);
    bool haveX = false, haveY = false, haveZ = false;
    std::span<const uint8_t> cells;
    std::optional<uint64_t> rawSize;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case K_X: data.pos.x = static_cast<int32_t>(in.sint()); haveX = true; return true;
            case K_Y: data.pos.y = static_cast<int32_t>(in.sint()); haveY = true; return true;
            case K_Z: data.pos.z = static_cast<int32_t>(in.sint()); haveZ = true; return true;
            case K_CELLS: cells = in.bytes(); return true;
            case K_RAW_SIZE: rawSize = in.uint(); return true;
            default: return false;
        }
    });
    if (!read || !haveX || !haveY || !haveZ || cells.empty()) return false;
    if (!rawSize) return readCells(cells, data);

    std::vector<uint8_t> raw;
    if (!lz4Decompress(cells, *rawSize, MAX_RAW, raw)) return false;
    return readCells(raw, data);
}

std::vector<uint8_t> lz4Compress(std::span<const uint8_t> raw) {
    int bound = LZ4_compressBound(static_cast<int>(raw.size()));
    std::vector<uint8_t> out(static_cast<size_t>(bound));
    int written = LZ4_compress_default(reinterpret_cast<const char*>(raw.data()), reinterpret_cast<char*>(out.data()),
                                       static_cast<int>(raw.size()), bound);
    out.resize(static_cast<size_t>(std::max(written, 0)));
    return out;
}

bool lz4Decompress(std::span<const uint8_t> block, uint64_t rawSize, size_t limit, std::vector<uint8_t>& raw) {
    if (rawSize > limit) return false;
    raw.resize(rawSize);
    int got = LZ4_decompress_safe(reinterpret_cast<const char*>(block.data()), reinterpret_cast<char*>(raw.data()),
                                  static_cast<int>(block.size()), static_cast<int>(rawSize));
    return got == static_cast<int>(rawSize);
}

}  // namespace finevox::net

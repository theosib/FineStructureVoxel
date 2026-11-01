#include "finevox/net/subchunk_codec.hpp"

#include "finevox/net/protocol.hpp"

#include <lz4.h>

#include <algorithm>

namespace finevox::net {

namespace {

constexpr uint8_t FLAG_COMPRESSED = 1;
constexpr uint8_t FLAG_FLUIDS = 2;
constexpr uint8_t FLAG_ROTATIONS = 4;

constexpr uint8_t LIGHT_ARRAY = 0;
constexpr uint8_t LIGHT_UNIFORM = 1;

/// A palette can't usefully be longer than there are cells
constexpr uint64_t MAX_PALETTE = SubchunkData::CELLS;

void writeBody(const SubchunkData& data, std::vector<uint8_t>& out) {
    ByteWriter w(out);
    w.var(data.palette.size());
    for (uint32_t id : data.palette) w.var(id);

    if (data.palette.size() > 1) {
        uint8_t width = data.palette.size() > 256 ? 2 : 1;
        w.u8(width);
        for (uint16_t index : data.indices) {
            if (width == 2) {
                w.u16(index);
            } else {
                w.u8(static_cast<uint8_t>(index));
            }
        }
    }

    bool uniform = std::all_of(data.light.begin(), data.light.end(),
                               [&](uint8_t v) { return v == data.light[0]; });
    if (uniform) {
        w.u8(LIGHT_UNIFORM);
        w.u8(data.light[0]);
    } else {
        w.u8(LIGHT_ARRAY);
        w.raw(data.light);
    }

    if (!data.fluids.empty()) {
        w.var(data.fluids.size());
        for (const auto& fluid : data.fluids) {
            w.u16(fluid.index);
            w.var(fluid.id);
            w.u8(fluid.level);
        }
    }
}

bool readBody(ByteReader& r, bool hasFluids, bool hasRotations, SubchunkData& data) {
    uint64_t count = r.var();
    if (!r.ok() || count == 0 || count > MAX_PALETTE) return false;
    data.palette.resize(count);
    for (auto& id : data.palette) id = static_cast<uint32_t>(r.var());

    data.indices.clear();
    if (count > 1) {
        uint8_t width = r.u8();
        if (width != 1 && width != 2) return false;
        data.indices.resize(SubchunkData::CELLS);
        for (auto& index : data.indices) {
            index = width == 2 ? r.u16() : r.u8();
            if (index >= count) return false;
        }
    }

    uint8_t lightMode = r.u8();
    if (lightMode == LIGHT_UNIFORM) {
        data.light.fill(r.u8());
    } else if (lightMode == LIGHT_ARRAY) {
        auto light = r.take(SubchunkData::CELLS);
        if (!r.ok()) return false;
        std::copy(light.begin(), light.end(), data.light.begin());
    } else {
        return false;
    }

    data.fluids.clear();
    if (hasFluids) {
        uint64_t fluids = r.var();
        if (fluids > SubchunkData::CELLS) return false;
        data.fluids.resize(fluids);
        for (auto& fluid : data.fluids) {
            fluid.index = r.u16();
            fluid.id = static_cast<uint32_t>(r.var());
            fluid.level = r.u8();
            if (fluid.index >= SubchunkData::CELLS) return false;
        }
    }
    if (hasRotations) r.take(SubchunkData::CELLS);  // Nothing uses them yet
    return r.ok();
}

}  // namespace

void encodeSubchunk(const SubchunkData& data, std::vector<uint8_t>& out, bool compress) {
    ByteWriter w(out);
    w.svar(data.pos.x);
    w.svar(data.pos.y);
    w.svar(data.pos.z);

    uint8_t flags = data.fluids.empty() ? 0 : FLAG_FLUIDS;
    std::vector<uint8_t> body;
    body.reserve(compress ? 8192 : 0);
    if (!compress) {
        w.u8(flags);
        writeBody(data, out);
        return;
    }

    writeBody(data, body);
    w.u8(flags | FLAG_COMPRESSED);
    encodeCompressed(body, out);
}

bool decodeSubchunk(std::span<const uint8_t> body, SubchunkData& data) {
    ByteReader r(body);
    data.pos.x = static_cast<int32_t>(r.svar());
    data.pos.y = static_cast<int32_t>(r.svar());
    data.pos.z = static_cast<int32_t>(r.svar());
    uint8_t flags = r.u8();
    if (!r.ok()) return false;

    bool fluids = flags & FLAG_FLUIDS;
    bool rotations = flags & FLAG_ROTATIONS;
    if (!(flags & FLAG_COMPRESSED)) return readBody(r, fluids, rotations, data);

    constexpr size_t LIMIT = SubchunkData::CELLS * 16;
    std::vector<uint8_t> raw;
    if (!decodeCompressed(r.take(r.remaining()), raw, LIMIT)) return false;
    ByteReader inner(raw);
    return readBody(inner, fluids, rotations, data);
}

void encodeCompressed(std::span<const uint8_t> raw, std::vector<uint8_t>& out) {
    ByteWriter w(out);
    w.var(raw.size());
    int bound = LZ4_compressBound(static_cast<int>(raw.size()));
    size_t start = out.size();
    out.resize(start + static_cast<size_t>(bound));
    int written = LZ4_compress_default(reinterpret_cast<const char*>(raw.data()),
                                       reinterpret_cast<char*>(out.data() + start),
                                       static_cast<int>(raw.size()), bound);
    out.resize(start + static_cast<size_t>(std::max(written, 0)));
}

bool decodeCompressed(std::span<const uint8_t> body, std::vector<uint8_t>& raw, size_t limit) {
    ByteReader r(body);
    uint64_t size = r.var();
    if (!r.ok() || size > limit) return false;
    auto block = r.take(r.remaining());
    raw.resize(size);
    int got = LZ4_decompress_safe(reinterpret_cast<const char*>(block.data()), reinterpret_cast<char*>(raw.data()),
                                  static_cast<int>(block.size()), static_cast<int>(size));
    return got == static_cast<int>(size);
}

}  // namespace finevox::net

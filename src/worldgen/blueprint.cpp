#include "finevox/worldgen/blueprint.hpp"
#include "finevox/worldgen/schematic_io.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace finevox::worldgen {

namespace {

constexpr char KEEP = ' ';
constexpr char CLEAR = '_';

// BlockSnapshot::isAir means "nothing here", which Schematic iteration skips. A cell
// that must be emptied needs a name that isn't that.
constexpr const char* CLEAR_TYPE = "blueprint:clear";

std::string trim(std::string_view text) {
    size_t begin = text.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) return {};
    size_t end = text.find_last_not_of(" \t\r");
    return std::string(text.substr(begin, end - begin + 1));
}

std::string rstrip(std::string line) {
    while (!line.empty() && (line.back() == '\r')) line.pop_back();
    return line;
}

}  // namespace

std::optional<Schematic> parseBlueprint(std::string_view text, std::string* error) {
    auto fail = [error](std::string message) -> std::optional<Schematic> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };

    std::unordered_map<char, std::string> legend;
    legend[CLEAR] = CLEAR_TYPE;

    std::vector<std::vector<std::string>> layers;
    std::istringstream stream{std::string(text)};
    std::string line;
    bool inLegend = true;

    while (std::getline(stream, line)) {
        line = rstrip(std::move(line));

        if (trim(line) == "---") {
            inLegend = false;
            layers.emplace_back();
            continue;
        }
        if (inLegend) {
            std::string content = trim(line);
            if (content.empty() || content[0] == '#') continue;
            auto eq = content.find('=');
            if (eq == std::string::npos || eq == 0) return fail("bad legend line: " + content);
            std::string symbol = trim(content.substr(0, eq));
            if (symbol.size() != 1) return fail("legend symbol must be one character: " + symbol);
            legend[symbol[0]] = trim(content.substr(eq + 1));
            continue;
        }
        if (!line.empty() && line[0] == '#') continue;
        layers.back().push_back(line);
    }

    if (layers.empty()) return fail("no layers");

    int32_t sizeX = 0;
    int32_t sizeZ = 0;
    for (const auto& layer : layers) {
        sizeZ = std::max(sizeZ, static_cast<int32_t>(layer.size()));
        for (const auto& row : layer) {
            sizeX = std::max(sizeX, static_cast<int32_t>(row.size()));
        }
    }
    if (sizeX == 0 || sizeZ == 0) return fail("empty blueprint");

    Schematic schematic(sizeX, static_cast<int32_t>(layers.size()), sizeZ);
    for (size_t y = 0; y < layers.size(); ++y) {
        for (size_t z = 0; z < layers[y].size(); ++z) {
            const std::string& row = layers[y][z];
            for (size_t x = 0; x < row.size(); ++x) {
                char symbol = row[x];
                if (symbol == KEEP) continue;
                auto it = legend.find(symbol);
                if (it == legend.end()) {
                    return fail(std::string("unknown symbol '") + symbol + "'");
                }
                schematic.at(int32_t(x), int32_t(y), int32_t(z)).typeName = it->second;
            }
        }
    }
    return schematic;
}

void BlueprintLibrary::add(std::string name, Schematic schematic) {
    blueprints_[std::move(name)] = std::make_unique<Schematic>(std::move(schematic));
}

const Schematic* BlueprintLibrary::find(std::string_view name) const {
    auto it = blueprints_.find(std::string(name));
    return it == blueprints_.end() ? nullptr : it->second.get();
}

std::vector<std::string> BlueprintLibrary::names() const {
    std::vector<std::string> result;
    result.reserve(blueprints_.size());
    for (const auto& [name, schematic] : blueprints_) result.push_back(name);
    std::sort(result.begin(), result.end());
    return result;
}

size_t BlueprintLibrary::loadDirectory(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(dir)) return 0;

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::string name = entry.path().stem().string();
        if (entry.path().extension() == ".blueprint") {
            std::ifstream file(entry.path());
            std::ostringstream text;
            text << file.rdbuf();
            std::string error;
            auto schematic = parseBlueprint(text.str(), &error);
            if (!schematic) {
                std::cerr << "[Blueprint] " << entry.path() << ": " << error << "\n";
                continue;
            }
            add(name, std::move(*schematic));
            ++count;
        } else if (entry.path().extension() == ".vxsc") {
            add(name, loadSchematic(entry.path()));
            ++count;
        }
    }
    return count;
}

BlueprintSite::BlueprintSite(const Schematic& schematic, BlockCoord origin, int quarterTurns)
    : origin_(origin) {
    int turns = ((quarterTurns % 4) + 4) % 4;
    Rotation rotation = Rotation::IDENTITY;
    for (int i = 0; i < turns; ++i) rotation = Rotation::ROTATE_Y_90.compose(rotation);

    auto stamp = [this, origin](const Schematic& source) {
        source.forEachBlock([this, origin](glm::ivec3 pos, const BlockSnapshot& snapshot) {
            addCell(BlockCoord(origin.x + pos.x, origin.y + pos.y, origin.z + pos.z),
                    snapshot.typeName == CLEAR_TYPE ? BlockTypeId()
                                                    : BlockTypeId::fromName(snapshot.typeName),
                    snapshot.rotation);
        });
    };
    if (turns == 0) {
        stamp(schematic);
    } else {
        stamp(rotateSchematic(schematic, rotation));
    }
}

void BlueprintSite::addCell(BlockCoord pos, BlockTypeId block, Rotation rotation) {
    // One target per position. Two would have agents undoing each other forever.
    for (const SiteCell& existing : cells_) {
        if (existing.pos == pos) return;
    }

    SiteCell cell;
    cell.pos = pos;
    cell.block = block;
    cell.rotation = rotation;
    cells_.push_back(cell);
}

bool BlueprintSite::isDone(const World& world, const SiteCell& cell) {
    return world.getBlock(cell.pos) == cell.block;
}

std::optional<SiteCell> BlueprintSite::claimNext(const World& world, const glm::dvec3& near,
                                                 EntityId agent) {
    int32_t lowest = std::numeric_limits<int32_t>::max();
    for (const SiteCell& cell : cells_) {
        if (!isDone(world, cell)) lowest = std::min(lowest, cell.pos.y);
    }
    if (lowest == std::numeric_limits<int32_t>::max()) return std::nullopt;

    SiteCell* best = nullptr;
    double bestDistance = std::numeric_limits<double>::max();
    for (SiteCell& cell : cells_) {
        if (cell.pos.y != lowest || isDone(world, cell)) continue;
        if (cell.claimedBy != INVALID_ENTITY_ID && cell.claimedBy != agent) continue;
        double dx = cell.pos.x + 0.5 - near.x;
        double dz = cell.pos.z + 0.5 - near.z;
        double distance = dx * dx + dz * dz;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &cell;
        }
    }
    if (!best) return std::nullopt;
    best->claimedBy = agent;
    return *best;
}

void BlueprintSite::release(BlockCoord pos, EntityId agent) {
    for (SiteCell& cell : cells_) {
        if (cell.pos == pos && cell.claimedBy == agent) cell.claimedBy = INVALID_ENTITY_ID;
    }
}

void BlueprintSite::releaseAgent(EntityId agent) {
    for (SiteCell& cell : cells_) {
        if (cell.claimedBy == agent) cell.claimedBy = INVALID_ENTITY_ID;
    }
}

SiteProgress BlueprintSite::progress(const World& world) const {
    SiteProgress result;
    result.total = cells_.size();
    for (const SiteCell& cell : cells_) {
        if (isDone(world, cell)) ++result.done;
    }
    return result;
}

std::vector<SiteCell> BlueprintSite::remaining(const World& world) const {
    std::vector<SiteCell> result;
    for (const SiteCell& cell : cells_) {
        if (!isDone(world, cell)) result.push_back(cell);
    }
    return result;
}

}  // namespace finevox::worldgen

#pragma once

/**
 * @file density_graph.hpp
 * @brief Terrain described as a graph of numbers, in a file a game writes
 *
 * A `.terrain` file names nodes, each an operation on numbers or on earlier nodes:
 *
 *     node:land: noise scale=0.002 octaves=5 seed=1
 *     node:height: curve land -1:40 0:64 1:110
 *     node:ground: sub height y
 *     terrain: ground                 # solid where this is above zero
 *     field:wet: noise scale=0.001 seed=7
 *
 * Operations: `const v`, `x`, `y`, `z`, `noise`, `ridged` (scale=, scale_y=, octaves=, seed=,
 * lacunarity=, gain=, and `3d` for a noise that varies with height), `add`, `mul` (any number
 * of inputs), `sub`, `div`, `min`, `max`, `neg`, `abs`, `square`, `clamp a lo hi`, `lerp t a b`
 * (t clamped to 0..1), and `curve a x:y x:y ...` (straight between points, flat beyond them).
 * An input is an earlier node's name, a number, or `x`, `y` or `z` for where the sample is. A node that never depends on height is worked
 * out once per column. `field:` nodes are columns' values a game can choose biomes by.
 *
 * Settings: `fill: <block>` (stone), `bottom:` and `top:` (the heights generated), `cell: h v`
 * (density is sampled every h blocks across and v up, and blended between; 4 and 8),
 * `floor: <block> <depth>` (a floor at the bottom, ragged up to depth), and any number of
 * `layer: <block> below <y> [blend <n>]`: the fill is that block under y, mixed into the n
 * blocks above it, thinning upward. The first layer that applies wins.
 */

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace finevox::worldgen {

class Noise2D;
class Noise3D;

class DensityGraph {
public:
    /// Read a graph. Seeds in it are mixed with worldSeed. Nothing on error, with the reason.
    static std::optional<DensityGraph> parse(const std::string& text, uint64_t worldSeed,
                                             std::string* error = nullptr);
    static std::optional<DensityGraph> load(const std::string& path, uint64_t worldSeed,
                                            std::string* error = nullptr);

    /// Node index by name, or -1
    [[nodiscard]] int find(const std::string& name) const;
    [[nodiscard]] bool dependsOnHeight(int node) const { return nodes_[static_cast<size_t>(node)].is3D; }
    [[nodiscard]] size_t size() const { return nodes_.size(); }

    /// Every node that doesn't depend on height, at a column. `values` is sized to size().
    void evaluateColumn(float x, float z, std::vector<float>& values) const;

    /// Every node that does, at a height in that column, after evaluateColumn filled `values`
    void evaluateHeight(float x, float y, float z, std::vector<float>& values) const;

    /// One node anywhere, working out what it needs. Slower than the two above in a loop.
    [[nodiscard]] float evaluate(int node, float x, float y, float z) const;

    // Settings
    [[nodiscard]] int terrainNode() const { return terrain_; }
    [[nodiscard]] const std::vector<std::pair<std::string, int>>& fields() const { return fields_; }
    [[nodiscard]] const std::string& fillBlock() const { return fill_; }
    [[nodiscard]] int32_t bottom() const { return bottom_; }
    [[nodiscard]] int32_t top() const { return top_; }
    [[nodiscard]] int32_t cellAcross() const { return cellAcross_; }
    [[nodiscard]] int32_t cellUp() const { return cellUp_; }
    [[nodiscard]] const std::string& floorBlock() const { return floor_; }
    [[nodiscard]] int32_t floorDepth() const { return floorDepth_; }
    struct Layer {
        std::string block;
        int32_t below = 0;
        int32_t blend = 0;
    };
    [[nodiscard]] const std::vector<Layer>& layers() const { return layers_; }

    DensityGraph();
    ~DensityGraph();
    DensityGraph(DensityGraph&&) noexcept;
    DensityGraph& operator=(DensityGraph&&) noexcept;

private:
    enum class Op : uint8_t {
        Const, X, Y, Z, Noise2, Noise3, Add, Mul, Sub, Div, Min, Max, Neg, Abs, Square,
        Clamp, Lerp, Curve
    };
    struct Node {
        Op op = Op::Const;
        bool is3D = false;
        float value = 0.0f;
        std::vector<int> inputs;
        std::vector<std::pair<float, float>> points;  // curve
        float scale = 1.0f, scaleY = 1.0f;
        std::shared_ptr<Noise2D> noise2;
        std::shared_ptr<Noise3D> noise3;
    };

    [[nodiscard]] float apply(const Node& node, float x, float y, float z,
                              const std::vector<float>& values) const;

    std::vector<Node> nodes_;
    std::unordered_map<std::string, int> names_;
    int terrain_ = -1;
    std::vector<std::pair<std::string, int>> fields_;
    std::string fill_ = "stone";
    int32_t bottom_ = 0;
    int32_t top_ = 256;
    int32_t cellAcross_ = 4;
    int32_t cellUp_ = 8;
    std::string floor_;
    int32_t floorDepth_ = 0;
    std::vector<Layer> layers_;
};

}  // namespace finevox::worldgen

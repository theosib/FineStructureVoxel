#include "finevox/worldgen/density_graph.hpp"

#include "finevox/worldgen/noise.hpp"
#include "finevox/worldgen/noise.hpp"
#include "finevox/worldgen/noise_ops.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace finevox::worldgen {

DensityGraph::DensityGraph() = default;
DensityGraph::~DensityGraph() = default;
DensityGraph::DensityGraph(DensityGraph&&) noexcept = default;
DensityGraph& DensityGraph::operator=(DensityGraph&&) noexcept = default;

namespace {

std::vector<std::string> words(const std::string& text) {
    std::istringstream iss(text);
    std::vector<std::string> out;
    std::string w;
    while (iss >> w) out.push_back(w);
    return out;
}

std::optional<float> number(const std::string& text) {
    char* end = nullptr;
    float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return std::nullopt;
    return v;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

}  // namespace

std::optional<DensityGraph> DensityGraph::load(const std::string& path, uint64_t worldSeed,
                                               std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "can't read " + path;
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return parse(buffer.str(), worldSeed, error);
}

std::optional<DensityGraph> DensityGraph::parse(const std::string& text, uint64_t worldSeed,
                                                std::string* error) {
    DensityGraph g;
    int lineNo = 0;
    auto fail = [&](const std::string& why) -> std::optional<DensityGraph> {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + why;
        return std::nullopt;
    };

    // An input: an earlier node, or a number made into a constant node
    auto input = [&g](const std::string& token) -> int {
        if (auto v = number(token)) {
            Node n;
            n.op = Op::Const;
            n.value = *v;
            g.nodes_.push_back(std::move(n));
            return static_cast<int>(g.nodes_.size()) - 1;
        }
        auto it = g.names_.find(token);
        if (it != g.names_.end()) return it->second;
        // Where the sample is, by name, unless a node has taken the name
        if (token == "x" || token == "y" || token == "z") {
            Node n;
            n.op = token == "x" ? Op::X : token == "y" ? Op::Y : Op::Z;
            n.is3D = token == "y";
            g.nodes_.push_back(std::move(n));
            return static_cast<int>(g.nodes_.size()) - 1;
        }
        return -1;
    };

    // Parse `op args...` into a node, returning its index or -1
    auto build = [&](const std::vector<std::string>& w, std::string& why) -> int {
        if (w.empty()) {
            why = "no operation";
            return -1;
        }
        const std::string& op = w[0];
        Node n;
        auto addInputs = [&](size_t from, size_t minCount, size_t maxCount) -> bool {
            for (size_t i = from; i < w.size(); ++i) {
                int in = input(w[i]);
                if (in < 0) {
                    why = "no node called " + w[i];
                    return false;
                }
                n.inputs.push_back(in);
            }
            if (n.inputs.size() < minCount || n.inputs.size() > maxCount) {
                why = op + " takes " + std::to_string(minCount) +
                      (maxCount == minCount ? "" : " or more") + " inputs";
                return false;
            }
            return true;
        };

        static const std::unordered_map<std::string, std::pair<Op, std::pair<size_t, size_t>>> simple = {
            {"add", {Op::Add, {2, 64}}}, {"mul", {Op::Mul, {2, 64}}}, {"sub", {Op::Sub, {2, 2}}},
            {"div", {Op::Div, {2, 2}}}, {"min", {Op::Min, {2, 64}}}, {"max", {Op::Max, {2, 64}}},
            {"neg", {Op::Neg, {1, 1}}}, {"abs", {Op::Abs, {1, 1}}}, {"square", {Op::Square, {1, 1}}},
            {"clamp", {Op::Clamp, {3, 3}}}, {"lerp", {Op::Lerp, {3, 3}}},
        };

        if (op == "const") {
            auto v = w.size() == 2 ? number(w[1]) : std::nullopt;
            if (!v) {
                why = "const takes a number";
                return -1;
            }
            n.op = Op::Const;
            n.value = *v;
        } else if (op == "x" || op == "y" || op == "z") {
            n.op = op == "x" ? Op::X : op == "y" ? Op::Y : Op::Z;
            n.is3D = op == "y";
        } else if (op == "noise" || op == "ridged") {
            int octaves = 4;
            float lacunarity = 2.0f, gain = 0.5f;
            uint64_t salt = 0;
            bool threeD = false;
            bool scaleYSet = false;
            for (size_t i = 1; i < w.size(); ++i) {
                auto eq = w[i].find('=');
                std::string key = eq == std::string::npos ? w[i] : w[i].substr(0, eq);
                std::string val = eq == std::string::npos ? "" : w[i].substr(eq + 1);
                auto v = number(val);
                if (key == "3d") {
                    threeD = true;
                } else if (!v) {
                    why = "bad setting " + w[i];
                    return -1;
                } else if (key == "scale") {
                    n.scale = *v;
                } else if (key == "scale_y") {
                    n.scaleY = *v;
                    scaleYSet = true;
                } else if (key == "octaves") {
                    octaves = std::max(1, static_cast<int>(*v));
                } else if (key == "seed") {
                    salt = static_cast<uint64_t>(*v);
                } else if (key == "lacunarity") {
                    lacunarity = *v;
                } else if (key == "gain") {
                    gain = *v;
                } else {
                    why = "no setting " + key;
                    return -1;
                }
            }
            if (!scaleYSet) n.scaleY = n.scale;
            uint64_t seed = NoiseHash::deriveSeed(worldSeed, 0x7e44a1u + salt * 7919u);
            if (threeD) {
                n.op = Op::Noise3;
                n.is3D = true;
                if (op == "noise") {
                    n.noise3 = std::make_shared<FBMNoise3D>(std::make_unique<OpenSimplex3D>(seed), octaves, lacunarity, gain);
                } else {
                    n.noise3 = std::make_shared<RidgedNoise3D>(std::make_unique<OpenSimplex3D>(seed), octaves, lacunarity, gain);
                }
            } else {
                n.op = Op::Noise2;
                if (op == "noise") {
                    n.noise2 = std::make_shared<FBMNoise2D>(std::make_unique<OpenSimplex2D>(seed), octaves, lacunarity, gain);
                } else {
                    n.noise2 = std::make_shared<RidgedNoise2D>(std::make_unique<OpenSimplex2D>(seed), octaves, lacunarity, gain);
                }
            }
        } else if (op == "curve") {
            n.op = Op::Curve;
            if (w.size() < 3) {
                why = "curve takes an input and points";
                return -1;
            }
            int in = input(w[1]);
            if (in < 0) {
                why = "no node called " + w[1];
                return -1;
            }
            n.inputs.push_back(in);
            for (size_t i = 2; i < w.size(); ++i) {
                auto colon = w[i].find(':');
                auto a = colon == std::string::npos ? std::nullopt : number(w[i].substr(0, colon));
                auto b = colon == std::string::npos ? std::nullopt : number(w[i].substr(colon + 1));
                if (!a || !b) {
                    why = "curve points are x:y, not " + w[i];
                    return -1;
                }
                n.points.emplace_back(*a, *b);
            }
            std::sort(n.points.begin(), n.points.end());
        } else if (auto it = simple.find(op); it != simple.end()) {
            n.op = it->second.first;
            if (!addInputs(1, it->second.second.first, it->second.second.second)) return -1;
        } else {
            why = "no operation " + op;
            return -1;
        }
        for (int in : n.inputs) n.is3D = n.is3D || g.nodes_[static_cast<size_t>(in)].is3D;
        g.nodes_.push_back(std::move(n));
        return static_cast<int>(g.nodes_.size()) - 1;
    };

    std::istringstream lines(text);
    std::string line;
    std::vector<std::pair<std::string, std::string>> fieldsPending;
    std::string terrainName;
    while (std::getline(lines, line)) {
        ++lineNo;
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;

        auto colon = line.find(':');
        if (colon == std::string::npos) return fail("expected key: value");
        std::string key = line.substr(0, colon);
        std::string rest = line.substr(colon + 1);
        std::string suffix;
        // key:name: value
        auto second = rest.find(':');
        if ((key == "node" || key == "field") && second != std::string::npos) {
            suffix = trim(rest.substr(0, second));
            rest = rest.substr(second + 1);
        }
        rest = trim(rest);
        auto w = words(rest);

        if (key == "node" || key == "field") {
            if (suffix.empty()) return fail(key + " needs a name: " + key + ":<name>: ...");
            std::string why;
            // A field may name a node, or define one in place
            int index = (key == "field" && w.size() == 1) ? input(w[0]) : build(w, why);
            if (index < 0) return fail(why.empty() ? "no node called " + rest : why);
            if (key == "node") {
                if (g.names_.count(suffix)) return fail("a second node called " + suffix);
                g.names_[suffix] = index;
            } else {
                if (g.nodes_[static_cast<size_t>(index)].is3D) return fail("field " + suffix + " depends on height");
                g.fields_.emplace_back(suffix, index);
            }
        } else if (key == "terrain") {
            if (w.size() != 1) return fail("terrain names one node");
            terrainName = w[0];
            int index = input(w[0]);
            if (index < 0) return fail("no node called " + w[0]);
            g.terrain_ = index;
        } else if (key == "fill") {
            if (w.size() != 1) return fail("fill names one block");
            g.fill_ = w[0];
        } else if (key == "bottom" || key == "top") {
            auto v = w.size() == 1 ? number(w[0]) : std::nullopt;
            if (!v) return fail(key + " takes a height");
            (key == "bottom" ? g.bottom_ : g.top_) = static_cast<int32_t>(*v);
        } else if (key == "cell") {
            auto a = w.size() == 2 ? number(w[0]) : std::nullopt;
            auto b = w.size() == 2 ? number(w[1]) : std::nullopt;
            if (!a || !b || *a < 1 || *b < 1 || 16 % static_cast<int>(*a) != 0) {
                return fail("cell takes two sizes, the first dividing 16");
            }
            g.cellAcross_ = static_cast<int32_t>(*a);
            g.cellUp_ = static_cast<int32_t>(*b);
        } else if (key == "floor") {
            auto d = w.size() == 2 ? number(w[1]) : std::nullopt;
            if (!d) return fail("floor takes a block and a depth");
            g.floor_ = w[0];
            g.floorDepth_ = static_cast<int32_t>(*d);
        } else if (key == "layer") {
            Layer layer;
            auto below = w.size() >= 3 && w[1] == "below" ? number(w[2]) : std::nullopt;
            if (!below) return fail("layer takes a block, below <y>, and blend <n>");
            layer.block = w[0];
            layer.below = static_cast<int32_t>(*below);
            if (w.size() >= 5 && w[3] == "blend") {
                auto blend = number(w[4]);
                if (!blend) return fail("blend takes a number of blocks");
                layer.blend = static_cast<int32_t>(*blend);
            }
            g.layers_.push_back(std::move(layer));
        } else {
            return fail("no setting " + key);
        }
    }
    if (g.terrain_ < 0) return fail("no terrain: line");
    if (g.top_ <= g.bottom_) return fail("top must be above bottom");
    return g;
}

int DensityGraph::find(const std::string& name) const {
    auto it = names_.find(name);
    return it == names_.end() ? -1 : it->second;
}

float DensityGraph::apply(const Node& n, float x, float y, float z, const std::vector<float>& v) const {
    auto in = [&](size_t i) { return v[static_cast<size_t>(n.inputs[i])]; };
    switch (n.op) {
        case Op::Const: return n.value;
        case Op::X: return x;
        case Op::Y: return y;
        case Op::Z: return z;
        case Op::Noise2: return n.noise2->evaluate(x * n.scale, z * n.scale);
        case Op::Noise3: return n.noise3->evaluate(x * n.scale, y * n.scaleY, z * n.scale);
        case Op::Add: {
            float s = 0;
            for (size_t i = 0; i < n.inputs.size(); ++i) s += in(i);
            return s;
        }
        case Op::Mul: {
            float s = 1;
            for (size_t i = 0; i < n.inputs.size(); ++i) s *= in(i);
            return s;
        }
        case Op::Sub: return in(0) - in(1);
        case Op::Div: {
            float d = in(1);
            return d == 0.0f ? 0.0f : in(0) / d;
        }
        case Op::Min: {
            float s = in(0);
            for (size_t i = 1; i < n.inputs.size(); ++i) s = std::min(s, in(i));
            return s;
        }
        case Op::Max: {
            float s = in(0);
            for (size_t i = 1; i < n.inputs.size(); ++i) s = std::max(s, in(i));
            return s;
        }
        case Op::Neg: return -in(0);
        case Op::Abs: return std::fabs(in(0));
        case Op::Square: return in(0) * in(0);
        case Op::Clamp: return std::clamp(in(0), in(1), std::max(in(1), in(2)));
        case Op::Lerp: {
            float t = std::clamp(in(0), 0.0f, 1.0f);
            return in(1) + (in(2) - in(1)) * t;
        }
        case Op::Curve: {
            float a = in(0);
            const auto& p = n.points;
            if (p.empty()) return a;
            if (a <= p.front().first) return p.front().second;
            if (a >= p.back().first) return p.back().second;
            for (size_t i = 1; i < p.size(); ++i) {
                if (a <= p[i].first) {
                    float span = p[i].first - p[i - 1].first;
                    float t = span > 0 ? (a - p[i - 1].first) / span : 0.0f;
                    return p[i - 1].second + (p[i].second - p[i - 1].second) * t;
                }
            }
            return p.back().second;
        }
    }
    return 0.0f;
}

void DensityGraph::evaluateColumn(float x, float z, std::vector<float>& values) const {
    values.resize(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (!nodes_[i].is3D) values[i] = apply(nodes_[i], x, 0.0f, z, values);
    }
}

void DensityGraph::evaluateHeight(float x, float y, float z, std::vector<float>& values) const {
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].is3D) values[i] = apply(nodes_[i], x, y, z, values);
    }
}

float DensityGraph::evaluate(int node, float x, float y, float z) const {
    std::vector<float> values;
    evaluateColumn(x, z, values);
    evaluateHeight(x, y, z, values);
    return values[static_cast<size_t>(node)];
}

}  // namespace finevox::worldgen

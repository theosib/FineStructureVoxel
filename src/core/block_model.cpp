#include "finevox/core/block_model.hpp"
#include <algorithm>
#include <sstream>
#include <array>
#include <cctype>
#include <string_view>

namespace finevox {

// ============================================================================
// Face name alias table
// ============================================================================

namespace {

// Map of face name aliases to indices
const std::unordered_map<std::string, int> faceAliases = {
    // NegX = 0 (West)
    {"negx", 0}, {"west", 0}, {"w", 0}, {"-x", 0},
    // PosX = 1 (East)
    {"posx", 1}, {"east", 1}, {"e", 1}, {"+x", 1},
    // NegY = 2 (Down/Bottom)
    {"negy", 2}, {"down", 2}, {"bottom", 2}, {"d", 2}, {"-y", 2},
    // PosY = 3 (Up/Top)
    {"posy", 3}, {"up", 3}, {"top", 3}, {"u", 3}, {"+y", 3},
    // NegZ = 4 (North)
    {"negz", 4}, {"north", 4}, {"n", 4}, {"-z", 4},
    // PosZ = 5 (South)
    {"posz", 5}, {"south", 5}, {"s", 5}, {"+z", 5},
};

std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

}  // namespace

// ============================================================================
// FaceGeometry implementation
// ============================================================================

AABB FaceGeometry::computeBounds() const {
    if (vertices.empty()) {
        return AABB();
    }

    glm::vec3 minPt = vertices[0].position;
    glm::vec3 maxPt = vertices[0].position;

    for (size_t i = 1; i < vertices.size(); ++i) {
        minPt = glm::min(minPt, vertices[i].position);
        maxPt = glm::max(maxPt, vertices[i].position);
    }

    return AABB(minPt, maxPt);
}

std::optional<Face> FaceGeometry::standardFace() const {
    if (faceIndex >= 0 && faceIndex < 6) {
        return static_cast<Face>(faceIndex);
    }
    return std::nullopt;
}

// ============================================================================
// BlockGeometry implementation
// ============================================================================

void BlockGeometry::addFace(FaceGeometry face) {
    // Assign index if not set
    if (face.faceIndex < 0) {
        face.faceIndex = nextCustomIndex_++;
    } else if (face.faceIndex >= 6) {
        nextCustomIndex_ = std::max(nextCustomIndex_, face.faceIndex + 1);
    }

    size_t idx = faces_.size();
    faces_.push_back(std::move(face));

    // Update lookups
    if (!faces_[idx].name.empty()) {
        facesByName_[faces_[idx].name] = idx;
    }
    facesByIndex_[faces_[idx].faceIndex] = idx;

    // Update bounds
    updateBounds(faces_[idx]);

    if (faces_[idx].connection >= 0) hasConnections_ = true;
}

const FaceGeometry* BlockGeometry::getFace(const std::string& name) const {
    auto it = facesByName_.find(name);
    if (it != facesByName_.end()) {
        return &faces_[it->second];
    }
    return nullptr;
}

const FaceGeometry* BlockGeometry::getFace(int index) const {
    auto it = facesByIndex_.find(index);
    if (it != facesByIndex_.end()) {
        return &faces_[it->second];
    }
    return nullptr;
}

const FaceGeometry* BlockGeometry::getStandardFace(Face face) const {
    return getFace(static_cast<int>(face));
}

uint8_t BlockGeometry::solidFacesMask() const {
    uint8_t mask = 0;
    for (const auto& face : faces_) {
        if (face.isSolid && face.faceIndex >= 0 && face.faceIndex < 6) {
            mask |= (1 << face.faceIndex);
        }
    }
    return mask;
}

CollisionShape BlockGeometry::computeCollisionFromFaces() const {
    CollisionShape shape;

    for (const auto& face : faces_) {
        if (face.isSolid && !face.vertices.empty()) {
            // Compute AABB for this face and add to collision
            AABB faceBounds = face.computeBounds();

            // Ensure non-zero thickness for flat faces
            glm::vec3 size = faceBounds.size();
            if (size.x < 0.001f) {
                faceBounds.min.x -= 0.001f;
                faceBounds.max.x += 0.001f;
            }
            if (size.y < 0.001f) {
                faceBounds.min.y -= 0.001f;
                faceBounds.max.y += 0.001f;
            }
            if (size.z < 0.001f) {
                faceBounds.min.z -= 0.001f;
                faceBounds.max.z += 0.001f;
            }

            shape.addBox(faceBounds);
        }
    }

    return shape;
}

void BlockGeometry::updateBounds(const FaceGeometry& face) {
    AABB faceBounds = face.computeBounds();

    if (faces_.size() == 1) {
        bounds_ = faceBounds;
    } else {
        bounds_ = bounds_.merged(faceBounds);
    }
}

// ============================================================================
// RotationSet utilities
// ============================================================================

std::vector<uint8_t> getRotationIndices(RotationSet set) {
    switch (set) {
        case RotationSet::None:
            return {0};

        case RotationSet::Vertical:
            // Identity and upside-down
            // Rotation index 0 = identity, need to find upside-down index
            // Based on Rotation class, need to check which index is 180° around X
            return {0, 12};  // Assuming index 12 is upside-down

        case RotationSet::Horizontal:
            // 4 rotations around Y axis (0°, 90°, 180°, 270°)
            return {0, 1, 2, 3};

        case RotationSet::HorizontalFlip:
            // Horizontal rotations + their upside-down variants
            return {0, 1, 2, 3, 12, 13, 14, 15};

        case RotationSet::All:
            {
                std::vector<uint8_t> all(24);
                for (int i = 0; i < 24; ++i) {
                    all[i] = static_cast<uint8_t>(i);
                }
                return all;
            }

        case RotationSet::Custom:
            return {};  // Caller must provide custom list
    }

    return {0};
}

RotationSet parseRotationSet(const std::string& str) {
    std::string lower = toLower(str);

    if (lower == "none" || lower == "fixed") {
        return RotationSet::None;
    }
    if (lower == "vertical" || lower == "top-bottom") {
        return RotationSet::Vertical;
    }
    if (lower == "horizontal" || lower == "y-axis") {
        return RotationSet::Horizontal;
    }
    if (lower == "horizontal-flip" || lower == "stairs") {
        return RotationSet::HorizontalFlip;
    }
    if (lower == "all" || lower == "full") {
        return RotationSet::All;
    }

    // Assume custom if not recognized
    return RotationSet::Custom;
}

// ============================================================================
// BlockModel implementation
// ============================================================================

BlockModel& BlockModel::setFarGeometry(BlockGeometry geometry) {
    farGeometry_ = std::move(geometry);
    return *this;
}

BlockModel& BlockModel::setGeometry(BlockGeometry geometry) {
    geometry_ = std::move(geometry);
    collisionResolved_ = false;
    hitResolved_ = false;
    return *this;
}

BlockModel& BlockModel::setCollision(CollisionShape shape) {
    collision_ = std::move(shape);
    hasExplicitCollision_ = true;
    collisionResolved_ = false;
    hitResolved_ = false;
    return *this;
}

BlockModel& BlockModel::setHit(CollisionShape shape) {
    hit_ = std::move(shape);
    hasExplicitHit_ = true;
    hitResolved_ = false;
    return *this;
}

BlockModel& BlockModel::setRotations(RotationSet set) {
    rotationSet_ = set;
    customRotations_.clear();
    return *this;
}

BlockModel& BlockModel::setRotations(std::vector<uint8_t> indices) {
    rotationSet_ = RotationSet::Custom;
    customRotations_ = std::move(indices);
    return *this;
}

BlockModel& BlockModel::setHardness(float hardness) {
    hardness_ = hardness;
    return *this;
}

BlockModel& BlockModel::setTexture(const std::string& texture) {
    texture_ = texture;
    return *this;
}

BlockModel& BlockModel::setFaceTextures(const std::string& top, const std::string& bottom,
                                        const std::string& side) {
    textureTop_ = top;
    textureBottom_ = bottom;
    textureSide_ = side;
    return *this;
}

BlockModel& BlockModel::setFaceTexture(Face face, const std::string& texture) {
    faceTextures_[static_cast<size_t>(face)] = texture;
    return *this;
}

BlockModel& BlockModel::setTintSource(const std::string& source) {
    tintSource_ = source;
    return *this;
}

BlockModel& BlockModel::setTintColor(glm::vec3 color) {
    tintColor_ = color;
    return *this;
}

BlockModel& BlockModel::setFaceTint(Face face, int tint) {
    faceTint_[static_cast<size_t>(face)] = tint;
    return *this;
}

BlockModel& BlockModel::setTextureVariable(const std::string& name, const std::string& texture) {
    textureVariables_[name] = texture;
    return *this;
}

std::string BlockModel::cubeFaceTexture(Face face) const {
    const std::string& own = faceTextures_[static_cast<size_t>(face)];
    if (!own.empty()) return own;
    const std::string* general = &textureSide_;
    if (face == Face::PosY) general = &textureTop_;
    if (face == Face::NegY) general = &textureBottom_;
    return general->empty() ? texture_ : *general;
}

void BlockModel::resolveFaceTextures() {
    auto resolve = [this](BlockGeometry& geometry) {
        for (auto& face : geometry.mutableFaces()) {
            std::string ref = face.textureRef;
            // A variable may name another; a few steps is plenty, and stops a cycle
            for (int step = 0; step < 8 && !ref.empty() && ref[0] == '#'; ++step) {
                auto it = textureVariables_.find(ref.substr(1));
                if (it == textureVariables_.end()) break;
                ref = it->second;
            }
            // A name no variable here fills keeps what it was given before: a look whose faces
            // were resolved by the model that made them
            if (ref.empty() || ref[0] != '#' || face.texture.empty() || face.texture[0] == '#') {
                face.texture = ref;
            }
            face.hasTextureUV = false;
        }
    };
    resolve(geometry_);
    resolve(farGeometry_);
}

void BlockModel::takeAppearance(const BlockModel& look) {
    geometry_ = look.geometry_;
    farGeometry_ = look.farGeometry_;
    texture_ = look.texture_;
    textureTop_ = look.textureTop_;
    textureBottom_ = look.textureBottom_;
    textureSide_ = look.textureSide_;
    faceTextures_ = look.faceTextures_;
    faceTint_ = look.faceTint_;
    // The block's own variables win: a shared look can leave `#name` for each block to fill
    for (const auto& [name, texture] : look.textureVariables_) textureVariables_.try_emplace(name, texture);
    if (look.hasExplicitCollision_) setCollision(look.collision_);
    if (look.hasExplicitHit_) setHit(look.hit_);
    collisionResolved_ = false;
    hitResolved_ = false;
}

BlockModel& BlockModel::addProperty(BlockProperty property) {
    for (auto& existing : properties_) {
        if (existing.name == property.name) {
            existing = std::move(property);
            return *this;
        }
    }
    properties_.push_back(std::move(property));
    return *this;
}

BlockModel& BlockModel::addVariant(PropertyLook look) {
    variants_.push_back(std::move(look));
    return *this;
}

BlockModel& BlockModel::addStateSetting(StateSetting setting) {
    stateSettings_.push_back(std::move(setting));
    return *this;
}

void BlockModel::applySetting(const StateSetting& setting) {
    for (const auto& [key, value] : setting.values) {
        float number = std::strtof(value.c_str(), nullptr);
        if (key == "light-emission") {
            setLightEmission(static_cast<uint8_t>(std::clamp(number, 0.0f, 15.0f)));
        } else if (key == "light-attenuation") {
            setLightAttenuation(static_cast<uint8_t>(std::clamp(number, 0.0f, 15.0f)));
        } else if (key == "hardness") {
            setHardness(number);
        } else if (key == "opaque") {
            setOpaque(value == "true");
        } else if (key == "collision") {
            if (value == "none") setCollision(CollisionShape::NONE);
            if (value == "full") setCollision(CollisionShape::FULL_BLOCK);
        } else if (key == "walk-speed") {
            setWalkSpeed(number);
        }
    }
}

BlockModel& BlockModel::addPart(PropertyLook look) {
    parts_.push_back(std::move(look));
    return *this;
}

namespace {

std::vector<std::string> splitOn(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == sep) {
            out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    out.push_back(current);
    return out;
}

}  // namespace

PropertyCondition PropertyCondition::parse(const std::string& text) {
    PropertyCondition cond;
    if (text.empty() || text == "*") return cond;
    for (const auto& term : splitOn(text, ',')) {
        auto eq = term.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        cond.terms.emplace_back(term.substr(0, eq), splitOn(term.substr(eq + 1), '|'));
    }
    return cond;
}

bool PropertyCondition::matches(const std::vector<std::string>& propertyNames,
                                  const std::vector<std::string>& values) const {
    for (const auto& [name, allowed] : terms) {
        auto it = std::find(propertyNames.begin(), propertyNames.end(), name);
        if (it == propertyNames.end()) return false;  // names a property the block lacks
        const std::string& value = values[static_cast<size_t>(it - propertyNames.begin())];
        if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) return false;
    }
    return true;
}

PropertyLook PropertyLook::parse(const std::string& when, const std::string& value) {
    PropertyLook look;
    look.when = PropertyCondition::parse(when);
    std::istringstream iss(value);
    std::string token;
    while (iss >> token) {
        if (token == "lock-uv") {
            look.lockUV = true;
        } else if (token.rfind("x=", 0) == 0) {
            look.x = std::stoi(token.substr(2));
        } else if (token.rfind("y=", 0) == 0) {
            look.y = std::stoi(token.substr(2));
        } else if (look.model.empty()) {
            look.model = token;
        }
    }
    return look;
}

void BlockModel::clearGeometry() {
    geometry_ = BlockGeometry{};
    collisionResolved_ = false;
    hitResolved_ = false;
}

BlockModel& BlockModel::setWalkSpeed(float multiplier) {
    walkSpeed_ = multiplier;
    return *this;
}

BlockModel& BlockModel::setConnects(std::string group) {
    connects_ = std::move(group);
    return *this;
}

BlockModel& BlockModel::setOpaque(bool opaque) {
    hasOpaqueOverride_ = true;
    opaque_ = opaque;
    return *this;
}

BlockModel& BlockModel::setBlocksSkyLight(bool blocks) {
    hasBlocksSkyLightOverride_ = true;
    blocksSkyLight_ = blocks;
    return *this;
}

BlockModel& BlockModel::setSounds(const std::string& sounds) {
    sounds_ = sounds;
    return *this;
}

BlockModel& BlockModel::setLightEmission(uint8_t level) {
    lightEmission_ = std::min(level, uint8_t(15));
    return *this;
}

BlockModel& BlockModel::setLightAttenuation(uint8_t level) {
    lightAttenuation_ = std::min(level, uint8_t(15));
    return *this;
}

BlockModel& BlockModel::setScript(const std::string& script) {
    script_ = script;
    return *this;
}

BlockModel& BlockModel::setLoot(const std::string& loot) {
    loot_ = loot;
    return *this;
}

BlockModel& BlockModel::setParticle(const std::string& particle) {
    particle_ = particle;
    return *this;
}

const CollisionShape& BlockModel::resolvedCollision() const {
    if (!collisionResolved_) {
        if (hasExplicitCollision_) {
            resolvedCollision_ = collision_;
        } else if (!geometry_.isEmpty()) {
            resolvedCollision_ = geometry_.computeCollisionFromFaces();
        } else {
            resolvedCollision_ = CollisionShape::FULL_BLOCK;
        }
        collisionResolved_ = true;
    }
    return resolvedCollision_;
}

const CollisionShape& BlockModel::resolvedHit() const {
    if (!hitResolved_) {
        if (hasExplicitHit_) {
            resolvedHit_ = hit_;
        } else {
            // Fall back to collision
            resolvedHit_ = resolvedCollision();
        }
        hitResolved_ = true;
    }
    return resolvedHit_;
}

std::vector<uint8_t> BlockModel::allowedRotations() const {
    if (rotationSet_ == RotationSet::Custom) {
        return customRotations_;
    }
    return getRotationIndices(rotationSet_);
}

bool BlockModel::isRotationAllowed(uint8_t rotationIndex) const {
    auto allowed = allowedRotations();
    return std::find(allowed.begin(), allowed.end(), rotationIndex) != allowed.end();
}

// ============================================================================
// Face name utilities
// ============================================================================

int parseFaceName(const std::string& name) {
    std::string lower = toLower(name);

    // Check alias table
    auto it = faceAliases.find(lower);
    if (it != faceAliases.end()) {
        return it->second;
    }

    // Check if it's a numeric index
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
        try {
            return std::stoi(name);
        } catch (...) {
            // Not a valid number
        }
    }

    // Unknown custom name - return -1 to signal caller should assign new index
    return -1;
}

namespace {

constexpr std::string_view CONNECT_PREFIX = "connect_";
constexpr std::string_view OPEN_PREFIX = "open_";

}  // namespace

bool isOpenConnection(const std::string& name) {
    return name.rfind(OPEN_PREFIX, 0) == 0 && parseConnection(name) >= 0;
}

int parseConnection(const std::string& name) {
    static constexpr std::array<std::string_view, 10> DIRECTIONS = {"n", "ne", "e", "se", "s", "sw",
                                                                    "w", "nw", "up", "down"};
    size_t start = 0;
    if (name.rfind(CONNECT_PREFIX, 0) == 0) {
        start = CONNECT_PREFIX.size();
    } else if (name.rfind(OPEN_PREFIX, 0) == 0) {
        start = OPEN_PREFIX.size();
    } else {
        return -1;
    }
    const size_t end = name.find('_', start);
    if (end == std::string::npos) return -1;
    const std::string_view direction = std::string_view(name).substr(start, end - start);
    for (size_t i = 0; i < DIRECTIONS.size(); ++i) {
        if (DIRECTIONS[i] == direction) return static_cast<int>(i);
    }
    return -1;
}

bool isStandardFaceName(const std::string& name) {
    int idx = parseFaceName(name);
    return idx >= 0 && idx < 6;
}

std::string faceName(int index) {
    switch (index) {
        case 0: return "west";
        case 1: return "east";
        case 2: return "bottom";
        case 3: return "top";
        case 4: return "north";
        case 5: return "south";
        default: return std::to_string(index);
    }
}

}  // namespace finevox

#include "finevox/core/block_model.hpp"
#include <algorithm>
#include <cctype>

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

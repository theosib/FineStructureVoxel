#pragma once

/**
 * @file block_model.hpp
 * @brief Data structures for non-cube block geometry
 *
 * Design: [19-block-models.md]
 *
 * BlockModel represents a complete block definition including:
 * - Render geometry (faces with vertices and UVs)
 * - Collision shape (AABBs for physics)
 * - Hit shape (AABBs for raycasting)
 * - Rotation constraints
 * - Properties (hardness, sounds, etc.)
 */

#include "finevox/core/physics.hpp"
#include "finevox/core/position.hpp"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace finevox {

// ============================================================================
// ModelVertex - Single vertex with position and UV
// ============================================================================

struct ModelVertex {
    glm::vec3 position{0.0f};  // In [0,1] local block space
    glm::vec2 uv{0.0f};        // Texture coordinates

    ModelVertex() = default;
    ModelVertex(glm::vec3 pos, glm::vec2 texCoord)
        : position(pos), uv(texCoord) {}
    ModelVertex(float x, float y, float z, float u, float v)
        : position(x, y, z), uv(u, v) {}
};

// ============================================================================
// FaceGeometry - Per-face vertex data
// ============================================================================

/**
 * @brief Geometry for a single face of a block model
 *
 * Faces can have 3-6 vertices (triangles, quads, pentagons, hexagons).
 * Standard faces (0-5) correspond to cube directions.
 * Extra faces (6+) are custom geometry like stair steps.
 */
struct FaceGeometry {
    std::vector<ModelVertex> vertices;  // 3-6 vertices in CCW order
    std::string name;                    // Face name (e.g., "top", "step_top")
    int faceIndex = -1;                  // 0-5 for standard, 6+ for extra, -1 if unset
    bool isSolid = false;                // Does this face fully occlude neighbor?
    int connection = -1;                 // Direction it reaches, 0-7 clockwise from north, 8 up,
                                         // 9 down, or -1
    bool whenOpen = false;               // Shown when nothing joins there, instead of when it does

    /// What the face is drawn with: a texture under <root>/textures, or `#name` for one the
    /// model fills in with `texture:name:`. Empty draws the block's texture for the face it
    /// lies on, or its top texture if it's an extra face.
    std::string textureRef;
    std::string texture;                 // textureRef with any `#name` resolved by the model
    glm::vec4 textureUV{0.0f};           // Where `texture` landed in the atlas (minU minV maxU maxV)
    bool hasTextureUV = false;           // Set once a renderer has placed `texture`
    int tintSlot = -1;                  // Coloured by the block's tint (`tint=0`), or -1

    FaceGeometry() = default;

    /// Compute AABB from vertices (for collision fallback)
    [[nodiscard]] AABB computeBounds() const;

    /// Get the standard Face enum if this is a standard face (0-5)
    [[nodiscard]] std::optional<Face> standardFace() const;

    /// Check if this is a standard face (0-5)
    [[nodiscard]] bool isStandardFace() const { return faceIndex >= 0 && faceIndex < 6; }

    /// Check if this face is valid (has at least 3 vertices)
    [[nodiscard]] bool isValid() const { return vertices.size() >= 3; }
};

// ============================================================================
// BlockGeometry - Collection of faces for rendering
// ============================================================================

/**
 * @brief Complete render geometry for a block model
 *
 * Contains all faces (standard cube faces + extra faces) with their
 * vertices and UVs. Provides lookups by face index or name.
 */
class BlockGeometry {
public:
    BlockGeometry() = default;

    /// Add a face to the geometry
    void addFace(FaceGeometry face);

    /// Get all faces
    [[nodiscard]] const std::vector<FaceGeometry>& faces() const { return faces_; }

    /// For filling in what a face is drawn with once textures are placed. Positions and
    /// names must not change through this: the lookup tables depend on them.
    [[nodiscard]] std::vector<FaceGeometry>& mutableFaces() { return faces_; }

    /// Get face by name (returns nullptr if not found)
    [[nodiscard]] const FaceGeometry* getFace(const std::string& name) const;

    /// Get face by index (returns nullptr if not found)
    [[nodiscard]] const FaceGeometry* getFace(int index) const;

    /// Get standard face (0-5) if present
    [[nodiscard]] const FaceGeometry* getStandardFace(Face face) const;

    /// Check if geometry has any faces
    [[nodiscard]] bool isEmpty() const { return faces_.empty(); }

    /// Get overall bounding box of all faces
    [[nodiscard]] AABB bounds() const { return bounds_; }

    /// Get solid faces as bitmask (bit N set if face N is solid)
    [[nodiscard]] uint8_t solidFacesMask() const;

    /// Has faces that only show when joined to a neighbour, so meshing needs to look around
    [[nodiscard]] bool hasConnections() const { return hasConnections_; }

    /// Compute collision shape from solid faces
    /// Each solid face contributes an AABB covering its extent
    [[nodiscard]] CollisionShape computeCollisionFromFaces() const;

    /// Get the next available face index for custom faces
    [[nodiscard]] int nextCustomFaceIndex() const { return nextCustomIndex_; }

private:
    std::vector<FaceGeometry> faces_;
    std::unordered_map<std::string, size_t> facesByName_;
    std::unordered_map<int, size_t> facesByIndex_;
    AABB bounds_;
    int nextCustomIndex_ = 6;  // Next index for custom faces
    bool hasConnections_ = false;

    void updateBounds(const FaceGeometry& face);
};

// ============================================================================
// RotationSet - Predefined rotation constraints
// ============================================================================

/**
 * @brief Predefined sets of allowed rotations
 *
 * Not all 24 rotations make sense for every block:
 * - Slabs: top/bottom only (2 states)
 * - Slope blocks: horizontal + inverted (8 states)
 * - Directional blocks: horizontal only (4 states)
 */
enum class RotationSet {
    None,            // 1 orientation (identity only)
    Vertical,        // 2 orientations (top/bottom for slabs)
    Horizontal,      // 4 orientations (Y-axis rotations)
    HorizontalFlip,  // 8 orientations (horizontal + upside-down)
    All,             // All 24 orientations
    Custom           // Explicit list of allowed indices
};

/// Get the rotation indices for a predefined set
[[nodiscard]] std::vector<uint8_t> getRotationIndices(RotationSet set);

/// Parse rotation set from string (e.g., "horizontal", "all", "none")
[[nodiscard]] RotationSet parseRotationSet(const std::string& str);

// ============================================================================
// Block states - a block whose look and behaviour vary with a few properties
// ============================================================================

/// A property a block can be in: `state:half: bottom top`. The first value is the default.
struct BlockProperty {
    std::string name;
    std::vector<std::string> values;
};

/// Which states a rule applies to: for each property it names, the values that match. A
/// property it doesn't name matches anything, and so does an empty condition.
struct PropertyCondition {
    std::vector<std::pair<std::string, std::vector<std::string>>> terms;

    /// Parse `half=top,facing=east|west`, or `*` / empty for every state
    static PropertyCondition parse(const std::string& text);
    [[nodiscard]] bool matches(const std::vector<std::string>& propertyNames,
                               const std::vector<std::string>& values) const;
};

/// A look for the states a condition picks: another .model drawn turned whole quarter turns.
/// `x` turns first, about the X axis, then `y` about the vertical; each is clockwise looking
/// from the positive end of its axis, so `y=90` turns a north face to the east.
/// `lock-uv` keeps textures aligned with the world instead of turning with the faces.
struct PropertyLook {
    PropertyCondition when;
    std::string model;  // .model path, resolved like `include:`
    int x = 0;
    int y = 0;
    bool lockUV = false;

    /// Parse `models/stairs_top y=90 lock-uv` (the part after `variant:<when>:`)
    static PropertyLook parse(const std::string& when, const std::string& value);
};

// ============================================================================
// BlockModel - Complete block definition
// ============================================================================

/**
 * @brief Complete model for a block type
 *
 * Includes render geometry, collision/hit shapes, rotation constraints,
 * and various properties. Implements the fallback chain:
 *   hit → collision → geometry_faces → full_block
 */
class BlockModel {
public:
    BlockModel() = default;

    // ========================================================================
    // Builder-style setters
    // ========================================================================

    /// Set the render geometry
    BlockModel& setGeometry(BlockGeometry geometry);

    /// What the block is drawn as where its detail is too small to see (see DetailRange)
    BlockModel& setFarGeometry(BlockGeometry geometry);

    /// Set explicit collision shape
    BlockModel& setCollision(CollisionShape shape);

    /// Set explicit hit shape
    BlockModel& setHit(CollisionShape shape);

    /// Set rotation constraint
    BlockModel& setRotations(RotationSet set);

    /// Set custom rotation indices
    BlockModel& setRotations(std::vector<uint8_t> indices);

    /// Set hardness (mining time factor)
    BlockModel& setHardness(float hardness);

    /// Set texture name
    BlockModel& setTexture(const std::string& texture);

    /// Per-face texture overrides (empty = use texture()). Cube blocks only.
    BlockModel& setFaceTextures(const std::string& top, const std::string& bottom,
                                const std::string& side);

    /// One cube face's texture, overriding the top/bottom/side ones. A name may end in
    /// `@90`, `@180` or `@270`: the texture turned clockwise that far.
    BlockModel& setFaceTexture(Face face, const std::string& texture);

    /// A texture a geometry face can name as `#name`
    BlockModel& setTextureVariable(const std::string& name, const std::string& texture);

    /// Tint: the colour tinted faces are multiplied by. `source` names a colour each biome
    /// gives (`tint:<source>:` in a .biome), blended between neighbours; `color` is used where
    /// a biome doesn't say, or always without a source. A cube face is tinted with
    /// setFaceTint, a geometry face with `tint=0` after its texture.
    BlockModel& setTintSource(const std::string& source);
    BlockModel& setTintColor(glm::vec3 color);
    BlockModel& setFaceTint(Face face, int tint);
    [[nodiscard]] const std::string& tintSource() const { return tintSource_; }
    [[nodiscard]] std::optional<glm::vec3> tintColor() const { return tintColor_; }
    [[nodiscard]] int faceTint(Face face) const { return faceTint_[static_cast<size_t>(face)]; }

    /// Movement speed multiplier for entities walking on this block (1 = normal)
    BlockModel& setWalkSpeed(float multiplier);

    /// Explicit opacity override; unset keeps the BlockType default (opaque).
    BlockModel& setOpaque(bool opaque);

    /// Whether sky light stops here, for a block that isn't opaque but shades what's under it, as
    /// leaves do. Unset, a block stops it if it's opaque.
    BlockModel& setBlocksSkyLight(bool blocks);

    /// Blocks naming the same group join up, showing their connect_* faces
    BlockModel& setConnects(std::string group);

    /// Set sound set name
    BlockModel& setSounds(const std::string& sounds);

    /// Set light emission level (0-15)
    BlockModel& setLightEmission(uint8_t level);

    /// Set light attenuation (0 = transparent to light, higher = blocks more light)
    BlockModel& setLightAttenuation(uint8_t level);

    /// Set script path (for scripted block handlers)
    BlockModel& setScript(const std::string& script);

    /// Set loot table name (references a .loot file)
    BlockModel& setLoot(const std::string& loot);

    /// Set particle emitter preset name (references a .emitter file).
    /// When set, triggers like block-break route through this preset instead
    /// of the engine's default material-tinted burst.
    BlockModel& setParticle(const std::string& particle);

    // ========================================================================
    // Accessors
    // ========================================================================

    /// Get render geometry
    [[nodiscard]] const BlockGeometry& geometry() const { return geometry_; }
    [[nodiscard]] const BlockGeometry& farGeometry() const { return farGeometry_; }
    [[nodiscard]] bool hasFarGeometry() const { return !farGeometry_.isEmpty(); }

    /// Get resolved collision shape (with fallback chain)
    [[nodiscard]] const CollisionShape& resolvedCollision() const;

    /// Get resolved hit shape (with fallback chain)
    [[nodiscard]] const CollisionShape& resolvedHit() const;

    /// Check if collision was explicitly set
    [[nodiscard]] bool hasExplicitCollision() const { return hasExplicitCollision_; }

    /// Check if hit was explicitly set
    [[nodiscard]] bool hasExplicitHit() const { return hasExplicitHit_; }

    /// Get rotation set
    [[nodiscard]] RotationSet rotationSet() const { return rotationSet_; }

    /// Get allowed rotation indices
    [[nodiscard]] std::vector<uint8_t> allowedRotations() const;

    /// Check if a rotation index is allowed
    [[nodiscard]] bool isRotationAllowed(uint8_t rotationIndex) const;

    /// Get hardness
    [[nodiscard]] float hardness() const { return hardness_; }

    /// Get texture name
    [[nodiscard]] const std::string& texture() const { return texture_; }

    [[nodiscard]] const std::string& textureTop() const { return textureTop_; }
    [[nodiscard]] const std::string& textureBottom() const { return textureBottom_; }
    [[nodiscard]] const std::string& textureSide() const { return textureSide_; }

    /// The texture a cube face is drawn with, after every override: the face's own, then
    /// top/bottom/side, then texture(). Empty if none is set.
    [[nodiscard]] std::string cubeFaceTexture(Face face) const;
    [[nodiscard]] const std::array<std::string, 6>& faceTextureOverrides() const { return faceTextures_; }
    [[nodiscard]] const std::unordered_map<std::string, std::string>& textureVariables() const {
        return textureVariables_;
    }

    /// Fill in each geometry face's `texture` from its `textureRef`, through the variables.
    /// Call again after variables change: an included model's faces resolve against the
    /// model that includes it.
    void resolveFaceTextures();

    /// Replace geometry and cube textures with those of another model: for a block whose
    /// look comes from elsewhere (a state's variant) and whose behaviour stays its own.
    void takeAppearance(const BlockModel& look);

    /// Drop the geometry, for a model found to be a plain cube that draws as one
    void clearGeometry();

    /// Block states (see BlockProperty). A model with properties stands for one block
    /// type per combination of their values. `variant:` rules pick each state's look, the first
    /// matching one winning; `part:` rules draw every matching look together.
    BlockModel& addProperty(BlockProperty property);
    BlockModel& addVariant(PropertyLook look);
    BlockModel& addPart(PropertyLook look);

    /// Behaviour that differs by state: `set:lit=true: light-emission=13`. Every matching
    /// line applies, in order. Keys: light-emission, light-attenuation, hardness, opaque,
    /// collision (none or full), walk-speed.
    struct StateSetting {
        PropertyCondition when;
        std::vector<std::pair<std::string, std::string>> values;
    };
    BlockModel& addStateSetting(StateSetting setting);
    [[nodiscard]] const std::vector<StateSetting>& stateSettings() const { return stateSettings_; }
    /// Apply one setting's values to this model
    void applySetting(const StateSetting& setting);
    [[nodiscard]] const std::vector<BlockProperty>& properties() const { return properties_; }
    [[nodiscard]] const std::vector<PropertyLook>& variants() const { return variants_; }
    [[nodiscard]] const std::vector<PropertyLook>& parts() const { return parts_; }
    [[nodiscard]] bool hasProperties() const { return !properties_.empty(); }

    [[nodiscard]] float walkSpeed() const { return walkSpeed_; }
    [[nodiscard]] bool hasOpaqueOverride() const { return hasOpaqueOverride_; }
    [[nodiscard]] const std::string& connects() const { return connects_; }
    [[nodiscard]] bool opaque() const { return opaque_; }
    [[nodiscard]] bool hasBlocksSkyLightOverride() const { return hasBlocksSkyLightOverride_; }
    [[nodiscard]] bool blocksSkyLight() const { return blocksSkyLight_; }

    /// Get sound set name
    [[nodiscard]] const std::string& sounds() const { return sounds_; }

    /// Get light emission level
    [[nodiscard]] uint8_t lightEmission() const { return lightEmission_; }

    /// Get light attenuation level
    [[nodiscard]] uint8_t lightAttenuation() const { return lightAttenuation_; }

    /// Get script path (empty if no script)
    [[nodiscard]] const std::string& script() const { return script_; }

    /// Check if this model has a script handler
    [[nodiscard]] bool hasScript() const { return !script_.empty(); }

    /// Get loot table name (empty if no loot table)
    [[nodiscard]] const std::string& loot() const { return loot_; }

    /// Check if this model has a loot table
    [[nodiscard]] bool hasLoot() const { return !loot_.empty(); }

    /// Get particle emitter preset name (empty if none)
    [[nodiscard]] const std::string& particle() const { return particle_; }

    /// Check if this model overrides the default break emitter
    [[nodiscard]] bool hasParticle() const { return !particle_.empty(); }

    /// Check if this model has custom geometry (non-cube)
    [[nodiscard]] bool hasCustomGeometry() const { return !geometry_.isEmpty(); }

private:
    BlockGeometry geometry_;
    BlockGeometry farGeometry_;
    CollisionShape collision_;
    CollisionShape hit_;
    bool hasExplicitCollision_ = false;
    bool hasExplicitHit_ = false;
    RotationSet rotationSet_ = RotationSet::None;
    std::vector<uint8_t> customRotations_;
    float hardness_ = 1.0f;
    std::string texture_;
    std::string textureTop_;
    std::string textureBottom_;
    std::string textureSide_;
    std::array<std::string, 6> faceTextures_;
    std::array<int, 6> faceTint_{-1, -1, -1, -1, -1, -1};
    std::string tintSource_;
    std::optional<glm::vec3> tintColor_;
    std::vector<BlockProperty> properties_;
    std::vector<PropertyLook> variants_;
    std::vector<PropertyLook> parts_;
    std::vector<StateSetting> stateSettings_;
    std::unordered_map<std::string, std::string> textureVariables_;
    float walkSpeed_ = 1.0f;
    bool hasOpaqueOverride_ = false;
    bool opaque_ = true;
    bool hasBlocksSkyLightOverride_ = false;
    bool blocksSkyLight_ = true;
    std::string connects_;
    std::string sounds_;
    std::string script_;
    std::string loot_;
    std::string particle_;
    uint8_t lightEmission_ = 0;
    uint8_t lightAttenuation_ = 15;  // Default: blocks all light

    // Cached resolved shapes (computed on first access)
    mutable CollisionShape resolvedCollision_;
    mutable CollisionShape resolvedHit_;
    mutable bool collisionResolved_ = false;
    mutable bool hitResolved_ = false;
};

// ============================================================================
// Face name utilities
// ============================================================================

/**
 * @brief Parse a face name to its index
 *
 * Supports multiple aliases:
 * - Standard faces: negx/west/w/-x (0), posx/east/e/+x (1), etc.
 * - Numeric: "6", "7", etc.
 * - Custom names: Returns -1 (caller assigns next available index)
 *
 * @param name Face name string
 * @return Face index (0-5 for standard, -1 for unknown custom name)
 */
[[nodiscard]] int parseFaceName(const std::string& name);

/**
 * @brief Check if a name is a standard face alias
 */
[[nodiscard]] bool isStandardFaceName(const std::string& name);

/// The direction in a face name of the form connect_<dir>_<anything> or open_<dir>_<anything>,
/// where dir is one of n ne e se s sw w nw (0-7 clockwise from north, -Z), up (8) or down (9).
/// -1 for any other name.
[[nodiscard]] int parseConnection(const std::string& name);

/// Whether a face name is open_<dir>_<anything>: shown when nothing joins in that direction
[[nodiscard]] bool isOpenConnection(const std::string& name);

/**
 * @brief Get the canonical name for a face index
 */
[[nodiscard]] std::string faceName(int index);

}  // namespace finevox

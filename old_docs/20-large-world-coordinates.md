# 20. Large World Coordinate Support

[Back to Index](INDEX.md) | [Previous: Block Models](19-block-models.md)

---

## 20.1 The Problem

Standard 32-bit floating point (`float`) has approximately 7 significant decimal digits. At world coordinates beyond ~10,000 blocks, precision loss causes visible artifacts:

- **Vertex jitter**: Objects shimmer or vibrate as the camera moves
- **Z-fighting**: Depth buffer precision degrades, causing flickering surfaces
- **Physics glitches**: Collision detection becomes unreliable

These issues compound at larger distances. At 1,000,000 blocks from origin, float32 can only represent positions to within ~0.1 block units.

---

## 20.2 The Solution: View-Relative Rendering

The key insight is that **rendering only needs relative positions**. We don't need to know a vertex is at world position (1,000,000, 64, 1,000,000)—we only need to know it's 50 blocks in front of the camera.

### Core Principle

1. **Store world positions in double precision (64-bit)** on the CPU
2. **Subtract camera position using doubles** to get view-relative offsets
3. **Convert the small result to float** for GPU consumption

The subtraction produces small values (within view distance), which float32 handles perfectly.

### Mathematical Basis

```
worldPos = (1,000,000.0, 64.0, 1,000,000.0)  // Large, needs double
cameraPos = (1,000,050.0, 64.0, 1,000,000.0) // Large, needs double

viewRelativePos = worldPos - cameraPos       // Subtracted as doubles
                = (-50.0, 0.0, 0.0)          // Small! Safe for float32
```

---

## 20.3 Implementation in FineVox

### Camera Setup (FineVK)

FineVK's `Camera` class supports double-precision positions:

```cpp
finevk::Camera camera;
camera.setPerspective(70.0f, aspectRatio, 0.1f, 500.0f);

// Use double-precision position for large worlds
camera.moveTo(glm::dvec3(1000000.0, 64.0, 1000000.0));
camera.setOrientation(forward, up);
camera.updateState();

// CameraState provides:
// - viewRelative: View matrix with camera at origin (rotation only)
// - viewRelativeFrustumPlanes: Frustum planes in view-relative space
```

### WorldRenderer Integration

```cpp
void WorldRenderer::updateCamera(
    const finevk::CameraState& cameraState,
    const glm::dvec3& highPrecisionPos)
{
    cameraState_ = cameraState;
    highPrecisionCameraPos_ = highPrecisionPos;

    // Use FineVK's view-relative matrix (camera at origin)
    finevk::CameraUniform uniform{};
    uniform.view = cameraState_.viewRelative;  // Rotation only, no translation
    uniform.projection = cameraState_.projection;
    uniform.viewProjection = cameraState_.projection * cameraState_.viewRelative;

    cameraUniform_->update(currentFrame, uniform);
}
```

### Per-Chunk Offset Calculation

Each subchunk's position is converted to view-relative coordinates using double precision:

```cpp
glm::vec3 WorldRenderer::calculateViewRelativeOffset(ChunkPos pos) const {
    // Calculate subchunk origin using double precision
    glm::dvec3 worldPosD(
        static_cast<double>(pos.x) * 16.0,
        static_cast<double>(pos.y) * 16.0,
        static_cast<double>(pos.z) * 16.0
    );

    // Subtract camera position (both doubles)
    glm::dvec3 offsetD = worldPosD - highPrecisionCameraPos_;

    // Result is small, safe to convert to float
    return glm::vec3(offsetD);
}
```

### Frustum Culling

Frustum culling also uses view-relative coordinates:

```cpp
bool WorldRenderer::isInFrustum(ChunkPos pos) const {
    // Create AABB in world space (double precision)
    glm::dvec3 worldMinD(
        static_cast<double>(pos.x) * 16.0,
        static_cast<double>(pos.y) * 16.0,
        static_cast<double>(pos.z) * 16.0
    );
    glm::dvec3 worldMaxD = worldMinD + glm::dvec3(16.0);

    // Convert to view-relative (small values, safe for float)
    glm::vec3 minViewRel = glm::vec3(worldMinD - highPrecisionCameraPos_);
    glm::vec3 maxViewRel = glm::vec3(worldMaxD - highPrecisionCameraPos_);

    finevk::AABB aabb = finevk::AABB::fromMinMax(minViewRel, maxViewRel);

    // Test against view-relative frustum planes
    return aabb.intersectsFrustum(cameraState_.viewRelativeFrustumPlanes);
}
```

### Vertex Shader

The shader receives pre-computed view-relative offsets via push constants:

```glsl
// Per-chunk push constants
layout(push_constant) uniform PushConstants {
    vec3 chunkOffset;  // View-relative: (chunkWorldOrigin - cameraPos)
    float padding;
} chunk;

void main() {
    // Vertex position within subchunk (0-16 range)
    // chunkOffset is view-relative (small, computed with doubles on CPU)
    vec3 viewRelativePos = chunk.chunkOffset + inPosition;

    // View matrix is rotation-only (camera at origin)
    gl_Position = camera.projection * camera.view * vec4(viewRelativePos, 1.0);
}
```

---

## 20.4 FineVK Support

FineVK provides the following for large world coordinate support:

### Camera Class

| Method | Description |
|--------|-------------|
| `moveTo(glm::dvec3)` | Set position using double precision |
| `move(glm::dvec3)` | Move by delta using double precision |
| `positionD()` | Get position as `glm::dvec3` |
| `hasHighPrecisionPosition()` | Check if using double mode |

### CameraState Struct

| Field | Description |
|-------|-------------|
| `viewRelative` | View matrix with camera at origin (rotation only) |
| `viewRelativeFrustumPlanes` | Frustum planes for view-relative AABB testing |
| `frustumPlanes` | Standard world-space frustum planes |

### Automatic Mode Switching

When you call `camera.moveTo(glm::dvec3(...))`, FineVK automatically switches to double-precision mode. Subsequent float-based calls continue to work but accumulate into the double-precision position.

---

## 20.5 Coordinate Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         CPU (Double Precision)                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Camera Position          Chunk World Position                          │
│  glm::dvec3               glm::dvec3                                    │
│  (1000050, 64, 1000000)   (1000000, 64, 1000000)                        │
│         │                        │                                      │
│         └───────────┬────────────┘                                      │
│                     │                                                   │
│                     ▼                                                   │
│            Subtract (double precision)                                  │
│            chunkOffset = chunkPos - cameraPos                          │
│                     │                                                   │
│                     ▼                                                   │
│            glm::dvec3 (-50, 0, 0)   ← Small value!                     │
│                     │                                                   │
│                     ▼                                                   │
│            Convert to float (safe)                                      │
│            glm::vec3 (-50.0f, 0.0f, 0.0f)                              │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
                              │
                              ▼ Push Constants
┌─────────────────────────────────────────────────────────────────────────┐
│                         GPU (Float Precision)                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Push Constants              Vertex Input                               │
│  chunkOffset: (-50, 0, 0)    inPosition: (8, 4, 12)                    │
│         │                           │                                   │
│         └───────────┬───────────────┘                                   │
│                     │                                                   │
│                     ▼                                                   │
│            viewRelativePos = chunkOffset + inPosition                  │
│                           = (-42, 4, 12)                               │
│                     │                                                   │
│                     ▼                                                   │
│            View Matrix (rotation only, camera at origin)               │
│            gl_Position = projection * view * vec4(viewRelativePos, 1)  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 20.6 Testing Large Coordinates

The render demo can test large coordinate support:

```cpp
// Position camera at 1 million blocks from origin
camera.moveTo(glm::dvec3(1000000.0, 100.0, 1000000.0));

// Place blocks near the camera
world.setBlock({1000000, 64, 1000000}, stoneId);
world.setBlock({1000001, 64, 1000000}, stoneId);
// ... etc

// Rendering should be stable with no jitter
```

**What to look for:**
- Vertices should remain stable as camera moves
- No visible jitter or shimmering
- Depth testing should work correctly (no z-fighting)
- Frustum culling should work correctly

---

## 20.7 Limitations and Considerations

### World Size Limits

With `int32_t` chunk coordinates and 16-block subchunks:
- Maximum coordinate: ±2^31 × 16 = ±34 billion blocks
- This exceeds practical memory and generation limits

### Physics

Physics calculations should also use double precision for positions when operating at large coordinates. The current physics implementation uses float32 but operates on local collision shapes, which keeps values small.

### Entity Positions

Entity positions should be stored as `glm::dvec3` when large world support is needed. The `Entity` class can provide both:
- `positionD()` for double-precision world position
- `position()` for float (truncated, for local calculations)

### Network Synchronization

For multiplayer, consider:
- Transmit positions as doubles or as chunk-relative offsets
- Use delta compression for position updates
- Define a "local origin" per player for relative calculations

---

## 20.8 Summary

| Component | Storage | Calculation |
|-----------|---------|-------------|
| Camera position | `glm::dvec3` | Double precision |
| Chunk world position | Derived from `ChunkPos` (int32) | Double precision |
| View-relative offset | `glm::vec3` (push constant) | Result of double subtraction |
| Vertex positions | `glm::vec3` (in mesh) | Local to subchunk (0-16) |
| View matrix | `glm::mat4` | Rotation only, camera at origin |
| Frustum planes | `std::array<glm::vec4, 6>` | View-relative space |

The key pattern: **Subtract large values using doubles, then work with small floats on GPU.**

---

[Back to Index](INDEX.md)

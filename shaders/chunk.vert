#version 450

// ============================================================================
// Chunk Vertex Shader
//
// Transforms chunk vertices using view-relative coordinates to avoid
// floating-point precision issues at large world positions.
// ============================================================================

// Vertex inputs (matches ChunkVertex)
layout(location = 0) in vec3 inPosition;   // Local position within subchunk (0-16)
layout(location = 1) in vec3 inNormal;     // Face normal
layout(location = 2) in vec2 inTexCoord;   // Texture coordinates (may extend beyond 0-1 for tiling)
layout(location = 3) in vec4 inTileBounds; // Texture tile bounds (minU, minV, maxU, maxV)
layout(location = 4) in float inAO;        // Ambient occlusion (0-1)
layout(location = 5) in float inLight;     // Smooth lighting (0-1, from block/sky light)

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;    // World position (for lighting)
layout(location = 1) out vec3 fragNormal;      // Normal (for lighting)
layout(location = 2) out vec2 fragTexCoord;    // Texture coordinates (may extend beyond 0-1)
layout(location = 3) out vec4 fragTileBounds;  // Texture tile bounds for atlas tiling
layout(location = 4) out float fragAO;         // Ambient occlusion
layout(location = 5) out vec4 fragClipPos;     // DEBUG: clip space position
layout(location = 6) out float fragDistance;   // Distance from camera (for fog)
layout(location = 7) out float fragLight;      // Smooth lighting value

// Camera uniform (binding 0)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
    float padding[3];
} camera;

// Per-chunk push constants
layout(push_constant) uniform PushConstants {
    vec3 chunkOffset;  // View-relative position of subchunk origin
    float fogStart;    // Fog start distance
    vec3 fogColor;     // Fog color
    float fogEnd;      // Fog end distance
} chunk;

void main() {
    // View-relative rendering for precision at large coordinates:
    // chunkOffset = (chunkWorldOrigin - cameraPos) computed on CPU with doubles/int64
    // viewRelativePos = chunkOffset + localPos = position relative to camera (small values)
    vec3 viewRelativePos = chunk.chunkOffset + inPosition;

    // Transform directly using view-relative position
    // The view matrix should be centered at origin (camera at 0,0,0)
    // This keeps all math in the small-number range for float32 precision
    gl_Position = camera.projection * camera.view * vec4(viewRelativePos, 1.0);

    // For lighting, we can use view-relative position (lighting is relative anyway)
    // Or reconstruct approximate world pos if needed for effects
    fragWorldPos = viewRelativePos + camera.cameraPos;
    fragNormal = inNormal;
    fragTexCoord = inTexCoord;
    fragTileBounds = inTileBounds;
    fragAO = inAO;
    fragClipPos = gl_Position;
    fragDistance = length(viewRelativePos);  // Distance from camera for fog
    fragLight = inLight;  // Pass through smooth lighting
}

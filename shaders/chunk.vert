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
layout(location = 2) in vec2 inTexCoord;   // Texture coordinates
layout(location = 3) in float inAO;        // Ambient occlusion (0-1)

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;    // World position (for lighting)
layout(location = 1) out vec3 fragNormal;      // Normal (for lighting)
layout(location = 2) out vec2 fragTexCoord;    // Texture coordinates
layout(location = 3) out float fragAO;         // Ambient occlusion
layout(location = 4) out vec4 fragClipPos;     // DEBUG: clip space position

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
    float padding;
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
    fragAO = inAO;
    fragClipPos = gl_Position;
}

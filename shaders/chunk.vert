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
    // Calculate view-relative world position
    // chunkOffset is already relative to camera position
    vec3 viewRelativePos = chunk.chunkOffset + inPosition;

    // Transform to clip space using view-projection matrix
    // Note: We use view matrix that expects world coordinates, but our viewRelativePos
    // is already camera-relative, so we use a translation-adjusted view matrix
    // The camera uniform's view matrix includes the camera translation,
    // so we can use viewRelativePos directly with just the projection
    vec4 viewPos = camera.view * vec4(viewRelativePos + camera.cameraPos, 1.0);
    gl_Position = camera.projection * viewPos;

    // Pass data to fragment shader
    fragWorldPos = viewRelativePos + camera.cameraPos;
    fragNormal = inNormal;
    fragTexCoord = inTexCoord;
    fragAO = inAO;
}

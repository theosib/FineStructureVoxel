#version 450

// ============================================================================
// Particle Vertex Shader
//
// CPU already oriented each quad (camera-facing / velocity-stretched /
// axis-locked) and produced 6 view-relative vertices per particle.  This
// shader just applies the camera transform — no per-vertex billboarding.
// ============================================================================

// Vertex inputs (matches ParticleVertex)
layout(location = 0) in vec3 inPosition;   // view-relative corner position
layout(location = 1) in vec2 inTexCoord;   // atlas UV
layout(location = 2) in vec4 inTint;       // per-particle RGBA tint

// Outputs to fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out float fragDistance;

// Camera uniform (shared with chunk pipeline)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
    float padding[3];
} camera;

void main() {
    // inPosition is already view-relative (quad corners pre-computed by CPU
    // using view-relative camera basis), so the view matrix is "rotate only"
    // (cameraPos == origin in view-relative space).
    gl_Position = camera.projection * camera.view * vec4(inPosition, 1.0);

    fragTexCoord = inTexCoord;
    fragTint = inTint;
    fragDistance = length(inPosition);
}

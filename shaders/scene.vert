#version 450

// ============================================================================
// Scene Vertex Shader
//
// Entities and world markers. Geometry arrives view-relative, already posed on
// the CPU, so this only applies the camera.
// ============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragDistance;

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
    gl_Position = camera.projection * camera.view * vec4(inPosition, 1.0);
    fragNormal = inNormal;
    fragColor = inColor;
    fragDistance = length(inPosition);
}

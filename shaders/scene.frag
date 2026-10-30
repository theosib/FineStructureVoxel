#version 450

// ============================================================================
// Scene Fragment Shader
//
// A zero normal means unlit: markers keep their colour at night so selection
// rings and build previews stay readable.
// ============================================================================

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragDistance;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec3 chunkOffset;
    float fogStart;
    vec3 fogColor;
    float fogEnd;
    vec3 sunDirection;
    float skyBrightness;
    float ambientLevel;
    float pad0;
    float pad1;
    float pad2;
} scene;

float getFaceShade(vec3 normal) {
    if (normal.y > 0.5) return 1.0;
    if (normal.y < -0.5) return 0.55;
    if (abs(normal.z) > 0.5) return 0.85;
    return 0.7;
}

void main() {
    vec3 color = fragColor.rgb;

    if (dot(fragNormal, fragNormal) > 0.01) {
        vec3 normal = normalize(fragNormal);
        float diffuse = max(dot(normal, scene.sunDirection), 0.0) * 0.5;
        float lighting = (scene.ambientLevel + diffuse) * getFaceShade(normal);
        color *= max(lighting, 0.12);
    }

    if (scene.fogStart < scene.fogEnd) {
        float fog = clamp((fragDistance - scene.fogStart) / (scene.fogEnd - scene.fogStart), 0.0, 1.0);
        color = mix(color, scene.fogColor, fog);
    }

    outColor = vec4(color, fragColor.a);
}

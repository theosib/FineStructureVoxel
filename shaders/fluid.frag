#version 450

// ============================================================================
// Fluid Fragment Shader
//
// Renders fluid surfaces with tint color and alpha blending.
// Uses the same vertex layout as chunk.vert but reinterprets:
//   - tileBounds (location 3) as RGBA tint color
//   - ao (location 4) as alpha value
// No texture atlas sampling — color comes from the tint.
// ============================================================================

// Inputs from vertex shader (same as chunk.vert)
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragTileBounds;   // Repurposed: RGBA tint color
layout(location = 4) in float fragAO;          // Repurposed: alpha value
layout(location = 5) in vec4 fragClipPos;
layout(location = 6) in float fragDistance;
layout(location = 7) in float fragSkyLight;
layout(location = 8) in float fragBlockLight;

// Output color (with alpha for blending)
layout(location = 0) out vec4 outColor;

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

// Block atlas texture (binding 1) — present in descriptor set but unused
layout(set = 0, binding = 1) uniform sampler2D blockAtlas;

// Per-chunk push constants (must match vertex shader)
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
} chunk;

// Lighting constants
const float DIFFUSE = 0.6;

// Calculate fog factor based on distance
float calculateFog(float distance) {
    if (chunk.fogStart >= chunk.fogEnd) {
        return 0.0;
    }
    return clamp((distance - chunk.fogStart) / (chunk.fogEnd - chunk.fogStart), 0.0, 1.0);
}

// Per-face brightness
float getFaceShade(vec3 normal) {
    if (normal.y > 0.5) return 1.0;
    if (normal.y < -0.5) return 0.5;
    if (abs(normal.z) > 0.5) return 0.8;
    return 0.6;
}

void main() {
    // Tint color from vertex data (repurposed tileBounds field)
    vec3 baseColor = fragTileBounds.rgb;
    float alpha = fragAO;  // Repurposed ao field

    // Discard fully transparent pixels
    if (alpha < 0.01) {
        discard;
    }

    // Calculate lighting (same as chunk.frag)
    vec3 normal = normalize(fragNormal);
    float NdotL = max(dot(normal, chunk.sunDirection), 0.0);
    float diffuse = NdotL * DIFFUSE;
    float faceShade = getFaceShade(normal);
    float dirLighting = (chunk.ambientLevel + diffuse) * faceShade;

    // Combine sky and block light
    float skyContrib = fragSkyLight * chunk.skyBrightness;
    float blockContrib = fragBlockLight;
    float smoothLight = max(max(skyContrib, blockContrib), 0.1);

    float lighting = dirLighting * smoothLight;

    // Final color before fog
    vec3 finalColor = baseColor * lighting;

    // Apply distance fog (fade toward fog color)
    float fogFactor = calculateFog(fragDistance);
    finalColor = mix(finalColor, chunk.fogColor, fogFactor);

    // Output with alpha for translucent blending
    outColor = vec4(finalColor, alpha);
}

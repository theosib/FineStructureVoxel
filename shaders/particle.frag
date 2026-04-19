#version 450

// ============================================================================
// Particle Fragment Shader
//
// Samples the particle atlas with bilinear filtering, multiplies by the
// per-particle tint, discards fully-transparent fragments to avoid
// alpha-blending ordering artifacts on hard-edged cutouts.
// ============================================================================

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragTint;
layout(location = 2) in float fragDistance;

layout(location = 0) out vec4 outColor;

// Particle atlas (binding 1 — same slot the chunk pipeline uses for the
// block atlas, but we re-bind with our own descriptor set).
layout(set = 0, binding = 1) uniform sampler2D particleAtlas;

// Push constants kept binary-compatible with the chunk pipeline's push
// constants so the same PipelineLayout could be reused if we ever need to.
// Only fog fields are consumed here.
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
} params;

void main() {
    vec4 sampled = texture(particleAtlas, fragTexCoord);
    vec4 c = sampled * fragTint;

    // Alpha cutoff keeps sharp edges from blending into neighbouring
    // atlas cells.  Fragments below this threshold contribute nothing.
    if (c.a < 0.02) discard;

    // Distance fog fades particles into the fog colour like world geometry.
    if (params.fogEnd > params.fogStart) {
        float fogFactor = clamp(
            (fragDistance - params.fogStart) / (params.fogEnd - params.fogStart),
            0.0, 1.0);
        c.rgb = mix(c.rgb, params.fogColor, fogFactor);
    }

    outColor = c;
}

#version 450
#extension GL_GOOGLE_include_directive : require

// ============================================================================
// Chunk Fragment Shader
//
// Textured rendering with ambient occlusion, directional lighting from
// dynamic sun direction, and separate sky/block light channels for
// day/night cycle support.
// ============================================================================

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;     // Raw texture coordinates (may extend beyond tile bounds)
layout(location = 3) in vec4 fragTileBounds;   // Texture tile bounds (minU, minV, maxU, maxV)
layout(location = 4) in float fragAO;
layout(location = 5) in vec4 fragClipPos;      // DEBUG
layout(location = 6) in float fragDistance;    // Distance from camera (for fog)
layout(location = 7) in float fragSkyLight;    // Sky light (0-1)
layout(location = 8) in float fragBlockLight;  // Block light (0-1)
layout(location = 9) in vec3 fragTint;         // Colour the texture is multiplied by
layout(location = 10) flat in uvec4 fragBlend;  // Neighbours' textures spread over the face (ChunkVertex::blend)
// fragTexCoord again, but read inside the triangle. With MSAA a pixel on a face's edge is shaded at
// its centre, which can be past the edge, and a coordinate a little past 1 wraps round to 0: the
// far side of the face. Blending would draw a neighbour's texture there, in dots along the edge.
layout(location = 11) centroid in vec2 fragCellTexCoord;

// Output color
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

// Block atlas texture (binding 1)
layout(set = 0, binding = 1) uniform sampler2D blockAtlas;

#include "region_tint.glsl"

// Per-chunk push constants (must match vertex shader)
layout(push_constant) uniform PushConstants {
    vec3 chunkOffset;    // View-relative position of subchunk origin
    float fogStart;      // Fog start distance (0 = fog disabled)
    vec3 fogColor;       // Fog color
    float fogEnd;        // Fog end distance
    vec3 sunDirection;   // Directional light vector (normalized)
    float skyBrightness; // Sky light multiplier (0-1)
    float ambientLevel;  // Minimum ambient light
    float pad0;
    float pad1;
    float pad2;
} chunk;

// Lighting constants
const float DIFFUSE = 0.6;  // Diffuse light strength

// Highest mip the atlas's tile borders support
const float MAX_ATLAS_MIP = 2.0;

// Calculate fog factor based on distance
// Returns 0.0 (no fog) to 1.0 (full fog)
float calculateFog(float distance) {
    // Fog disabled if start >= end
    if (chunk.fogStart >= chunk.fogEnd) {
        return 0.0;
    }
    return clamp((distance - chunk.fogStart) / (chunk.fogEnd - chunk.fogStart), 0.0, 1.0);
}

// Per-face brightness
// Top faces are brightest, bottom darkest, sides in between
float getFaceShade(vec3 normal) {
    if (normal.y > 0.5) return 1.0;        // Top: full brightness
    if (normal.y < -0.5) return 0.5;       // Bottom: darkest
    if (abs(normal.z) > 0.5) return 0.8;   // North/South: medium-light
    return 0.6;                             // East/West: medium-dark
}

// Where in its block's face a point is, 0-1 along the texture's U and V. Greedy meshing tiles
// merged faces past 1.0, so wrap there; within a single tile, clamp directly so float
// interpolation rounding at 0.0 or 1.0 never wraps to the opposite edge of a cutout tile.
vec2 cellCoord(vec2 uv, vec4 tileBounds) {
    vec2 t = (uv - tileBounds.xy) / (tileBounds.zw - tileBounds.xy);
    return vec2(
        t.x > 1.0001 ? fract(t.x) : clamp(t.x, 0.0, 1.0),
        t.y > 1.0001 ? fract(t.y) : clamp(t.y, 0.0, 1.0)
    );
}

// Wrap texture coordinates to tile within atlas cell bounds
// This allows greedy meshing to work with texture atlases by tiling
// the texture across merged faces rather than stretching
vec2 wrapTexCoord(vec2 uv, vec4 tileBounds) {
    vec2 tileMin = tileBounds.xy;  // minU, minV
    vec2 tileMax = tileBounds.zw;  // maxU, maxV
    vec2 tileSize = tileMax - tileMin;
    vec2 local = cellCoord(uv, tileBounds);
    vec2 wrapped = local * tileSize + tileMin;

    // Clamp to stay within tile bounds with half-texel margin
    // This prevents bilinear filtering from sampling adjacent atlas cells
    // The atlas has 2-pixel borders, so we have some margin, but clamping
    // ensures we never sample outside our designated content area
    vec2 halfTexel = vec2(0.5) / vec2(textureSize(blockAtlas, 0));
    wrapped = clamp(wrapped, tileMin + halfTexel, tileMax - halfTexel);

    return wrapped;
}

// ----------------------------------------------------------------------------
// Blending: a neighbour's texture spread over this face near their shared edge
// (ChunkVertex::blend). Both sides of an edge work out the same distance to the
// neighbour's cells, and the ragged border comes from noise in world space, so
// the spread runs on unbroken from one face to the next.
// ----------------------------------------------------------------------------

float hash3(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), f.x),
                   mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x),
                   mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// How far a point in the face is from the neighbours in the mask (BLEND_* in mesh.hpp): across to
// an edge, or straight to a corner
float blendDistance(uint mask, vec2 st) {
    float d = 2.0;
    if ((mask & 1u) != 0u) d = min(d, st.x);
    if ((mask & 2u) != 0u) d = min(d, 1.0 - st.x);
    if ((mask & 4u) != 0u) d = min(d, st.y);
    if ((mask & 8u) != 0u) d = min(d, 1.0 - st.y);
    if ((mask & 16u) != 0u) d = min(d, length(st));
    if ((mask & 32u) != 0u) d = min(d, length(vec2(1.0 - st.x, st.y)));
    if ((mask & 64u) != 0u) d = min(d, length(vec2(st.x, 1.0 - st.y)));
    if ((mask & 128u) != 0u) d = min(d, length(1.0 - st));
    return d;
}

vec3 unpackRGB565(uint c) {
    vec3 rgb = vec3(float(c & 31u) / 31.0, float((c >> 5) & 63u) / 63.0, float((c >> 11) & 31u) / 31.0);
    return pow(rgb, vec3(2.2));
}

vec3 applyBlend(vec3 albedo, vec2 st, float mipLevel) {
    vec2 tileSize = fragTileBounds.zw - fragTileBounds.xy;
    vec2 halfTexel = vec2(0.5) / vec2(textureSize(blockAtlas, 0));
    for (int layer = 0; layer < 2; ++layer) {
        uint mask = (fragBlend.z >> (8 * layer)) & 0xFFu;
        float width = float((fragBlend.z >> (16 + 8 * layer)) & 0xFFu) / 255.0;
        if (mask == 0u || width <= 0.0) continue;
        float d = blendDistance(mask, st);
        if (d >= width) continue;

        uint packedMin = layer == 0 ? fragBlend.x : fragBlend.y;
        vec2 tileMin = vec2(float(packedMin & 0xFFFFu), float(packedMin >> 16)) / 65535.0;
        vec2 uv = clamp(tileMin + st * tileSize, tileMin + halfTexel, tileMin + tileSize - halfTexel);
        vec3 color = textureLod(blockAtlas, uv, clamp(mipLevel, 0.0, MAX_ATLAS_MIP)).rgb;

        // Two octaves of noise, a little of the texture's own light and dark so the border
        // follows what's drawn, and an offset per layer so two layers don't share a shape
        vec3 p = fragWorldPos + float(layer) * 17.0;
        float n = 0.65 * valueNoise(p * 3.0) + 0.35 * valueNoise(p * 7.0);
        n = mix(n, dot(color, vec3(0.3, 0.6, 0.1)) * 2.0, 0.25);
        float reach = 1.0 - d / width;
        float cover = smoothstep(0.0, 0.08, reach - 0.9 * n);

        color *= unpackRGB565((fragBlend.w >> (16 * layer)) & 0xFFFFu);
        albedo = mix(albedo, color, cover);
    }
    return albedo;
}

// DEBUG: Uncomment one of these to enable debug visualization
// #define DEBUG_WORLD_POS
// #define DEBUG_LOCAL_POS
// #define DEBUG_CLIP_DEPTH
// #define DEBUG_NORMALS

void main() {
    // Wrap texture coordinates for proper tiling within atlas cell
    vec2 tiledTexCoord = wrapTexCoord(fragTexCoord, fragTileBounds);

    // Sample the block atlas with an explicit mip level.
    // fract() creates a UV discontinuity at wrap points, so the GPU's own derivatives
    // would see a huge jump there and pick the wrong level. Taking derivatives of the
    // unwrapped coordinates avoids that. Sampling only mip 0 instead sparkles badly
    // from a distance. The clamp stops at the level the atlas's 2-pixel tile borders
    // can support before neighbouring tiles bleed in.
    vec2 texelCoord = fragTexCoord * vec2(textureSize(blockAtlas, 0));
    vec2 dx = dFdx(texelCoord);
    vec2 dy = dFdy(texelCoord);
    float mipLevel = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8));
    vec4 texColor = textureLod(blockAtlas, tiledTexCoord, clamp(mipLevel, 0.0, MAX_ATLAS_MIP));

    // Discard fully transparent pixels
    if (texColor.a < 0.01) {
        discard;
    }

#ifdef DEBUG_WORLD_POS
    // DEBUG: Visualize world position (modulo 32 to show patterns)
    vec3 debugColor = fract(fragWorldPos / 32.0);
    outColor = vec4(debugColor, 1.0);
    return;
#endif

#ifdef DEBUG_LOCAL_POS
    // DEBUG: Visualize local position within chunk (should be 0-16)
    // Red if outside expected range
    vec3 localPos = fragWorldPos - camera.cameraPos;  // Approximate local
    bool outOfRange = any(lessThan(localPos, vec3(-256.0))) || any(greaterThan(localPos, vec3(256.0)));
    if (outOfRange) {
        outColor = vec4(1.0, 0.0, 0.0, 1.0);  // Red = bad!
        return;
    }
    outColor = vec4(fract(localPos / 16.0), 1.0);
    return;
#endif

#ifdef DEBUG_CLIP_DEPTH
    // DEBUG: Visualize clip space depth
    float depth = fragClipPos.z / fragClipPos.w;
    outColor = vec4(vec3(depth), 1.0);
    return;
#endif

#ifdef DEBUG_NORMALS
    // DEBUG: Visualize normals - just show them as colors
    outColor = vec4(fragNormal * 0.5 + 0.5, 1.0);
    return;
#endif

    // Calculate lighting
    vec3 normal = normalize(fragNormal);

    // Dynamic directional light (sun direction from push constants)
    float NdotL = max(dot(normal, chunk.sunDirection), 0.0);
    float diffuse = NdotL * DIFFUSE;

    // Per-face shading for that classic voxel look
    float faceShade = getFaceShade(normal);

    // Combine directional lighting with face shading
    float dirLighting = min(chunk.ambientLevel + diffuse, 1.0) * faceShade;

    // Apply ambient occlusion
    dirLighting *= fragAO;

    // Combine sky and block light with sky brightness multiplier
    // Sky light is attenuated by time of day; block light (torches) stays constant
    float skyContrib = fragSkyLight * chunk.skyBrightness;
    float blockContrib = fragBlockLight;
    float smoothLight = max(max(skyContrib, blockContrib), 0.1);  // Min ambient floor

    float lighting = dirLighting * smoothLight;

    // Final color before fog
    vec3 albedo = texColor.rgb * fragTint;
    if (fragBlend.z != 0u) {
        albedo = applyBlend(albedo, cellCoord(fragCellTexCoord, fragTileBounds), mipLevel);
    }
    vec3 finalColor = albedo * lighting;

    // Apply distance fog
    float fogFactor = calculateFog(fragDistance);
    finalColor = mix(finalColor, chunk.fogColor, fogFactor);

    finalColor = applyRegionTint(finalColor, fragWorldPos);

    // Assuming sRGB framebuffer handles gamma
    outColor = vec4(finalColor, texColor.a);
}

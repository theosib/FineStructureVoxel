#version 450

// ============================================================================
// Chunk Fragment Shader
//
// Simple textured rendering with ambient occlusion and basic directional
// lighting for block faces.
// ============================================================================

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;     // Raw texture coordinates (may extend beyond tile bounds)
layout(location = 3) in vec4 fragTileBounds;   // Texture tile bounds (minU, minV, maxU, maxV)
layout(location = 4) in float fragAO;
layout(location = 5) in vec4 fragClipPos;      // DEBUG
layout(location = 6) in float fragDistance;    // Distance from camera (for fog)

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

// Per-chunk push constants (must match vertex shader)
layout(push_constant) uniform PushConstants {
    vec3 chunkOffset;  // View-relative position of subchunk origin
    float fogStart;    // Fog start distance (0 = fog disabled)
    vec3 fogColor;     // Fog color
    float fogEnd;      // Fog end distance
} chunk;

// Lighting constants
const vec3 LIGHT_DIR = normalize(vec3(0.5, 1.0, 0.3));  // Sun direction
const float AMBIENT = 0.4;                               // Ambient light level
const float DIFFUSE = 0.6;                               // Diffuse light strength

// Calculate fog factor based on distance
// Returns 0.0 (no fog) to 1.0 (full fog)
float calculateFog(float distance) {
    // Fog disabled if start >= end
    if (chunk.fogStart >= chunk.fogEnd) {
        return 0.0;
    }
    return clamp((distance - chunk.fogStart) / (chunk.fogEnd - chunk.fogStart), 0.0, 1.0);
}

// Per-face brightness (Minecraft-style)
// Top faces are brightest, bottom darkest, sides in between
float getFaceShade(vec3 normal) {
    if (normal.y > 0.5) return 1.0;        // Top: full brightness
    if (normal.y < -0.5) return 0.5;       // Bottom: darkest
    if (abs(normal.z) > 0.5) return 0.8;   // North/South: medium-light
    return 0.6;                             // East/West: medium-dark
}

// Wrap texture coordinates to tile within atlas cell bounds
// This allows greedy meshing to work with texture atlases by tiling
// the texture across merged faces rather than stretching
vec2 wrapTexCoord(vec2 uv, vec4 tileBounds) {
    vec2 tileMin = tileBounds.xy;  // minU, minV
    vec2 tileMax = tileBounds.zw;  // maxU, maxV
    vec2 tileSize = tileMax - tileMin;

    // Wrap UV coordinates within tile bounds using fract()
    // uv is in tile-space (0 to N for N tiles), wrap to 0-1 then scale to tile
    vec2 wrapped = fract((uv - tileMin) / tileSize) * tileSize + tileMin;

    // Clamp to stay within tile bounds with half-texel margin
    // This prevents bilinear filtering from sampling adjacent atlas cells
    // The atlas has 2-pixel borders, so we have some margin, but clamping
    // ensures we never sample outside our designated content area
    vec2 halfTexel = vec2(0.5) / vec2(textureSize(blockAtlas, 0));
    wrapped = clamp(wrapped, tileMin + halfTexel, tileMax - halfTexel);

    return wrapped;
}

// DEBUG: Uncomment one of these to enable debug visualization
// #define DEBUG_WORLD_POS
// #define DEBUG_LOCAL_POS
// #define DEBUG_CLIP_DEPTH
// #define DEBUG_NORMALS

void main() {
    // Wrap texture coordinates for proper tiling within atlas cell
    vec2 tiledTexCoord = wrapTexCoord(fragTexCoord, fragTileBounds);

    // Sample the block atlas texture using explicit LOD 0
    // We use textureLod instead of texture because fract() creates a UV discontinuity
    // at wrap points (1.0 -> 0.0). The GPU's derivative calculations (dFdx/dFdy) see
    // this as a huge change, causing incorrect LOD selection and filtering artifacts.
    // By forcing LOD 0, we bypass the derivative issue entirely.
    vec4 texColor = textureLod(blockAtlas, tiledTexCoord, 0.0);

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
    // Normal (0,-1,0) maps to color (0.5, 0, 0.5) = dark purple
    // Normal (0,1,0) maps to color (0.5, 1, 0.5) = light green
    // Normal (1,0,0) maps to color (1, 0.5, 0.5) = salmon
    // Normal (-1,0,0) maps to color (0, 0.5, 0.5) = teal
    // Normal (0,0,1) maps to color (0.5, 0.5, 1) = light blue
    // Normal (0,0,-1) maps to color (0.5, 0.5, 0) = olive
    outColor = vec4(fragNormal * 0.5 + 0.5, 1.0);
    return;
#endif

    // Calculate lighting
    vec3 normal = normalize(fragNormal);

    // Simple directional light
    float NdotL = max(dot(normal, LIGHT_DIR), 0.0);
    float diffuse = NdotL * DIFFUSE;

    // Per-face shading for that classic voxel look
    float faceShade = getFaceShade(normal);

    // Combine lighting
    float lighting = (AMBIENT + diffuse) * faceShade;

    // Apply ambient occlusion
    lighting *= fragAO;

    // Final color before fog
    vec3 finalColor = texColor.rgb * lighting;

    // Apply distance fog
    float fogFactor = calculateFog(fragDistance);
    finalColor = mix(finalColor, chunk.fogColor, fogFactor);

    // Output with gamma correction (if not using sRGB framebuffer)
    // outColor = vec4(pow(finalColor, vec3(1.0/2.2)), texColor.a);

    // Assuming sRGB framebuffer handles gamma
    outColor = vec4(finalColor, texColor.a);
}

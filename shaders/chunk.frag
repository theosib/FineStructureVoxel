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
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragAO;

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

// Lighting constants
const vec3 LIGHT_DIR = normalize(vec3(0.5, 1.0, 0.3));  // Sun direction
const float AMBIENT = 0.4;                               // Ambient light level
const float DIFFUSE = 0.6;                               // Diffuse light strength

// Per-face brightness (Minecraft-style)
// Top faces are brightest, bottom darkest, sides in between
float getFaceShade(vec3 normal) {
    if (normal.y > 0.5) return 1.0;        // Top: full brightness
    if (normal.y < -0.5) return 0.5;       // Bottom: darkest
    if (abs(normal.z) > 0.5) return 0.8;   // North/South: medium-light
    return 0.6;                             // East/West: medium-dark
}

void main() {
    // Sample the block atlas texture
    vec4 texColor = texture(blockAtlas, fragTexCoord);

    // Discard fully transparent pixels
    if (texColor.a < 0.01) {
        discard;
    }

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

    // Final color
    vec3 finalColor = texColor.rgb * lighting;

    // Output with gamma correction (if not using sRGB framebuffer)
    // outColor = vec4(pow(finalColor, vec3(1.0/2.2)), texColor.a);

    // Assuming sRGB framebuffer handles gamma
    outColor = vec4(finalColor, texColor.a);
}

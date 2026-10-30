// Blend toward a colour by a value read from a grid laid over the map, so that what's in a
// place can be dimmed without the blocks knowing. See core/region_tint.hpp.

layout(std430, set = 0, binding = 2) readonly buffer RegionTintData {
    vec3 color;
    float enabled;
    vec2 origin;       // World XZ of the grid's corner
    float cellSize;
    int columns;
    int rows;
    float cells[];
} regionTint;

float regionTintCell(int x, int z) {
    if (x < 0 || z < 0 || x >= regionTint.columns || z >= regionTint.rows) return 0.0;
    return regionTint.cells[z * regionTint.columns + x];
}

vec3 applyRegionTint(vec3 color, vec3 worldPos) {
    if (regionTint.enabled == 0.0) return color;

    // Cell values sit at cell centres. Smoothstep rather than linear between them, because
    // straight ramps meet in creases that show the grid.
    vec2 grid = (worldPos.xz - regionTint.origin) / regionTint.cellSize - 0.5;
    vec2 base = floor(grid);
    vec2 blend = smoothstep(0.0, 1.0, grid - base);
    int x = int(base.x);
    int z = int(base.y);

    float near = mix(regionTintCell(x, z), regionTintCell(x + 1, z), blend.x);
    float far = mix(regionTintCell(x, z + 1), regionTintCell(x + 1, z + 1), blend.x);
    return mix(regionTint.color, color, mix(near, far, blend.y));
}

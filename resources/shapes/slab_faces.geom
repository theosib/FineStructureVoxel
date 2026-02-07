# Slab geometry (bottom half of a block)
# Y ranges from 0 to 0.5
# Winding: CCW when looking at the front of each face
#
# Only the bottom face is a standard face (index 2) since it's a full face
# that can occlude neighbors. All other faces are half-height and use
# custom names to get indices 6+ so they don't trigger face culling.

# Bottom face (solid - blocks neighbor) - standard face index 2
face:bottom:
    0 0 1    0 0
    1 0 1    1 0
    1 0 0    1 1
    0 0 0    0 1

# Top face (at y=0.5) - custom face, NOT a full top face
face:slab_top:
    0 0.5 0  0 0
    1 0.5 0  1 0
    1 0.5 1  1 1
    0 0.5 1  0 1

# West face (-X) - custom face, half height
face:slab_west:
    0 0   1    0 0
    0 0   0    1 0
    0 0.5 0    1 0.5
    0 0.5 1    0 0.5

# East face (+X) - custom face, half height
face:slab_east:
    1 0   0    0 0
    1 0   1    1 0
    1 0.5 1    1 0.5
    1 0.5 0    0 0.5

# North face (-Z) - custom face, half height
face:slab_north:
    0 0   0    0 0
    1 0   0    1 0
    1 0.5 0    1 0.5
    0 0.5 0    0 0.5

# South face (+Z) - custom face, half height
face:slab_south:
    1 0   1    0 0
    0 0   1    1 0
    0 0.5 1    1 0.5
    1 0.5 1    0 0.5

# Bottom face is solid (occludes neighbor's top face)
solid-faces: bottom

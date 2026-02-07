# Stairs geometry (north-facing, climb toward -Z)
# Front step: z=0.5 to 1.0, y=0 to 0.5
# Back step: z=0 to 0.5, y=0.5 to 1.0
# Winding: CCW when looking at the front of each face
#
# Standard faces (indices 0-5) are only used for truly full faces:
# - bottom (index 2): full bottom face
# - north (index 4): full height back face
# All other faces use custom names to avoid incorrect face culling.

# Bottom face (solid) - standard face index 2
face:bottom:
    0 0 1    0 0
    1 0 1    1 0
    1 0 0    1 1
    0 0 0    0 1

# Top face (back half only, at y=1) - custom, not a full top
face:stairs_top:
    0 1.0 0    0 0
    1 1.0 0    1 0
    1 1.0 0.5  1 0.5
    0 1.0 0.5  0 0.5

# Step top face (front half, at y=0.5) - custom
face:step_top:
    0 0.5 0.5  0 0
    1 0.5 0.5  1 0
    1 0.5 1    1 0.5
    0 0.5 1    0 0.5

# Riser face (vertical surface at z=0.5, between y=0.5 and y=1) - custom
face:riser:
    1 0.5 0.5  0 0
    0 0.5 0.5  1 0
    0 1.0 0.5  1 0.5
    1 1.0 0.5  0 0.5

# North face (-Z, full height) - standard face index 4
face:north:
    0 0   0    0 0
    1 0   0    1 0
    1 1.0 0    1 1
    0 1.0 0    0 1

# South face (+Z, half height) - custom, not a full south face
face:stairs_south:
    1 0   1    0 0
    0 0   1    1 0
    0 0.5 1    1 0.5
    1 0.5 1    0 0.5

# West face (-X) - split into two parts for the step shape (both custom)

# West lower step (z from 0.5 to 1, y from 0 to 0.5)
face:west_lower:
    0 0   1    0 0
    0 0   0.5  0.5 0
    0 0.5 0.5  0.5 0.5
    0 0.5 1    0 0.5

# West upper step (z from 0 to 0.5, y from 0 to 1)
face:west_upper:
    0 0   0.5  0.5 0
    0 0   0    1 0
    0 1.0 0    1 1
    0 1.0 0.5  0.5 1

# East face (+X) - split into two parts for the step shape (both custom)

# East lower step (z from 0.5 to 1, y from 0 to 0.5)
face:east_lower:
    1 0   0.5  0.5 0
    1 0   1    1 0
    1 0.5 1    1 0.5
    1 0.5 0.5  0.5 0.5

# East upper step (z from 0 to 0.5, y from 0 to 1)
face:east_upper:
    1 0   0    0 0
    1 0   0.5  0.5 0
    1 1.0 0.5  0.5 1
    1 1.0 0    0 1

# Solid faces: bottom and north (blocks neighbor's top and south faces)
solid-faces: bottom north

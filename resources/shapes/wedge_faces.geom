# Wedge geometry (north-facing ramp, slopes up toward -Z)
# High edge at z=0 (y=1), low edge at z=1 (y=0)
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

# Diagonal top face (slopes from z=0,y=1 to z=1,y=0) - custom, not a full top
face:wedge_slope:
    0 1 0    0 1
    1 1 0    1 1
    1 0 1    1 0
    0 0 1    0 0

# North face (-Z, full height, rectangular) - standard face index 4
face:north:
    0 0 0    0 0
    1 0 0    1 0
    1 1 0    1 1
    0 1 0    0 1

# South face (+Z) - no face, wedge ends at ground level

# West face (-X, triangular) - custom, not a full west face
face:wedge_west:
    0 0 1    0 0
    0 0 0    1 0
    0 1 0    1 1

# East face (+X, triangular) - custom, not a full east face
face:wedge_east:
    1 0 0    0 0
    1 0 1    1 0
    1 1 0    0 1

# Bottom and north faces are solid
solid-faces: bottom north

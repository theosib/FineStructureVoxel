# Solid cube geometry - standard 1x1x1 block
# All 6 faces are full faces that can occlude neighbors
# Winding: CCW when looking at the front of each face

# Bottom face (-Y)
face:bottom:
    0 0 1    0 0
    1 0 1    1 0
    1 0 0    1 1
    0 0 0    0 1

# Top face (+Y)
face:top:
    0 1 0    0 0
    1 1 0    1 0
    1 1 1    1 1
    0 1 1    0 1

# West face (-X)
face:west:
    0 0 1    0 0
    0 0 0    1 0
    0 1 0    1 1
    0 1 1    0 1

# East face (+X)
face:east:
    1 0 0    0 0
    1 0 1    1 0
    1 1 1    1 1
    1 1 0    0 1

# North face (-Z)
face:north:
    0 0 0    0 0
    1 0 0    1 0
    1 1 0    1 1
    0 1 0    0 1

# South face (+Z)
face:south:
    1 0 1    0 0
    0 0 1    1 0
    0 1 1    1 1
    1 1 1    0 1

# All faces are solid (occlude neighbors)
solid-faces: bottom top west east north south

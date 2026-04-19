# Tool-classification tags.
#
# Members of these tags are item type names (the same names passed to
# ItemTypeId::fromName). When a block is broken, the tool the player
# holds is placed in LootContext.toolUsed, and .loot files can gate
# entries on "tool-tag finevox:tool.precise" (etc.) or "precise-break"
# (an alias checked in C++ for silk-touch-equivalent behaviour).
#
# Mining tools — tag pickaxe-like items.
tag finevox:tool.pickaxe {
    finevox:pickaxe
    finevox:stone_pickaxe
    finevox:iron_pickaxe
    finevox:diamond_pickaxe
}

tag finevox:tool.axe {
    finevox:axe
    finevox:stone_axe
    finevox:iron_axe
    finevox:diamond_axe
}

tag finevox:tool.shovel {
    finevox:shovel
    finevox:stone_shovel
    finevox:iron_shovel
    finevox:diamond_shovel
}

# Precise-break (silk-touch equivalent) tools — preserve the exact block
# form on mining instead of dropping a downgraded/random product. Scripts
# that produce LootContext should set preciseBreak = true whenever the
# held tool has this tag, so .loot files can route on either the explicit
# condition or the boolean flag.
tag finevox:tool.precise {
    finevox:silk_pickaxe
}

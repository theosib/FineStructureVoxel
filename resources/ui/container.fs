# Generic container window — shows a container's inventory + player bag
#
# Variables set before sourcing:
#   container_owner  — owner name registered in InventoryBridge
#   container_section — section name for the container slots
#   container_rows / container_cols — grid dimensions
#   container_title — window title
#
# Depends: scripts/inventory_helpers (for swap_or_stack)

{source "scripts/inventory_helpers"}

# Click handler: swap cursor with target slot
set on_slot_click fn [owner section index] do
    {swap_or_stack "player" "cursor" 0 owner section index}
end

# Right-click handler: deposit one or pick up half
set on_slot_right_click fn [owner section index] do
    {right_click_slot "player" "cursor" 0 owner section index}
end

# Build container grid
set container_grid {build_inv_grid container_owner container_section
    container_rows container_cols 48 on_slot_click on_slot_right_click}

# Build player bag grid (4x9) below
set player_bag {build_inv_grid "player" "bag" 4 9 48 on_slot_click on_slot_right_click}

# Layout
set container_window {ui.window container_title
    =window_flags [:no_resize :no_collapse :no_move :always_auto_resize]
    =children [
        {ui.text container_title}
        {ui.separator}
        {ui.group =id "container_grid_group" =children container_grid}

        {ui.spacing}
        {ui.separator}
        {ui.text "Bag"}
        {ui.separator}
        {ui.group =id "container_bag_group" =children player_bag}
    ]
}

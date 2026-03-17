# Player inventory window — main bag + 2x2 crafting grid + output slot
#
# Uses build_inv_grid (C++ native) to construct slot button arrays.
# Slot contents are updated per-frame from C++ via findById.
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

# Build inventory bag grid (4 rows x 9 cols)
set bag_slots {build_inv_grid "player" "bag" 4 9 48 on_slot_click on_slot_right_click}

# Build 2x2 hand-crafting grid
set craft_slots {build_inv_grid "player" "craft_grid" 2 2 48 on_slot_click on_slot_right_click}

# Craft output slot — clicking executes craft and places result in cursor
set craft_output {ui.button "" =width 48 =height 48 =id "slot_craft_output_0"
    =on_click fn [] do
        set result {craft_execute "player" "craft_grid" 2 2 "none"}
        if (result != nil) do
            # Place crafted item into cursor
            {inv_set "player" "cursor" 0 result}
        end
    end}

# Cursor item display (floating label near mouse)
set cursor_display {ui.text "" =id "cursor_label"}

# Layout: inventory window with bag + crafting area
set inventory_window {ui.window "Inventory"
    =window_flags [:no_resize :no_collapse :no_move :always_auto_resize]
    =children [
        # Crafting area header
        {ui.text "Crafting"}
        {ui.separator}

        # Crafting grid + arrow + output in a row
        {ui.group =children [
            # 2x2 craft grid (wrapped in group)
            {ui.group =id "craft_grid_group" =children craft_slots}

            {ui.same_line}

            # Arrow indicator
            {ui.text "=>"}

            {ui.same_line}

            # Output slot
            craft_output
        ]}

        {ui.spacing}
        {ui.separator}
        {ui.text "Bag"}
        {ui.separator}

        # Main bag grid
        {ui.group =id "bag_grid_group" =children bag_slots}
    ]
}

# Workbench crafting window — 3x3 crafting grid + output slot + player bag
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

# Build 3x3 workbench crafting grid
set wb_craft_slots {build_inv_grid "player" "wb_craft_grid" 3 3 48 on_slot_click}

# Craft output slot — clicking executes craft with workbench station
set wb_craft_output {ui.button "" =width 48 =height 48 =id "slot_wb_craft_output_0"
    =on_click fn [] do
        set result {craft_execute "player" "wb_craft_grid" 3 3 "finevox:workbench"}
        if (result != nil) do
            {inv_set "player" "cursor" 0 result}
        end
    end}

# Build player bag grid (4x9) below
set wb_bag_slots {build_inv_grid "player" "bag" 4 9 48 on_slot_click}

# Layout
set workbench_window {ui.window "Workbench"
    =window_flags [:no_resize :no_collapse :no_move :always_auto_resize]
    =children [
        {ui.text "Crafting (3x3)"}
        {ui.separator}

        # Crafting grid + arrow + output in a row
        {ui.group =children [
            {ui.group =id "wb_craft_grid_group" =children wb_craft_slots}

            {ui.same_line}
            {ui.text "=>"}
            {ui.same_line}

            wb_craft_output
        ]}

        {ui.spacing}
        {ui.separator}
        {ui.text "Bag"}
        {ui.separator}

        {ui.group =id "wb_bag_grid_group" =children wb_bag_slots}
    ]
}

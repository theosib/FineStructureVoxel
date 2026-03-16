# Recipe browser — scrollable list of available recipes
#
# build_recipe_browser(station, grid_section, grid_w, grid_h) builds
# a complete recipe browser window widget tree with clickable entries
# that auto-fill the crafting grid from the player's bag.

set build_recipe_browser fn [station grid_section grid_w grid_h] do
    set recipe_list_items {build_recipe_list station grid_section grid_w grid_h}

    {ui.window "Recipes"
        =window_flags [:no_resize :no_collapse :no_move]
        =width 280 =height 400
        =children [
            {ui.text "Available Recipes"}
            {ui.separator}
            {ui.child =id "recipe_scroll" =width 260 =height 340
                =children recipe_list_items}
        ]
    }
end

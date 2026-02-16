# Pause menu UI definitions
# Defines main_menu, settings_menu, and dim_overlay variables.
# These are map trees — C++ shows/hides them via mapRenderer.

set main_menu {ui.window "##pause_main"
    =window_flags [:no_title_bar :no_resize :no_move :always_auto_resize]
    =window_pivot_x 0.5 =window_pivot_y 0.5
    =children [
        {ui.text_colored [1 1 1 0.9] "Game Paused"}
        {ui.separator}
        {ui.spacing}
        {ui.button "Resume" =width 200 =height 40 =on_click fn [] do resume_game end}
        {ui.spacing}
        {ui.button "Settings" =width 200 =height 40 =on_click fn [] do show_settings end}
        {ui.spacing}
        {ui.button "Quit" =width 200 =height 40 =on_click fn [] do quit_game end}
    ]
}

set settings_menu {ui.window "##settings"
    =window_flags [:no_title_bar :no_resize :no_move :always_auto_resize]
    =window_pivot_x 0.5 =window_pivot_y 0.5
    =children [
        {ui.text_colored [1 1 1 0.9] "Settings"}
        {ui.separator}
        {ui.spacing}
        {ui.slider "View Distance" 128 32 512 =id "view_dist" =format "%.0f" =on_change fn [v] do set_view_distance v end}
        {ui.slider "Field of View" 70 50 120 =id "fov" =format "%.0f" =on_change fn [v] do set_fov v end}
        {ui.slider "Mouse Sensitivity" 1.0 0.1 3.0 =id "sensitivity" =format "%.2f" =on_change fn [v] do set_sensitivity v end}
        {ui.spacing}
        {ui.separator}
        {ui.slider "Time Speed" 1.0 0.0 100.0 =id "time_speed" =format "%.1f" =on_change fn [v] do set_time_speed v end}
        {ui.checkbox "Freeze Time" false =id "freeze_time" =on_change fn [v] do set_freeze_time v end}
        {ui.spacing}
        {ui.separator}
        {ui.combo "Lighting" ["Off" "Flat" "Smooth"] 2 =id "lighting" =on_change fn [i] do set_lighting i end}
        {ui.spacing}
        {ui.separator}
        {ui.spacing}
        {ui.button "Back" =width 200 =height 40 =on_click fn [] do show_main_menu end}
    ]
}

set dim_overlay {ui.window "##dim"
    =window_flags [:no_title_bar :no_resize :no_move :no_scrollbar :no_inputs]
    =window_alpha 0.47
    =children []
}

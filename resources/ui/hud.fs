# HUD overlay — health, hunger, and stamina bars
# Values are updated per-frame from C++ via findById
# Uses progress_bar widgets with text overlays

set hud_overlay {ui.window "##hud"
    =window_flags [:no_title_bar :no_resize :no_move :no_background :always_auto_resize :no_inputs]
    =children [
        # Health bar (red)
        {ui.push_color :plot_histogram [0.8 0.2 0.2 0.9] =id "hp_bar_col"}
        {ui.progress_bar 1.0
            =width 180 =height 16
            =overlay "HP: 20 / 20"
            =id "hp_bar"}
        {ui.pop_color 1}

        # Hunger bar (brown/gold)
        {ui.push_color :plot_histogram [0.7 0.5 0.15 0.9] =id "hunger_bar_col"}
        {ui.progress_bar 1.0
            =width 180 =height 16
            =overlay "Hunger: 20 / 20"
            =id "hunger_bar"}
        {ui.pop_color 1}
    ]
}

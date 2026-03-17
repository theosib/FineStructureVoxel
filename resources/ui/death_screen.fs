# Death screen — shown when the player dies
# Hidden by default; shown via show_ui "death_screen"
# Respawn button calls player_respawn native

set death_screen {ui.window "You Died"
    =window_flags [:no_resize :no_collapse :no_move :always_auto_resize]
    =children [
        {ui.text_colored [1 0.3 0.3 1] "You were slain!"}
        {ui.spacing}
        {ui.button "Respawn" =width 120 =height 30 =id "respawn_btn"
            =on_click fn [] do
                {player_respawn}
            end}
    ]
}

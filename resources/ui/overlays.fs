# Debug and coordinates overlay definitions
# debug_overlay: F7 toggle, upper-right stats
# coords_overlay: always visible, upper-left position

set debug_overlay {ui.window "##debug"
    =window_flags [:no_title_bar :no_resize :no_move :no_background :always_auto_resize :no_inputs]
    =children [
        {ui.text_colored [1 1 0.4 0.9] "FPS: 0" =id "fps"}
        {ui.text_colored [1 1 1 0.85] "Chunks: 0/0 (culled 0)" =id "chunks"}
        {ui.text_colored [1 1 1 0.85] "Tris: 0" =id "tris"}
        {ui.text_colored [1 1 1 0.85] "Time: Day 0/36000" =id "time"}
        {ui.text_colored [1 1 1 0.85] "Mode: Fly" =id "mode"}
        {ui.text_colored [1 1 1 0.85] "LOD: ON  Greedy: ON" =id "lod"}
    ]
}

set coords_overlay {ui.window "##coords"
    =window_pos_x 10 =window_pos_y 10
    =window_flags [:no_title_bar :no_resize :no_move :no_background :always_auto_resize :no_inputs]
    =children [
        {ui.text_colored [1 1 1 0.85] "X: 0.0" =id "pos_x"}
        {ui.text_colored [1 1 1 0.85] "Y: 0.0" =id "pos_y"}
        {ui.text_colored [1 1 1 0.85] "Z: 0.0" =id "pos_z"}
    ]
}

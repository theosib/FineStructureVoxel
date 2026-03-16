# Hotbar overlay — bottom-center, 8 slots with selection highlight
# Slot labels and colors are updated per-frame from C++ via findById

set hotbar_overlay {ui.window "##hotbar"
    =window_flags [:no_title_bar :no_resize :no_move :no_scrollbar :always_auto_resize :no_inputs]
    =children [
        {ui.push_color :button [0.3 0.6 1 0.9] =id "s0_btn_col"}
        {ui.push_color :button_hovered [0.3 0.6 1 0.9] =id "s0_hov_col"}
        {ui.button "Stone\n[1]" =width 48 =height 48 =id "slot_0"}
        {ui.pop_color 2}

        {ui.same_line}
        {ui.push_color :button [0.2 0.2 0.2 0.7] =id "s1_btn_col"}
        {ui.push_color :button_hovered [0.2 0.2 0.2 0.7] =id "s1_hov_col"}
        {ui.button "Dirt\n[2]" =width 48 =height 48 =id "slot_1"}
        {ui.pop_color 2}

        {ui.same_line}
        {ui.push_color :button [0.2 0.2 0.2 0.7] =id "s2_btn_col"}
        {ui.push_color :button_hovered [0.2 0.2 0.2 0.7] =id "s2_hov_col"}
        {ui.button "Grass\n[3]" =width 48 =height 48 =id "slot_2"}
        {ui.pop_color 2}

        {ui.same_line}
        {ui.push_color :button [0.2 0.2 0.2 0.7] =id "s3_btn_col"}
        {ui.push_color :button_hovered [0.2 0.2 0.2 0.7] =id "s3_hov_col"}
        {ui.button "Cobble\n[4]" =width 48 =height 48 =id "slot_3"}
        {ui.pop_color 2}

        {ui.same_line}
        {ui.push_color :button [0.2 0.2 0.2 0.7] =id "s4_btn_col"}
        {ui.push_color :button_hovered [0.2 0.2 0.2 0.7] =id "s4_hov_col"}
        {ui.button "Glow\n[5]" =width 48 =height 48 =id "slot_4"}
        {ui.pop_color 2}

        {ui.same_line}
        {ui.push_color :button [0.2 0.2 0.2 0.7] =id "s5_btn_col"}
        {ui.push_color :button_hovered [0.2 0.2 0.2 0.7] =id "s5_hov_col"}
        {ui.button "Slab\n[6]" =width 48 =height 48 =id "slot_5"}
        {ui.pop_color 2}

        {ui.same_line}
        {ui.push_color :button [0.2 0.2 0.2 0.7] =id "s6_btn_col"}
        {ui.push_color :button_hovered [0.2 0.2 0.2 0.7] =id "s6_hov_col"}
        {ui.button "Stair\n[7]" =width 48 =height 48 =id "slot_6"}
        {ui.pop_color 2}

        {ui.same_line}
        {ui.push_color :button [0.2 0.2 0.2 0.7] =id "s7_btn_col"}
        {ui.push_color :button_hovered [0.2 0.2 0.2 0.7] =id "s7_hov_col"}
        {ui.button "Wedge\n[8]" =width 48 =height 48 =id "slot_7"}
        {ui.pop_color 2}

        {ui.same_line}
        {ui.push_color :button [0.2 0.2 0.2 0.7] =id "s8_btn_col"}
        {ui.push_color :button_hovered [0.2 0.2 0.2 0.7] =id "s8_hov_col"}
        {ui.button "Bench\n[9]" =width 48 =height 48 =id "slot_8"}
        {ui.pop_color 2}
    ]
}

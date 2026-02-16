# Console window definition
# console_window: bottom of screen, scrollable output + input field

set console_window {ui.window "Console"
    =window_flags [:no_move :no_resize :no_collapse]
    =on_close fn [] do close_console end
    =children [
        {ui.child "##console_output" [] =id "console_output" =border true =auto_scroll true =height -30}
        {ui.input "##console_input" ""
            =id "console_input"
            =on_submit fn [text] do submit_command text end
            =on_history fn [dir] do get_history dir end
        }
    ]
}

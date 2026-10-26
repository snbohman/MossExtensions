#pragma once

namespace moss::extensions::imgui {

/*
 * Optional, on at most one entity in the Context (Begin warns and uses the
 * first if several carry it). Absent means the defaults below.
 *
 *     .attach<ig::Settings>({.keyboardNav = true})
 */
struct Settings {
    bool saveLayout = false;    // write imgui.ini (window positions) in the working directory
    bool keyboardNav = false;   // ImGuiConfigFlags_NavEnableKeyboard
    bool darkTheme = true;
    bool docking = false;
    bool dockspace = false;
};

}

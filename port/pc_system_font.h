// The UI font for the in-game PC menu and the native launcher: the platform's
// regular sans-serif face, or nullptr to keep ImGui's built-in font.
#pragma once
#include <filesystem>
#include <system_error>

inline const char* buzz_system_ui_font()
{
#ifdef _WIN32
    static const char* const candidates[]={"C:/Windows/Fonts/segoeui.ttf"};
#else
    // Debian/Ubuntu, Arch/SteamOS, Fedora, then DejaVu and GNOME defaults.
    static const char* const candidates[]={
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
#endif
    for (const char* path : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) return path;
    }
    return nullptr;
}

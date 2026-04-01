#ifndef APPPREFERENCES_H
#define APPPREFERENCES_H

#include <Windows.h>
#include <string>

enum class UiLanguage {
    Turkish = 0,
    English = 1,
};

enum class ThemePreset {
    Midnight = 0,
    Graphite = 1,
    Emerald = 2,
    Ivory = 3,
    Crimson = 4,
    Custom = 5,
};

struct AppPreferences {
    bool darkMode = true;
    bool highlightModified = true;
    COLORREF modifiedCellColor = RGB(255, 200, 200);
    bool modernCheckboxes = true;
    UiLanguage language = UiLanguage::Turkish;
    ThemePreset themePreset = ThemePreset::Midnight;
    COLORREF customAccentColor = RGB(45, 115, 210);
    COLORREF customBackgroundColor = RGB(20, 24, 32);
    COLORREF customTextColor = RGB(235, 238, 245);
    bool rgbTheme = false;
    int rgbSpeed = 35;

    bool load();
    bool save() const;
    std::wstring getConfigPath() const;
};

#endif

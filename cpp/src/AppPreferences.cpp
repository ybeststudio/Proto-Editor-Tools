#include "AppPreferences.h"
#include <cstdio>

std::wstring AppPreferences::getConfigPath() const {
    wchar_t appData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0)
        return L"";
    std::wstring path = appData;
    path += L"\\BestStudio\\ProtoEditor";
    CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\prefs.ini";
    return path;
}

bool AppPreferences::load() {
    std::wstring path = getConfigPath();
    if (path.empty()) return false;
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"ProtoEditor", L"DarkMode", L"1", buf, 64, path.c_str());
    darkMode = (buf[0] == L'1' || buf[0] == L't' || buf[0] == L'T');
    GetPrivateProfileStringW(L"ProtoEditor", L"HighlightModified", L"1", buf, 64, path.c_str());
    highlightModified = (buf[0] != L'0' && buf[0] != L'f' && buf[0] != L'F');
    GetPrivateProfileStringW(L"ProtoEditor", L"ModifiedColorR", L"255", buf, 64, path.c_str());
    int r = (int)wcstol(buf, nullptr, 10);
    if (r < 0) r = 0; if (r > 255) r = 255;
    GetPrivateProfileStringW(L"ProtoEditor", L"ModifiedColorG", L"200", buf, 64, path.c_str());
    int g = (int)wcstol(buf, nullptr, 10);
    if (g < 0) g = 0; if (g > 255) g = 255;
    GetPrivateProfileStringW(L"ProtoEditor", L"ModifiedColorB", L"200", buf, 64, path.c_str());
    int b = (int)wcstol(buf, nullptr, 10);
    if (b < 0) b = 0; if (b > 255) b = 255;
    modifiedCellColor = RGB(r, g, b);
    GetPrivateProfileStringW(L"ProtoEditor", L"ModernCheckboxes", L"1", buf, 64, path.c_str());
    modernCheckboxes = (buf[0] != L'0' && buf[0] != L'f' && buf[0] != L'F');
    GetPrivateProfileStringW(L"ProtoEditor", L"Language", L"tr", buf, 64, path.c_str());
    language = (buf[0] == L'e' || buf[0] == L'E') ? UiLanguage::English : UiLanguage::Turkish;
    GetPrivateProfileStringW(L"ProtoEditor", L"ThemePreset", L"0", buf, 64, path.c_str());
    int themeValue = (int)wcstol(buf, nullptr, 10);
    if (themeValue < 0) themeValue = 0;
    if (themeValue > 5) themeValue = 5;
    themePreset = static_cast<ThemePreset>(themeValue);

    GetPrivateProfileStringW(L"ProtoEditor", L"CustomAccentR", L"45", buf, 64, path.c_str());
    r = (int)wcstol(buf, nullptr, 10);
    if (r < 0) r = 0; if (r > 255) r = 255;
    GetPrivateProfileStringW(L"ProtoEditor", L"CustomAccentG", L"115", buf, 64, path.c_str());
    g = (int)wcstol(buf, nullptr, 10);
    if (g < 0) g = 0; if (g > 255) g = 255;
    GetPrivateProfileStringW(L"ProtoEditor", L"CustomAccentB", L"210", buf, 64, path.c_str());
    b = (int)wcstol(buf, nullptr, 10);
    if (b < 0) b = 0; if (b > 255) b = 255;
    customAccentColor = RGB(r, g, b);

    GetPrivateProfileStringW(L"ProtoEditor", L"CustomBackgroundR", L"20", buf, 64, path.c_str());
    r = (int)wcstol(buf, nullptr, 10);
    if (r < 0) r = 0; if (r > 255) r = 255;
    GetPrivateProfileStringW(L"ProtoEditor", L"CustomBackgroundG", L"24", buf, 64, path.c_str());
    g = (int)wcstol(buf, nullptr, 10);
    if (g < 0) g = 0; if (g > 255) g = 255;
    GetPrivateProfileStringW(L"ProtoEditor", L"CustomBackgroundB", L"32", buf, 64, path.c_str());
    b = (int)wcstol(buf, nullptr, 10);
    if (b < 0) b = 0; if (b > 255) b = 255;
    customBackgroundColor = RGB(r, g, b);

    GetPrivateProfileStringW(L"ProtoEditor", L"CustomTextR", L"235", buf, 64, path.c_str());
    r = (int)wcstol(buf, nullptr, 10);
    if (r < 0) r = 0; if (r > 255) r = 255;
    GetPrivateProfileStringW(L"ProtoEditor", L"CustomTextG", L"238", buf, 64, path.c_str());
    g = (int)wcstol(buf, nullptr, 10);
    if (g < 0) g = 0; if (g > 255) g = 255;
    GetPrivateProfileStringW(L"ProtoEditor", L"CustomTextB", L"245", buf, 64, path.c_str());
    b = (int)wcstol(buf, nullptr, 10);
    if (b < 0) b = 0; if (b > 255) b = 255;
    customTextColor = RGB(r, g, b);
    GetPrivateProfileStringW(L"ProtoEditor", L"RgbTheme", L"0", buf, 64, path.c_str());
    rgbTheme = (buf[0] == L'1' || buf[0] == L't' || buf[0] == L'T');
    GetPrivateProfileStringW(L"ProtoEditor", L"RgbSpeed", L"35", buf, 64, path.c_str());
    rgbSpeed = (int)wcstol(buf, nullptr, 10);
    if (rgbSpeed < 5) rgbSpeed = 5;
    if (rgbSpeed > 100) rgbSpeed = 100;

    darkMode = (themePreset != ThemePreset::Ivory);
    return true;
}

bool AppPreferences::save() const {
    std::wstring path = getConfigPath();
    if (path.empty()) return false;
    wchar_t buf[32];
    swprintf_s(buf, L"%d", darkMode ? 1 : 0);
    WritePrivateProfileStringW(L"ProtoEditor", L"DarkMode", buf, path.c_str());
    swprintf_s(buf, L"%d", highlightModified ? 1 : 0);
    WritePrivateProfileStringW(L"ProtoEditor", L"HighlightModified", buf, path.c_str());
    swprintf_s(buf, L"%d", GetRValue(modifiedCellColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"ModifiedColorR", buf, path.c_str());
    swprintf_s(buf, L"%d", GetGValue(modifiedCellColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"ModifiedColorG", buf, path.c_str());
    swprintf_s(buf, L"%d", GetBValue(modifiedCellColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"ModifiedColorB", buf, path.c_str());
    swprintf_s(buf, L"%d", modernCheckboxes ? 1 : 0);
    WritePrivateProfileStringW(L"ProtoEditor", L"ModernCheckboxes", buf, path.c_str());
    WritePrivateProfileStringW(L"ProtoEditor", L"Language", language == UiLanguage::English ? L"en" : L"tr", path.c_str());
    swprintf_s(buf, L"%d", static_cast<int>(themePreset));
    WritePrivateProfileStringW(L"ProtoEditor", L"ThemePreset", buf, path.c_str());

    swprintf_s(buf, L"%d", GetRValue(customAccentColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"CustomAccentR", buf, path.c_str());
    swprintf_s(buf, L"%d", GetGValue(customAccentColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"CustomAccentG", buf, path.c_str());
    swprintf_s(buf, L"%d", GetBValue(customAccentColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"CustomAccentB", buf, path.c_str());

    swprintf_s(buf, L"%d", GetRValue(customBackgroundColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"CustomBackgroundR", buf, path.c_str());
    swprintf_s(buf, L"%d", GetGValue(customBackgroundColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"CustomBackgroundG", buf, path.c_str());
    swprintf_s(buf, L"%d", GetBValue(customBackgroundColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"CustomBackgroundB", buf, path.c_str());

    swprintf_s(buf, L"%d", GetRValue(customTextColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"CustomTextR", buf, path.c_str());
    swprintf_s(buf, L"%d", GetGValue(customTextColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"CustomTextG", buf, path.c_str());
    swprintf_s(buf, L"%d", GetBValue(customTextColor));
    WritePrivateProfileStringW(L"ProtoEditor", L"CustomTextB", buf, path.c_str());
    swprintf_s(buf, L"%d", rgbTheme ? 1 : 0);
    WritePrivateProfileStringW(L"ProtoEditor", L"RgbTheme", buf, path.c_str());
    swprintf_s(buf, L"%d", rgbSpeed);
    WritePrivateProfileStringW(L"ProtoEditor", L"RgbSpeed", buf, path.c_str());
    return true;
}

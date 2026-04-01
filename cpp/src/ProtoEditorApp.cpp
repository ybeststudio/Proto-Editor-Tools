#include "ProtoEditorApp.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <d3d11.h>
#include <commdlg.h>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
    constexpr const wchar_t* kAppWindowTitle = L"Proto Editor Tools | by Best Studio";
    constexpr int kAppIconResourceId = 101;

    std::string wideToUtf8(const std::wstring& value) {
        if (value.empty()) {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring utf8ToWide(const std::string& value) {
        if (value.empty()) {
            return {};
        }

        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    std::wstring toLowerCopy(std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }

    long long toNumber(const std::wstring& value) {
        if (value.empty()) {
            return 0;
        }

        wchar_t* end = nullptr;
        return std::wcstoll(value.c_str(), &end, 10);
    }

    std::vector<std::wstring> splitFlags(const std::wstring& value) {
        std::vector<std::wstring> result;
        std::wstring token;

        auto flushToken = [&]() {
            if (!token.empty()) {
                result.push_back(token);
                token.clear();
            }
        };

        for (wchar_t ch : value) {
            if (ch == L'|' || ch == L',' || ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
                flushToken();
                continue;
            }
            token.push_back(ch);
        }

        flushToken();
        return result;
    }

    std::wstring joinFlags(const std::vector<std::wstring>& flags) {
        std::wstring result;
        for (size_t i = 0; i < flags.size(); ++i) {
            if (i > 0) {
                result += L" | ";
            }
            result += flags[i];
        }
        return result;
    }

    bool containsCaseInsensitive(const std::wstring& haystack, const std::wstring& needle) {
        if (needle.empty()) {
            return true;
        }

        return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::wstring::npos;
    }

    ImVec4 colorRefToImVec4(COLORREF color, float alpha = 1.0f) {
        return ImVec4(
            static_cast<float>(GetRValue(color)) / 255.0f,
            static_cast<float>(GetGValue(color)) / 255.0f,
            static_cast<float>(GetBValue(color)) / 255.0f,
            alpha);
    }

    COLORREF imVec4ToColorRef(const ImVec4& color) {
        auto clampChannel = [](float value) -> int {
            if (value < 0.0f) return 0;
            if (value > 1.0f) return 255;
            return static_cast<int>(value * 255.0f + 0.5f);
        };
        return RGB(clampChannel(color.x), clampChannel(color.y), clampChannel(color.z));
    }

    ImVec4 rgbCycleColor(float t) {
        const float phase = t * 6.28318530718f;
        const float r = 0.5f + 0.5f * std::sinf(phase);
        const float g = 0.5f + 0.5f * std::sinf(phase + 2.09439510239f);
        const float b = 0.5f + 0.5f * std::sinf(phase + 4.18879020479f);
        return ImVec4(r, g, b, 1.0f);
    }

    bool isIntegerValue(const std::wstring& value) {
        if (value.empty()) {
            return true;
        }

        size_t index = 0;
        if (value[0] == L'-' || value[0] == L'+') {
            index = 1;
        }

        if (index >= value.size()) {
            return false;
        }

        for (; index < value.size(); ++index) {
            if (!iswdigit(value[index])) {
                return false;
            }
        }
        return true;
    }

    std::string escapeCsvValue(const std::wstring& value) {
        std::string utf8 = wideToUtf8(value);
        bool needsQuotes = utf8.find_first_of(",\"\r\n") != std::string::npos;
        size_t pos = 0;
        while ((pos = utf8.find('"', pos)) != std::string::npos) {
            utf8.insert(pos, 1, '"');
            pos += 2;
        }
        return needsQuotes ? "\"" + utf8 + "\"" : utf8;
    }

    std::string escapeSqlValue(const std::wstring& value) {
        std::string utf8 = wideToUtf8(value);
        size_t pos = 0;
        while ((pos = utf8.find('\'', pos)) != std::string::npos) {
            utf8.insert(pos, 1, '\'');
            pos += 2;
        }
        return utf8;
    }

    std::wstring buildTimestampString() {
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        wchar_t buffer[32] = {};
        swprintf_s(buffer, L"%04u%02u%02u_%02u%02u%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buffer;
    }

    constexpr int kDatasetCount = 2;
}

ProtoEditorApp::ProtoEditorApp(HINSTANCE instance)
    : instance_(instance) {
    datasets_[0].kind = DatasetKind::Item;
    datasets_[0].title = L"item_proto";
    datasets_[1].kind = DatasetKind::Mob;
    datasets_[1].title = L"mob_proto";
    preferences_.load();
}

ProtoEditorApp::~ProtoEditorApp() {
    preferences_.save();
    shutdownImGui();
    cleanupD3D();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

int ProtoEditorApp::run() {
    if (!initializeWindow()) {
        MessageBoxW(nullptr, trw(L"Window initialization failed.", L"Pencere başlatma başarısız.").c_str(), trw(L"Proto Editor Tools", L"Proto Editor Tools").c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!initializeD3D()) {
        MessageBoxW(nullptr, trw(L"DirectX 11 initialization failed.", L"DirectX 11 başlatma başarısız.").c_str(), trw(L"Proto Editor Tools", L"Proto Editor Tools").c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }

    initializeImGui();

    loadDataset(DatasetKind::Item);
    loadDataset(DatasetKind::Mob);

    MSG msg = {};
    while (running_) {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                running_ = false;
            }
        }

        if (!running_) {
            break;
        }

        renderFrame();
    }

    return 0;
}

bool ProtoEditorApp::initializeWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = ProtoEditorApp::WndProc;
    wc.hInstance = instance_;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(kAppIconResourceId));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(kAppIconResourceId), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.lpszClassName = L"ProtoEditorImGuiWindow";

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        kAppWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1600,
        920,
        nullptr,
        nullptr,
        instance_,
        this
    );

    if (!hwnd_) {
        return false;
    }

    SetWindowTextW(hwnd_, kAppWindowTitle);
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(wc.hIcon));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(wc.hIconSm));
    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);
    return true;
}

bool ProtoEditorApp::initializeD3D() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &swapChain_,
        &device_,
        &featureLevel,
        &deviceContext_
    );

    if (FAILED(hr)) {
        return false;
    }

    createRenderTarget();
    return true;
}

void ProtoEditorApp::cleanupD3D() {
    cleanupRenderTarget();
    if (swapChain_) {
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    if (deviceContext_) {
        deviceContext_->Release();
        deviceContext_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

void ProtoEditorApp::createRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView_);
        backBuffer->Release();
    }
}

void ProtoEditorApp::cleanupRenderTarget() {
    if (renderTargetView_) {
        renderTargetView_->Release();
        renderTargetView_ = nullptr;
    }
}

void ProtoEditorApp::initializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "proto_editor_imgui.ini";

    ImFontConfig fontConfig = {};
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = false;

    static const ImWchar turkishRanges[] = {
        0x0020, 0x00FF,
        0x011E, 0x011F,
        0x0130, 0x0131,
        0x015E, 0x015F,
        0,
    };

    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
    };

    for (const char* fontPath : fontCandidates) {
        if (io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, &fontConfig, turkishRanges) != nullptr) {
            break;
        }
    }

    if (io.Fonts->Fonts.empty()) {
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, deviceContext_);
    applyTheme();
}

void ProtoEditorApp::shutdownImGui() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void ProtoEditorApp::applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 8.0f;
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.CellPadding = ImVec2(10.0f, 8.0f);

    ImVec4* colors = style.Colors;
    ImVec4 accent = ImVec4(0.18f, 0.45f, 0.82f, 1.0f);
    ImVec4 background = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
    ImVec4 backgroundAlt = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    ImVec4 text = ImVec4(0.92f, 0.94f, 0.97f, 1.0f);
    bool darkTheme = true;

    switch (preferences_.themePreset) {
    case ThemePreset::Midnight:
        darkTheme = true;
        accent = ImVec4(0.18f, 0.45f, 0.82f, 1.0f);
        background = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
        backgroundAlt = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
        text = ImVec4(0.92f, 0.94f, 0.97f, 1.0f);
        break;
    case ThemePreset::Graphite:
        darkTheme = true;
        accent = ImVec4(0.52f, 0.60f, 0.72f, 1.0f);
        background = ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
        backgroundAlt = ImVec4(0.15f, 0.16f, 0.18f, 1.0f);
        text = ImVec4(0.90f, 0.91f, 0.93f, 1.0f);
        break;
    case ThemePreset::Emerald:
        darkTheme = true;
        accent = ImVec4(0.18f, 0.68f, 0.46f, 1.0f);
        background = ImVec4(0.06f, 0.10f, 0.09f, 1.0f);
        backgroundAlt = ImVec4(0.09f, 0.14f, 0.12f, 1.0f);
        text = ImVec4(0.90f, 0.95f, 0.92f, 1.0f);
        break;
    case ThemePreset::Ivory:
        darkTheme = false;
        accent = ImVec4(0.19f, 0.41f, 0.74f, 1.0f);
        background = ImVec4(0.95f, 0.95f, 0.93f, 1.0f);
        backgroundAlt = ImVec4(0.88f, 0.89f, 0.86f, 1.0f);
        text = ImVec4(0.11f, 0.12f, 0.14f, 1.0f);
        break;
    case ThemePreset::Crimson:
        darkTheme = true;
        accent = ImVec4(0.78f, 0.22f, 0.31f, 1.0f);
        background = ImVec4(0.12f, 0.08f, 0.10f, 1.0f);
        backgroundAlt = ImVec4(0.16f, 0.10f, 0.13f, 1.0f);
        text = ImVec4(0.97f, 0.92f, 0.93f, 1.0f);
        break;
    case ThemePreset::Custom:
        darkTheme = (GetRValue(preferences_.customBackgroundColor) + GetGValue(preferences_.customBackgroundColor) + GetBValue(preferences_.customBackgroundColor)) < 382;
        accent = colorRefToImVec4(preferences_.customAccentColor);
        background = colorRefToImVec4(preferences_.customBackgroundColor);
        backgroundAlt = ImVec4(
            (std::min)(background.x + 0.03f, 1.0f),
            (std::min)(background.y + 0.03f, 1.0f),
            (std::min)(background.z + 0.03f, 1.0f),
            1.0f);
        text = colorRefToImVec4(preferences_.customTextColor);
        break;
    }

    if (preferences_.rgbTheme) {
        const float speed = static_cast<float>(preferences_.rgbSpeed) / 35.0f;
        accent = rgbCycleColor(static_cast<float>(ImGui::GetTime()) * 0.18f * speed);
    }

    preferences_.darkMode = darkTheme;
    if (darkTheme) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = ImVec4(text.x * 0.68f, text.y * 0.68f, text.z * 0.68f, 1.0f);
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = backgroundAlt;
    colors[ImGuiCol_PopupBg] = ImVec4(backgroundAlt.x, backgroundAlt.y, backgroundAlt.z, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(accent.x * 0.60f, accent.y * 0.60f, accent.z * 0.60f, 0.70f);
    colors[ImGuiCol_FrameBg] = ImVec4(backgroundAlt.x + (darkTheme ? 0.03f : -0.03f), backgroundAlt.y + (darkTheme ? 0.03f : -0.03f), backgroundAlt.z + (darkTheme ? 0.03f : -0.03f), 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 0.45f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(accent.x * 0.65f, accent.y * 0.65f, accent.z * 0.65f, 0.55f);
    colors[ImGuiCol_Header] = ImVec4(accent.x * 0.75f, accent.y * 0.75f, accent.z * 0.75f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.72f);
    colors[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.92f);
    colors[ImGuiCol_Button] = ImVec4(accent.x * 0.78f, accent.y * 0.78f, accent.z * 0.78f, 0.82f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x, accent.y, accent.z, 0.96f);
    colors[ImGuiCol_ButtonActive] = ImVec4(accent.x * 0.65f, accent.y * 0.65f, accent.z * 0.65f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(backgroundAlt.x, backgroundAlt.y, backgroundAlt.z, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(accent.x * 0.82f, accent.y * 0.82f, accent.z * 0.82f, 0.82f);
    colors[ImGuiCol_TabActive] = ImVec4(accent.x * 0.72f, accent.y * 0.72f, accent.z * 0.72f, 0.95f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = ImVec4(accent.x * 0.85f, accent.y * 0.85f, accent.z * 0.85f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(accent.x * 0.45f, accent.y * 0.45f, accent.z * 0.45f, 0.55f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.65f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(accent.x, accent.y, accent.z, 0.85f);
}

bool ProtoEditorApp::hasActiveDataset() const {
    return activeDatasetIndex_ >= 0 && activeDatasetIndex_ < kDatasetCount;
}

const char* ProtoEditorApp::tr(const char* english, const char* turkish) const {
    return preferences_.language == UiLanguage::Turkish ? turkish : english;
}

std::string ProtoEditorApp::trs(const std::string& english, const std::string& turkish) const {
    return preferences_.language == UiLanguage::Turkish ? turkish : english;
}

std::wstring ProtoEditorApp::trw(const wchar_t* english, const wchar_t* turkish) const {
    return preferences_.language == UiLanguage::Turkish ? std::wstring(turkish) : std::wstring(english);
}

ProtoEditorApp::DatasetState& ProtoEditorApp::activeDataset() {
    return datasets_[activeDatasetIndex_];
}

const ProtoEditorApp::DatasetState& ProtoEditorApp::activeDataset() const {
    return datasets_[activeDatasetIndex_];
}

ProtoEditorApp::DatasetState& ProtoEditorApp::datasetByKind(DatasetKind kind) {
    return datasets_[kind == DatasetKind::Item ? 0 : 1];
}

void ProtoEditorApp::renderFrame() {
    if (preferences_.rgbTheme) {
        applyTheme();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    drawMenuBar();
    drawToolbar();
    drawSidebar();
    drawTablePanel();
    drawInspectorPanel();
    drawStatusBar();
    drawEditCellModal();
    drawFlagEditorModal();
    drawAboutModal();
    drawGotoRowModal();
    drawColumnManagerModal();
    drawBulkEditModal();
    drawColumnOrderModal();
    if (showValidationPanel_) {
        drawValidationPanel();
    }
    if (showHistoryPanel_) {
        drawHistoryPanel();
    }
    if (showSearchPanel_) {
        drawSearchPanel();
    }
    if (showComparePanel_) {
        drawComparePanel();
    }
    if (showLinkedNamesPanel_) {
        drawLinkedNamesPanel();
    }
    if (showWorkspacePanel_) {
        drawWorkspacePanel();
    }
    drawExportModal();
    if (showRulePresetPanel_) {
        drawRulePresetPanel();
    }
    if (showThemeBuilder_) {
        drawThemeBuilder();
    }

    ImGui::Render();

    const float clearColor[4] = { 0.06f, 0.07f, 0.09f, 1.0f };
    deviceContext_->OMSetRenderTargets(1, &renderTargetView_, nullptr);
    deviceContext_->ClearRenderTargetView(renderTargetView_, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swapChain_->Present(1, 0);
}

void ProtoEditorApp::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu(tr("File", "Dosya"))) {
        if (ImGui::MenuItem(tr("Open item_proto", "item_proto aç"))) {
            loadDataset(DatasetKind::Item, openFileDialog(trw(L"Open item_proto", L"item_proto aç").c_str(), trw(L"Proto files (*.txt;*.tsv)\0*.txt;*.tsv\0All files (*.*)\0*.*\0", L"Proto dosyaları (*.txt;*.tsv)\0*.txt;*.tsv\0Tüm dosyalar (*.*)\0*.*\0").c_str()));
        }
        if (ImGui::MenuItem(tr("Open mob_proto", "mob_proto aç"))) {
            loadDataset(DatasetKind::Mob, openFileDialog(trw(L"Open mob_proto", L"mob_proto aç").c_str(), trw(L"Proto files (*.txt;*.tsv)\0*.txt;*.tsv\0All files (*.*)\0*.*\0", L"Proto dosyaları (*.txt;*.tsv)\0*.txt;*.tsv\0Tüm dosyalar (*.*)\0*.*\0").c_str()));
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Save active", "Aktif sekmeyi kaydet"), "Ctrl+S", false, hasActiveDataset() && activeDataset().loaded)) {
            saveDataset(activeDataset(), false);
        }
        if (ImGui::MenuItem(tr("Save active as...", "Aktif sekmeyi farklı kaydet..."), nullptr, false, hasActiveDataset() && activeDataset().loaded)) {
            saveDataset(activeDataset(), true);
        }
        if (ImGui::MenuItem(tr("Export...", "Dışa aktar..."), nullptr, false, hasActiveDataset() && activeDataset().loaded)) {
            exportModalOpen_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Exit", "Çıkış"))) {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("Edit", "Düzenle"))) {
        if (ImGui::MenuItem(tr("Undo", "Geri al"), "Ctrl+Z", false, hasActiveDataset() && !activeDataset().undoStack.empty())) {
            undo(activeDataset());
        }
        if (ImGui::MenuItem(tr("Redo", "Yinele"), "Ctrl+Y", false, hasActiveDataset() && !activeDataset().redoStack.empty())) {
            redo(activeDataset());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Insert empty row", "Boş satır ekle"), "Ctrl+N", false, hasActiveDataset() && activeDataset().loaded)) {
            insertEmptyRow(activeDataset());
        }
        if (ImGui::MenuItem(tr("Duplicate selected row", "Seçili satırı kopyala"), "Ctrl+D", false, hasActiveDataset() && activeDataset().loaded && activeDataset().selectedRow >= 0)) {
            duplicateSelectedRow(activeDataset());
        }
        if (ImGui::MenuItem(tr("Delete selected row", "Seçili satırı sil"), "Delete", false, hasActiveDataset() && activeDataset().loaded && activeDataset().selectedRow >= 0)) {
            deleteSelectedRow(activeDataset());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Add column...", "Kolon ekle..."), nullptr, false, hasActiveDataset() && activeDataset().loaded)) {
            columnAction_ = 0;
            columnTargetIndex_ = activeDataset().selectedColumn;
            columnNameBuffer_.clear();
            columnManagerModalOpen_ = true;
            ImGui::OpenPopup(tr("Column Manager###ColumnManager", "Kolon Yöneticisi###ColumnManager"));
        }
        if (ImGui::MenuItem(tr("Rename selected column...", "Seçili kolonu yeniden adlandır..."), nullptr, false, hasActiveDataset() && activeDataset().loaded && activeDataset().selectedColumn >= 0)) {
            columnAction_ = 1;
            columnTargetIndex_ = activeDataset().selectedColumn;
            columnNameBuffer_ = wideToUtf8(activeDataset().table.header()[activeDataset().selectedColumn]);
            columnManagerModalOpen_ = true;
            ImGui::OpenPopup(tr("Column Manager###ColumnManager", "Kolon Yöneticisi###ColumnManager"));
        }
        if (ImGui::MenuItem(tr("Delete selected column", "Seçili kolonu sil"), nullptr, false, hasActiveDataset() && activeDataset().loaded && activeDataset().selectedColumn >= 0)) {
            deleteColumn(activeDataset(), activeDataset().selectedColumn);
        }
        if (ImGui::MenuItem(tr("Move selected column left", "Seçili kolonu sola taşı"), nullptr, false, hasActiveDataset() && activeDataset().loaded && activeDataset().selectedColumn > 0)) {
            moveColumn(activeDataset(), activeDataset().selectedColumn, activeDataset().selectedColumn - 1);
        }
        if (ImGui::MenuItem(tr("Move selected column right", "Seçili kolonu sağa taşı"), nullptr, false, hasActiveDataset() && activeDataset().loaded &&
            activeDataset().selectedColumn >= 0 &&
            activeDataset().selectedColumn < static_cast<int>(activeDataset().table.columnCount()) - 1)) {
            moveColumn(activeDataset(), activeDataset().selectedColumn, activeDataset().selectedColumn + 1);
        }
        if (ImGui::MenuItem(tr("Column order manager...", "Kolon sıralama yöneticisi..."), nullptr, false, hasActiveDataset() && activeDataset().loaded)) {
            columnOrderModalOpen_ = true;
            ImGui::OpenPopup(tr("Column Order Manager###ColumnOrderManager", "Kolon Sıralama Yöneticisi###ColumnOrderManager"));
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Bulk set selected column...", "Seçili kolonda toplu değer ata..."), nullptr, false, hasActiveDataset() && activeDataset().loaded && activeDataset().selectedColumn >= 0)) {
            bulkEditMode_ = 0;
            bulkValueBuffer_.clear();
            bulkVisibleRowsOnly_ = true;
            bulkEditModalOpen_ = true;
            ImGui::OpenPopup(tr("Bulk Column Tools###BulkColumnTools", "Toplu Kolon Araçları###BulkColumnTools"));
        }
        if (ImGui::MenuItem(tr("Bulk replace selected column...", "Seçili kolonda toplu değiştir..."), nullptr, false, hasActiveDataset() && activeDataset().loaded && activeDataset().selectedColumn >= 0)) {
            bulkEditMode_ = 1;
            bulkFindBuffer_.clear();
            bulkReplaceBuffer_.clear();
            bulkVisibleRowsOnly_ = true;
            bulkEditModalOpen_ = true;
            ImGui::OpenPopup(tr("Bulk Column Tools###BulkColumnTools", "Toplu Kolon Araçları###BulkColumnTools"));
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("Navigate", "Git"))) {
        if (ImGui::MenuItem(tr("Go to VNUM / first column", "VNUM / ilk kolona git"), "Ctrl+G", false, hasActiveDataset() && activeDataset().loaded)) {
            gotoRowModalOpen_ = true;
            ImGui::OpenPopup(tr("Go to Row###GotoRow", "Satıra Git###GotoRow"));
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("View", u8"G\u00F6r\u00FCn\u00FCm"))) {
        bool highlightModified = preferences_.highlightModified;
        if (ImGui::MenuItem(tr("Highlight modified cells", u8"De\u011Fi\u015Fen h\u00FCcreleri vurgula"), nullptr, &highlightModified)) {
            preferences_.highlightModified = highlightModified;
            preferences_.save();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu(tr("Themes", u8"Temalar"))) {
            const struct {
                ThemePreset preset;
                const char* en;
                const char* trLabel;
            } presets[] = {
                { ThemePreset::Midnight, "Midnight", u8"Gece Mavisi" },
                { ThemePreset::Graphite, "Graphite", u8"Grafit" },
                { ThemePreset::Emerald, "Emerald", u8"Z\u00FCmr\u00FCt" },
                { ThemePreset::Ivory, "Ivory", u8"Fildi\u015Fi" },
                { ThemePreset::Crimson, "Crimson", u8"K\u0131z\u0131l" },
                { ThemePreset::Custom, "Custom", u8"\u00D6zel" },
            };

            for (const auto& preset : presets) {
                const bool selected = preferences_.themePreset == preset.preset;
                if (ImGui::MenuItem(tr(preset.en, preset.trLabel), nullptr, selected)) {
                    preferences_.themePreset = preset.preset;
                    applyTheme();
                    preferences_.save();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Theme Builder...", u8"Tema Olu\u015Fturucu..."))) {
                showThemeBuilder_ = true;
            }
            ImGui::Separator();
            bool rgbTheme = preferences_.rgbTheme;
            if (ImGui::MenuItem(tr("Animated RGB Accent", u8"Animasyonlu RGB Vurgu"), nullptr, &rgbTheme)) {
                preferences_.rgbTheme = rgbTheme;
                applyTheme();
                preferences_.save();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        bool isTurkish = preferences_.language == UiLanguage::Turkish;
        bool isEnglish = preferences_.language == UiLanguage::English;
        if (ImGui::MenuItem(u8"TR - T\u00FCrk\u00E7e", nullptr, isTurkish)) {
            preferences_.language = UiLanguage::Turkish;
            for (auto& dataset : datasets_) {
                loadLinkedNames(dataset);
            }
            preferences_.save();
        }
        if (ImGui::MenuItem("EN - English", nullptr, isEnglish)) {
            preferences_.language = UiLanguage::English;
            for (auto& dataset : datasets_) {
                loadLinkedNames(dataset);
            }
            preferences_.save();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("Features", u8"\u00D6zellikler"))) {
        ImGui::MenuItem(tr("Validation Panel", u8"Do\u011Frulama Paneli"), nullptr, &showValidationPanel_);
        ImGui::MenuItem(tr("History Panel", u8"Ge\u00E7mi\u015F Paneli"), nullptr, &showHistoryPanel_);
        ImGui::MenuItem(tr("Search Panel", u8"Arama Paneli"), nullptr, &showSearchPanel_);
        ImGui::MenuItem(tr("Compare Panel", u8"Kar\u015F\u0131la\u015Ft\u0131rma Paneli"), nullptr, &showComparePanel_);
        ImGui::MenuItem(tr("Linked Names Panel", u8"Ba\u011Fl\u0131 Names Paneli"), nullptr, &showLinkedNamesPanel_);
        ImGui::MenuItem(tr("Workspace Presets", u8"\u00C7al\u0131\u015Fma Alan\u0131 Presetleri"), nullptr, &showWorkspacePanel_);
        ImGui::MenuItem(tr("Rule Presets", u8"Kural Presetleri"), nullptr, &showRulePresetPanel_);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("Help", u8"Yard\u0131m"))) {
        if (ImGui::MenuItem(tr("About", u8"Hakk\u0131nda"))) {
            aboutModalOpen_ = true;
            ImGui::OpenPopup(tr("About Proto Editor Tools###About", u8"Proto Editor Tools Hakk\u0131nda###About"));
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void ProtoEditorApp::drawToolbar() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 34.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x - 24.0f, 68.0f), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (!ImGui::Begin(tr("Toolbar", u8"Ara\u00E7 \u00C7ubu\u011Fu"), nullptr, flags)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button(tr("Open Item", u8"Item A\u00E7"))) {
        loadDataset(DatasetKind::Item, openFileDialog(trw(L"Open item_proto", L"item_proto aç").c_str(), trw(L"Proto files (*.txt;*.tsv)\0*.txt;*.tsv\0All files (*.*)\0*.*\0", L"Proto dosyaları (*.txt;*.tsv)\0*.txt;*.tsv\0Tüm dosyalar (*.*)\0*.*\0").c_str()));
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Open Mob", u8"Mob A\u00E7"))) {
        loadDataset(DatasetKind::Mob, openFileDialog(trw(L"Open mob_proto", L"mob_proto aç").c_str(), trw(L"Proto files (*.txt;*.tsv)\0*.txt;*.tsv\0All files (*.*)\0*.*\0", L"Proto dosyaları (*.txt;*.tsv)\0*.txt;*.tsv\0Tüm dosyalar (*.*)\0*.*\0").c_str()));
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Save Active", "Aktifi Kaydet")) && hasActiveDataset() && activeDataset().loaded) {
        saveDataset(activeDataset(), false);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Reload Active", u8"Aktifi Yeniden Y\u00FCkle")) && hasActiveDataset() && activeDataset().loaded) {
        loadDataset(activeDataset().kind, activeDataset().filePath);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Undo", "Geri Al")) && hasActiveDataset() && !activeDataset().undoStack.empty()) {
        undo(activeDataset());
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Redo", "Yinele")) && hasActiveDataset() && !activeDataset().redoStack.empty()) {
        redo(activeDataset());
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(tr("Language:", u8"Dil:"));
    ImGui::SameLine();
    if (ImGui::Button("TR")) {
        preferences_.language = UiLanguage::Turkish;
        for (auto& dataset : datasets_) {
            loadLinkedNames(dataset);
        }
        preferences_.save();
    }
    ImGui::SameLine();
    if (ImGui::Button("EN")) {
        preferences_.language = UiLanguage::English;
        for (auto& dataset : datasets_) {
            loadLinkedNames(dataset);
        }
        preferences_.save();
    }

    ImGui::Separator();
    if (ImGui::BeginTabBar("DatasetTabs")) {
        for (int index = 0; index < kDatasetCount; ++index) {
            DatasetState& dataset = datasets_[index];
            std::string tabLabel = wideToUtf8(dataset.title);
            if (dataset.modified) {
                tabLabel += " *";
            }

            if (ImGui::BeginTabItem(tabLabel.c_str())) {
                activeDatasetIndex_ = index;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void ProtoEditorApp::drawSidebar() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 110.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(280.0f, ImGui::GetIO().DisplaySize.y - 158.0f), ImGuiCond_Always);

    if (!ImGui::Begin(tr("Workspace", "Çalışma Alanı"), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        return;
    }

    DatasetState& dataset = activeDataset();
    ImGui::TextUnformatted(dataset.loaded ? tr("Dataset loaded", "Veri yüklendi") : tr("Dataset not loaded", "Veri yüklenmedi"));
    ImGui::Separator();
    ImGui::TextWrapped(tr("Current file: %s", "Mevcut dosya: %s"), dataset.filePath.empty() ? "-" : wideToUtf8(dataset.filePath).c_str());
    ImGui::TextWrapped(tr("Config: %s", "Config: %s"), dataset.configPath.empty() ? "-" : wideToUtf8(dataset.configPath).c_str());
    ImGui::Text(tr("Rows: %d", "Satır: %d"), static_cast<int>(dataset.table.rowCount()));
    ImGui::Text(tr("Columns: %d", "Kolon: %d"), static_cast<int>(dataset.table.columnCount()));
    ImGui::Text(tr("Encoding: %s", "Kodlama: %s"), dataset.table.encoding().empty() ? "-" : dataset.table.encoding().c_str());
    ImGui::Text(tr("Undo: %d", "Geri Al: %d"), static_cast<int>(dataset.undoStack.size()));
    ImGui::Text(tr("Redo: %d", "Yinele: %d"), static_cast<int>(dataset.redoStack.size()));

    ImGui::Separator();
    ImGui::TextUnformatted(tr("Filter", "Filtre"));

    if (dataset.loaded && !dataset.table.header().empty()) {
        std::string currentLabel = dataset.filterColumn >= 0 ? wideToUtf8(dataset.table.header()[dataset.filterColumn]) : tr("All columns", "Tum kolonlar");
        if (ImGui::BeginCombo(tr("Column", "Kolon"), currentLabel.c_str())) {
            if (ImGui::Selectable(tr("All columns", "Tum kolonlar"), dataset.filterColumn < 0)) {
                dataset.filterColumn = -1;
                rebuildFilteredRows(dataset);
            }
            for (size_t i = 0; i < dataset.table.header().size(); ++i) {
                const bool selected = dataset.filterColumn == static_cast<int>(i);
                const std::string columnName = wideToUtf8(dataset.table.header()[i]);
                if (ImGui::Selectable(columnName.c_str(), selected)) {
                    dataset.filterColumn = static_cast<int>(i);
                    rebuildFilteredRows(dataset);
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::InputTextWithHint(tr("Search", "Ara"), tr("vnum, name, flag...", "vnum, isim, flag..."), &dataset.filterText)) {
            rebuildFilteredRows(dataset);
        }

        if (ImGui::Button(tr("Clear filter", "Filtreyi temizle"))) {
            dataset.filterText.clear();
            dataset.filterColumn = -1;
            rebuildFilteredRows(dataset);
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(tr("Quick Actions", "Hızlı İşlemler"));

    if (dataset.loaded && !dataset.table.header().empty()) {
        std::string selectedColumnLabel =
            (dataset.selectedColumn >= 0 && dataset.selectedColumn < static_cast<int>(dataset.table.columnCount()))
            ? wideToUtf8(dataset.table.header()[dataset.selectedColumn])
            : tr("Select column", "Kolon seç");

        if (ImGui::BeginCombo(tr("Active column", "Aktif kolon"), selectedColumnLabel.c_str())) {
            for (size_t i = 0; i < dataset.table.header().size(); ++i) {
                const bool selected = dataset.selectedColumn == static_cast<int>(i);
                const std::string columnName = wideToUtf8(dataset.table.header()[i]);
                if (ImGui::Selectable(columnName.c_str(), selected)) {
                    dataset.selectedColumn = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::Button(tr("Open cell editor", "Hücre düzenleyicisini aç"), ImVec2(-1.0f, 0.0f))) {
        if (dataset.loaded && dataset.selectedRow >= 0 && dataset.selectedColumn >= 0) {
            openCellEditor(dataset, dataset.filteredRows[dataset.selectedRow], dataset.selectedColumn);
        }
    }
    if (ImGui::Button(tr("Open flag editor", "Flag düzenleyicisini aç"), ImVec2(-1.0f, 0.0f))) {
        if (dataset.loaded && dataset.selectedRow >= 0 && dataset.selectedColumn >= 0) {
            openFlagEditor(dataset, dataset.filteredRows[dataset.selectedRow], dataset.selectedColumn);
        }
    }
    if (ImGui::Button(tr("Insert empty row", "Boş satır ekle"), ImVec2(-1.0f, 0.0f)) && dataset.loaded) {
        insertEmptyRow(dataset);
    }
    if (ImGui::Button(tr("Duplicate selected row", "Seçili satırı kopyala"), ImVec2(-1.0f, 0.0f)) && dataset.loaded && dataset.selectedRow >= 0) {
        duplicateSelectedRow(dataset);
    }
    if (ImGui::Button(tr("Delete selected row", "Seçili satırı sil"), ImVec2(-1.0f, 0.0f)) && dataset.loaded && dataset.selectedRow >= 0) {
        deleteSelectedRow(dataset);
    }
    if (ImGui::Button(tr("Go to VNUM / first column", "VNUM / ilk kolona git"), ImVec2(-1.0f, 0.0f)) && dataset.loaded) {
        gotoRowModalOpen_ = true;
        ImGui::OpenPopup(tr("Go to Row###GotoRow", "Satıra Git###GotoRow"));
    }
    if (ImGui::Button(tr("Add column", "Kolon ekle"), ImVec2(-1.0f, 0.0f)) && dataset.loaded) {
        columnAction_ = 0;
        columnTargetIndex_ = dataset.selectedColumn;
        columnNameBuffer_.clear();
        columnManagerModalOpen_ = true;
        ImGui::OpenPopup(tr("Column Manager###ColumnManager", "Kolon Yöneticisi###ColumnManager"));
    }
    if (ImGui::Button(tr("Rename selected column", "Seçili kolonu yeniden adlandır"), ImVec2(-1.0f, 0.0f)) && dataset.loaded && dataset.selectedColumn >= 0) {
        columnAction_ = 1;
        columnTargetIndex_ = dataset.selectedColumn;
        columnNameBuffer_ = wideToUtf8(dataset.table.header()[dataset.selectedColumn]);
        columnManagerModalOpen_ = true;
        ImGui::OpenPopup(tr("Column Manager###ColumnManager", "Kolon Yöneticisi###ColumnManager"));
    }
    if (ImGui::Button(tr("Delete selected column", "Seçili kolonu sil"), ImVec2(-1.0f, 0.0f)) && dataset.loaded && dataset.selectedColumn >= 0) {
        deleteColumn(dataset, dataset.selectedColumn);
    }
    if (ImGui::Button(tr("Move selected column left", "Seçili kolonu sola taşı"), ImVec2(-1.0f, 0.0f)) && dataset.loaded && dataset.selectedColumn > 0) {
        moveColumn(dataset, dataset.selectedColumn, dataset.selectedColumn - 1);
    }
    if (ImGui::Button(tr("Move selected column right", "Seçili kolonu sağa taşı"), ImVec2(-1.0f, 0.0f)) &&
        dataset.loaded &&
        dataset.selectedColumn >= 0 &&
        dataset.selectedColumn < static_cast<int>(dataset.table.columnCount()) - 1) {
        moveColumn(dataset, dataset.selectedColumn, dataset.selectedColumn + 1);
    }
    if (ImGui::Button(tr("Column order manager", "Kolon sıralama yöneticisi"), ImVec2(-1.0f, 0.0f)) && dataset.loaded) {
        columnOrderModalOpen_ = true;
        ImGui::OpenPopup(tr("Column Order Manager###ColumnOrderManager", "Kolon Sıralama Yöneticisi###ColumnOrderManager"));
    }
    if (ImGui::Button(tr("Bulk set selected column", "Seçili kolona toplu değer ata"), ImVec2(-1.0f, 0.0f)) && dataset.loaded && dataset.selectedColumn >= 0) {
        bulkEditMode_ = 0;
        bulkValueBuffer_.clear();
        bulkVisibleRowsOnly_ = true;
        bulkEditModalOpen_ = true;
        ImGui::OpenPopup(tr("Bulk Column Tools###BulkColumnTools", "Toplu Kolon Araçları###BulkColumnTools"));
    }
    if (ImGui::Button(tr("Bulk replace selected column", "Seçili kolonda toplu değiştir"), ImVec2(-1.0f, 0.0f)) && dataset.loaded && dataset.selectedColumn >= 0) {
        bulkEditMode_ = 1;
        bulkFindBuffer_.clear();
        bulkReplaceBuffer_.clear();
        bulkVisibleRowsOnly_ = true;
        bulkEditModalOpen_ = true;
        ImGui::OpenPopup(tr("Bulk Column Tools###BulkColumnTools", "Toplu Kolon Araçları###BulkColumnTools"));
    }
    if (ImGui::Button(tr("Go to first modified", "İlk değişen hücreye git"), ImVec2(-1.0f, 0.0f)) && !dataset.modifiedCells.empty()) {
        const auto first = *dataset.modifiedCells.begin();
        auto it = std::find(dataset.filteredRows.begin(), dataset.filteredRows.end(), first.first);
        if (it != dataset.filteredRows.end()) {
            dataset.selectedRow = static_cast<int>(std::distance(dataset.filteredRows.begin(), it));
            dataset.selectedColumn = first.second;
            requestScrollToSelection_ = true;
        }
    }

    ImGui::End();
}

void ProtoEditorApp::drawTablePanel() {
    ImGui::SetNextWindowPos(ImVec2(304.0f, 110.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x - 628.0f, ImGui::GetIO().DisplaySize.y - 158.0f), ImGuiCond_Always);

    if (!ImGui::Begin(tr("Proto Grid", "Proto Grid"), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        return;
    }

    DatasetState& dataset = activeDataset();
    if (!dataset.loaded) {
        ImGui::TextWrapped(tr("No %s file loaded. Use the toolbar to open a proto file.", "%s dosyasi yuklenmedi. Arac cubugundan bir proto dosyasi acin."), wideToUtf8(dataset.title).c_str());
        ImGui::End();
        return;
    }

    std::vector<int> renderColumns;
    renderColumns.reserve(dataset.table.header().size());
    for (int column : dataset.pinnedColumns) {
        if (column >= 0 &&
            column < static_cast<int>(dataset.table.columnCount()) &&
            dataset.hiddenColumns.find(column) == dataset.hiddenColumns.end()) {
            renderColumns.push_back(column);
        }
    }
    for (int column = 0; column < static_cast<int>(dataset.table.columnCount()); ++column) {
        if (dataset.hiddenColumns.find(column) != dataset.hiddenColumns.end()) {
            continue;
        }
        if (dataset.pinnedColumns.find(column) != dataset.pinnedColumns.end()) {
            continue;
        }
        renderColumns.push_back(column);
    }

    if (renderColumns.empty()) {
        ImGui::TextUnformatted(tr("All columns are hidden. Use the workspace panel to show columns again.",
            "Tum kolonlar gizli. Kolonlari tekrar gostermek icin calisma alani panelini kullanin."));
        ImGui::End();
        return;
    }

    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Sortable |
        ImGuiTableFlags_Hideable;

    if (ImGui::BeginTable("ProtoTable", static_cast<int>(renderColumns.size()), tableFlags)) {
        int visiblePinnedCount = 0;
        for (int column : renderColumns) {
            if (dataset.pinnedColumns.find(column) != dataset.pinnedColumns.end()) {
                ++visiblePinnedCount;
            } else {
                break;
            }
        }
        ImGui::TableSetupScrollFreeze((std::min)((std::max)(visiblePinnedCount, 1), static_cast<int>(renderColumns.size())), 1);
        for (int column : renderColumns) {
            ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_WidthFixed;
            if (column == 0) {
                flags |= ImGuiTableColumnFlags_DefaultSort;
            }
            ImGui::TableSetupColumn(wideToUtf8(dataset.table.header()[column]).c_str(), flags, 140.0f, static_cast<ImGuiID>(column));
        }
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs && sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
            dataset.sortColumn = static_cast<int>(sortSpecs->Specs[0].ColumnUserID);
            dataset.sortAscending = sortSpecs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            sortFilteredRows(dataset);
            sortSpecs->SpecsDirty = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(dataset.filteredRows.size()));
        while (clipper.Step()) {
            for (int filteredIndex = clipper.DisplayStart; filteredIndex < clipper.DisplayEnd; ++filteredIndex) {
                const size_t sourceRow = dataset.filteredRows[filteredIndex];
                const auto& row = dataset.table.rows()[sourceRow];
                ImGui::TableNextRow();

                for (size_t renderIndex = 0; renderIndex < renderColumns.size(); ++renderIndex) {
                    const int column = renderColumns[renderIndex];
                    ImGui::TableSetColumnIndex(static_cast<int>(renderIndex));
                    const bool selected = dataset.selectedRow == filteredIndex && dataset.selectedColumn == column;
                    const bool modified = dataset.modifiedCells.find({ sourceRow, column }) != dataset.modifiedCells.end();

                    if (preferences_.highlightModified && modified) {
                        const COLORREF color = preferences_.modifiedCellColor;
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(GetRValue(color), GetGValue(color), GetBValue(color), 80));
                    }

                    std::string cellText = wideToUtf8(row[column]);
                    if (cellText.empty()) {
                        cellText = " ";
                    }

                    ImGui::PushID(static_cast<int>(sourceRow * 1000 + column));
                    if (ImGui::Selectable(cellText.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
                        dataset.selectedRow = filteredIndex;
                        dataset.selectedColumn = column;
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            if (dataset.config.isFlagColumn(dataset.table.header()[column])) {
                                openFlagEditor(dataset, sourceRow, column);
                            } else {
                                openCellEditor(dataset, sourceRow, column);
                            }
                        }
                    }
                    if (selected && requestScrollToSelection_) {
                        ImGui::SetScrollHereY(0.35f);
                        requestScrollToSelection_ = false;
                    }
                    ImGui::PopID();
                }
            }
        }

        ImGui::EndTable();
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (dataset.selectedRow >= 0 && dataset.selectedColumn >= 0) {
            const size_t sourceRow = dataset.filteredRows[dataset.selectedRow];
            if (dataset.config.isFlagColumn(dataset.table.header()[dataset.selectedColumn])) {
                openFlagEditor(dataset, sourceRow, dataset.selectedColumn);
            } else {
                openCellEditor(dataset, sourceRow, dataset.selectedColumn);
            }
        }
    }

    ImGui::End();
}

void ProtoEditorApp::drawInspectorPanel() {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 312.0f, 110.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300.0f, ImGui::GetIO().DisplaySize.y - 158.0f), ImGuiCond_Always);

    if (!ImGui::Begin(tr("Inspector", "Inspector"), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        return;
    }

    DatasetState& dataset = activeDataset();
    if (!dataset.loaded || dataset.selectedRow < 0 || dataset.selectedRow >= static_cast<int>(dataset.filteredRows.size())) {
        ImGui::TextWrapped(tr("Select a row from the grid to inspect and edit its values.", "Degerleri incelemek ve duzenlemek icin grid uzerinden bir satir secin."));
        ImGui::End();
        return;
    }

    const size_t sourceRow = dataset.filteredRows[dataset.selectedRow];
    auto& row = dataset.table.rows()[sourceRow];

    ImGui::Text(tr("Source row: %d", "Kaynak satir: %d"), static_cast<int>(sourceRow + 1));
    if (!row.empty()) {
        ImGui::TextWrapped(tr("Primary key: %s", "Birincil anahtar: %s"), wideToUtf8(row[0]).c_str());
    }
    ImGui::Separator();

    for (size_t column = 0; column < row.size(); ++column) {
        ImGui::PushID(static_cast<int>(column));
        const std::string columnName = wideToUtf8(dataset.table.header()[column]);
        ImGui::TextUnformatted(columnName.c_str());

        if (dataset.config.isFlagColumn(dataset.table.header()[column])) {
            ImGui::TextWrapped("%s", wideToUtf8(row[column]).c_str());
            if (ImGui::Button(tr("Edit flags", "Flagleri duzenle"), ImVec2(-1.0f, 0.0f))) {
                dataset.selectedColumn = static_cast<int>(column);
                openFlagEditor(dataset, sourceRow, static_cast<int>(column));
            }
        } else {
            ImGui::TextWrapped("%s", wideToUtf8(row[column]).c_str());
            if (ImGui::Button(tr("Edit value", "Degeri duzenle"), ImVec2(-1.0f, 0.0f))) {
                dataset.selectedColumn = static_cast<int>(column);
                openCellEditor(dataset, sourceRow, static_cast<int>(column));
            }
        }
        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::End();
}

void ProtoEditorApp::drawStatusBar() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, ImGui::GetIO().DisplaySize.y - 40.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x - 24.0f, 28.0f), ImGuiCond_Always);

    if (!ImGui::Begin("StatusBar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        return;
    }

    const DatasetState& dataset = activeDataset();
    ImGui::TextUnformatted("Proto Editor Tools | by Best Studio");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(statusText_.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text(tr("Active: %s", "Aktif: %s"), wideToUtf8(dataset.title).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text(tr("Visible rows: %d / %d", "Gorunen satirlar: %d / %d"), static_cast<int>(dataset.filteredRows.size()), static_cast<int>(dataset.table.rowCount()));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text(tr("Modified cells: %d", "Degisen hucreler: %d"), static_cast<int>(dataset.modifiedCells.size()));

    ImGui::End();
}

void ProtoEditorApp::drawEditCellModal() {
    if (editModalOpen_) {
        ImGui::OpenPopup(tr("Cell Editor###CellEditor", "Hucre Editoru###CellEditor"));
        editModalOpen_ = false;
    }

    if (!ImGui::BeginPopupModal(tr("Cell Editor###CellEditor", "Hucre Editoru###CellEditor"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    DatasetState& dataset = activeDataset();
    const std::string label = wideToUtf8(dataset.table.header()[editColumn_]);
    std::wstring columnType = L"string";
    for (const auto& def : dataset.config.columns()) {
        if (def.name == dataset.table.header()[editColumn_]) {
            columnType = def.type;
            break;
        }
    }

    const bool isIntegerColumn = columnType == L"int";
    const bool inputValid = !isIntegerColumn || isIntegerValue(utf8ToWide(editBuffer_));
    ImGui::Text(tr("Column: %s", "Kolon: %s"), label.c_str());
    ImGui::Text(tr("Type: %s", "Tur: %s"), wideToUtf8(columnType).c_str());
    ImGui::InputTextMultiline("##editbuffer", &editBuffer_, ImVec2(620.0f, 220.0f));

    if (!inputValid) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
            "%s",
            tr("This column accepts only integer values.", "Bu kolon sadece tam sayi degeri kabul eder."));
    }

    if (ImGui::Button(tr("Apply", "Uygula"), ImVec2(120.0f, 0.0f)) && inputValid) {
        setCellValue(dataset, editSourceRow_, editColumn_, utf8ToWide(editBuffer_));
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel", "Iptal"), ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ProtoEditorApp::drawFlagEditorModal() {
    if (flagModalOpen_) {
        ImGui::OpenPopup(tr("Flag Editor###FlagEditor", "Flag Editoru###FlagEditor"));
        flagModalOpen_ = false;
    }

    if (!ImGui::BeginPopupModal(tr("Flag Editor###FlagEditor", "Flag Editoru###FlagEditor"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    DatasetState& dataset = activeDataset();
    const std::wstring& columnName = dataset.table.header()[flagColumn_];
    const std::wstring flagSetName = dataset.config.flagSetForColumn(columnName);
    const auto& allFlags = dataset.config.flagList(flagSetName);

    ImGui::Text(tr("Column: %s", "Kolon: %s"), wideToUtf8(columnName).c_str());
    ImGui::InputTextWithHint(tr("Search", "Ara"), "IMMUNE_STUN, WEAR_BODY...", &flagSearch_);

    if (ImGui::Button(tr("Select all", "Tumunu sec"))) {
        flagSelection_ = allFlags;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Clear", "Temizle"))) {
        flagSelection_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset", "Sifirla"))) {
        flagSelection_ = splitFlags(flagOriginalValue_);
    }

    ImGui::Separator();
    ImGui::BeginChild("FlagList", ImVec2(520.0f, 300.0f), true);
    const std::wstring filter = utf8ToWide(flagSearch_);
    for (const std::wstring& flag : allFlags) {
        if (!containsCaseInsensitive(flag, filter)) {
            continue;
        }

        bool selected = std::find(flagSelection_.begin(), flagSelection_.end(), flag) != flagSelection_.end();
        std::string flagUtf8 = wideToUtf8(flag);
        if (ImGui::Checkbox(flagUtf8.c_str(), &selected)) {
            if (selected) {
                flagSelection_.push_back(flag);
            } else {
                flagSelection_.erase(std::remove(flagSelection_.begin(), flagSelection_.end(), flag), flagSelection_.end());
            }
        }
    }
    ImGui::EndChild();

    if (ImGui::Button(tr("Apply", "Uygula"), ImVec2(120.0f, 0.0f))) {
        setCellValue(dataset, flagSourceRow_, flagColumn_, joinFlags(flagSelection_));
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel", "Iptal"), ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ProtoEditorApp::drawAboutModal() {
    if (aboutModalOpen_) {
        ImGui::OpenPopup(tr("About Proto Editor Tools###About", "Proto Editor Tools Hakkinda###About"));
        aboutModalOpen_ = false;
    }

    if (!ImGui::BeginPopupModal(tr("About Proto Editor Tools###About", "Proto Editor Tools Hakkinda###About"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Proto Editor Tools | by Best Studio");
    ImGui::TextUnformatted(tr("Professional Dear ImGui based Metin2 proto editing suite.", "Dear ImGui tabanli profesyonel Metin2 proto duzenleme paketi."));
    ImGui::Spacing();
    ImGui::BulletText(tr("Large TSV files with clipped rendering", "Buyuk TSV dosyalarinda clipper destekli render"));
    ImGui::BulletText(tr("Cell inspector and modal editors", "Hucre inspector ve modal editorler"));
    ImGui::BulletText(tr("Flag-aware editing driven by YAML config", "YAML config destekli flag editor akisi"));
    ImGui::BulletText(tr("UTF-8 / CP1254 compatible save flow", "UTF-8 / CP1254 uyumlu kaydetme akisi"));
    ImGui::BulletText(tr("Minimum side-effect architecture: data layer preserved", "Dusuk yan etkili mimari: veri katmani korunur"));
    ImGui::Spacing();

    if (ImGui::Button(tr("Close", "Kapat"), ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ProtoEditorApp::drawGotoRowModal() {
    if (gotoRowModalOpen_) {
        ImGui::OpenPopup(tr("Go to Row###GotoRow", "Satira Git###GotoRow"));
        gotoRowModalOpen_ = false;
    }

    if (!ImGui::BeginPopupModal(tr("Go to Row###GotoRow", "Satira Git###GotoRow"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    DatasetState& dataset = activeDataset();
    ImGui::TextUnformatted(tr("Enter VNUM or first-column value:", "VNUM veya ilk kolon degerini girin:"));
    ImGui::InputTextWithHint("##goto", tr("101, 20035, ...", "101, 20035, ..."), &dataset.gotoRowBuffer);

    if (ImGui::Button(tr("Go", "Git"), ImVec2(120.0f, 0.0f))) {
        gotoRowByFirstColumn(dataset, utf8ToWide(dataset.gotoRowBuffer));
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel", "Iptal"), ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ProtoEditorApp::drawColumnManagerModal() {
    if (columnManagerModalOpen_) {
        ImGui::OpenPopup(tr("Column Manager###ColumnManager", "Kolon Yoneticisi###ColumnManager"));
        columnManagerModalOpen_ = false;
    }

    if (!ImGui::BeginPopupModal(tr("Column Manager###ColumnManager", "Kolon Yoneticisi###ColumnManager"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    DatasetState& dataset = activeDataset();

    if (columnAction_ == 0) {
        ImGui::TextUnformatted(tr("Add new column", "Yeni kolon ekle"));
        ImGui::InputTextWithHint(tr("Column name", "Kolon adi"), "NEW_COLUMN", &columnNameBuffer_);

        const char* positionLabel = tr("End of table", "Tablonun sonu");
        if (columnTargetIndex_ >= 0 && columnTargetIndex_ < static_cast<int>(dataset.table.columnCount())) {
            static std::string label;
            label = trs("After: ", "Sonra: ") + wideToUtf8(dataset.table.header()[columnTargetIndex_]);
            positionLabel = label.c_str();
        }
        ImGui::Text(tr("Position: %s", "Konum: %s"), positionLabel);

        if (ImGui::Button(tr("Add", "Ekle"), ImVec2(120.0f, 0.0f))) {
            addColumn(dataset, utf8ToWide(columnNameBuffer_), columnTargetIndex_);
            ImGui::CloseCurrentPopup();
        }
    } else {
        ImGui::TextUnformatted(tr("Rename selected column", "Secili kolonu yeniden adlandir"));
        ImGui::InputTextWithHint(tr("Column name", "Kolon adi"), "COLUMN_NAME", &columnNameBuffer_);

        if (ImGui::Button(tr("Rename", "Yeniden adlandir"), ImVec2(120.0f, 0.0f))) {
            renameColumn(dataset, columnTargetIndex_, utf8ToWide(columnNameBuffer_));
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel", "Iptal"), ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ProtoEditorApp::drawBulkEditModal() {
    if (bulkEditModalOpen_) {
        ImGui::OpenPopup(tr("Bulk Column Tools###BulkColumnTools", "Toplu Kolon Araclari###BulkColumnTools"));
        bulkEditModalOpen_ = false;
    }

    if (!ImGui::BeginPopupModal(tr("Bulk Column Tools###BulkColumnTools", "Toplu Kolon Araclari###BulkColumnTools"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    DatasetState& dataset = activeDataset();
    const bool validColumn = dataset.selectedColumn >= 0 && dataset.selectedColumn < static_cast<int>(dataset.table.columnCount());
    const std::string columnName = validColumn ? wideToUtf8(dataset.table.header()[dataset.selectedColumn]) : trs("-", "-");

    ImGui::Text(tr("Selected column: %s", "Secili kolon: %s"), columnName.c_str());
    ImGui::Checkbox(tr("Apply only to visible/filtered rows", "Sadece gorunen/filtreli satirlara uygula"), &bulkVisibleRowsOnly_);
    ImGui::Separator();

    const char* operations[] = {
        tr("Set", "Ata"),
        tr("Replace", "Degistir"),
        tr("Prefix", "On ek"),
        tr("Suffix", "Son ek"),
        tr("Numeric add", "Sayisal ekle")
    };
    bulkEditMode_ = std::clamp(bulkEditMode_, 0, 4);
    ImGui::Combo(tr("Operation", "Islem"), &bulkEditMode_, operations, IM_ARRAYSIZE(operations));

    if (bulkEditMode_ == 0) {
        ImGui::TextUnformatted(tr("Bulk set", "Toplu deger ata"));
        ImGui::InputTextWithHint(tr("Value", "Deger"), "0", &bulkValueBuffer_);
        if (ImGui::Button(tr("Apply", "Uygula"), ImVec2(120.0f, 0.0f))) {
            bulkSetColumnValue(dataset, dataset.selectedColumn, utf8ToWide(bulkValueBuffer_), bulkVisibleRowsOnly_);
            ImGui::CloseCurrentPopup();
        }
    } else if (bulkEditMode_ == 1) {
        ImGui::TextUnformatted(tr("Bulk replace", "Toplu degistir"));
        ImGui::InputTextWithHint(tr("Find", "Bul"), tr("old value", "eski deger"), &bulkFindBuffer_);
        ImGui::InputTextWithHint(tr("Replace", "Degistir"), tr("new value", "yeni deger"), &bulkReplaceBuffer_);
        if (ImGui::Button(tr("Apply", "Uygula"), ImVec2(120.0f, 0.0f))) {
            bulkReplaceColumnValue(dataset, dataset.selectedColumn, utf8ToWide(bulkFindBuffer_), utf8ToWide(bulkReplaceBuffer_), bulkVisibleRowsOnly_);
            ImGui::CloseCurrentPopup();
        }
    } else {
        const bool numericMode = bulkEditMode_ == 4;
        ImGui::TextUnformatted(
            bulkEditMode_ == 2 ? tr("Bulk prefix", "Toplu on ek") :
            bulkEditMode_ == 3 ? tr("Bulk suffix", "Toplu son ek") :
                                 tr("Bulk numeric add", "Toplu sayisal ekle"));
        ImGui::InputTextWithHint(tr("Value", "Deger"), numericMode ? "10" : "IMMUNE_", &bulkValueBuffer_);
        if (ImGui::Button(tr("Apply", "Uygula"), ImVec2(120.0f, 0.0f))) {
            int changedCount = 0;
            const std::wstring bulkValue = utf8ToWide(bulkValueBuffer_);
            auto applyToRow = [&](size_t sourceRow) {
                if (dataset.selectedColumn < 0 || dataset.selectedColumn >= static_cast<int>(dataset.table.rows()[sourceRow].size())) {
                    return;
                }

                const std::wstring current = dataset.table.rows()[sourceRow][dataset.selectedColumn];
                std::wstring nextValue = current;
                if (bulkEditMode_ == 2) {
                    nextValue = bulkValue + current;
                } else if (bulkEditMode_ == 3) {
                    nextValue = current + bulkValue;
                } else if (isIntegerValue(current) && isIntegerValue(bulkValue)) {
                    nextValue = std::to_wstring(toNumber(current) + toNumber(bulkValue));
                } else {
                    return;
                }

                if (nextValue != current) {
                    setCellValue(dataset, sourceRow, dataset.selectedColumn, nextValue);
                    ++changedCount;
                }
            };

            if (bulkVisibleRowsOnly_) {
                for (size_t sourceRow : dataset.filteredRows) {
                    applyToRow(sourceRow);
                }
            } else {
                for (size_t sourceRow = 0; sourceRow < dataset.table.rows().size(); ++sourceRow) {
                    applyToRow(sourceRow);
                }
            }

            statusText_ = trs("Bulk operation applied to ", "Toplu islem uygulandi: ") + std::to_string(changedCount) + trs(" cells.", " hucre.");
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel", "Iptal"), ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ProtoEditorApp::drawColumnOrderModal() {
    if (columnOrderModalOpen_) {
        ImGui::OpenPopup(tr("Column Order Manager###ColumnOrderManager", "Kolon Siralama Yoneticisi###ColumnOrderManager"));
        columnOrderModalOpen_ = false;
    }

    if (!ImGui::BeginPopupModal(tr("Column Order Manager###ColumnOrderManager", "Kolon Siralama Yoneticisi###ColumnOrderManager"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    DatasetState& dataset = activeDataset();
    ImGui::TextUnformatted(tr("Permanent column order editor", "Kalici kolon sirasi duzenleme"));
    ImGui::TextUnformatted(tr("Drag a column with the mouse and drop it where you want.", "Kolonu mouse ile tutup istediginiz siraya birakin."));
    ImGui::Separator();

    int pendingMoveFrom = -1;
    int pendingMoveTo = -1;

    ImGui::BeginChild("ColumnOrderList", ImVec2(420.0f, 320.0f), true);
    for (size_t i = 0; i < dataset.table.header().size(); ++i) {
        const bool selected = dataset.selectedColumn == static_cast<int>(i);
        std::string label = std::to_string(static_cast<int>(i)) + " | " + wideToUtf8(dataset.table.header()[i]);
        if (ImGui::Selectable(label.c_str(), selected)) {
            dataset.selectedColumn = static_cast<int>(i);
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const int sourceIndex = static_cast<int>(i);
            ImGui::SetDragDropPayload("COLUMN_ORDER_INDEX", &sourceIndex, sizeof(sourceIndex));
            ImGui::Text(tr("Move: %s", "Tasi: %s"), wideToUtf8(dataset.table.header()[i]).c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COLUMN_ORDER_INDEX")) {
                const int sourceIndex = *static_cast<const int*>(payload->Data);
                const int targetIndex = static_cast<int>(i);
                if (sourceIndex != targetIndex) {
                    pendingMoveFrom = sourceIndex;
                    pendingMoveTo = targetIndex;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::EndChild();

    if (pendingMoveFrom >= 0 && pendingMoveTo >= 0) {
        moveColumn(dataset, pendingMoveFrom, pendingMoveTo);
    }

    const bool canMoveLeft = dataset.selectedColumn > 0;
    const bool canMoveRight = dataset.selectedColumn >= 0 && dataset.selectedColumn < static_cast<int>(dataset.table.columnCount()) - 1;

    if (ImGui::Button(tr("Move Up", "Yukari"), ImVec2(120.0f, 0.0f)) && canMoveLeft) {
        moveColumn(dataset, dataset.selectedColumn, dataset.selectedColumn - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Move Down", "Asagi"), ImVec2(120.0f, 0.0f)) && canMoveRight) {
        moveColumn(dataset, dataset.selectedColumn, dataset.selectedColumn + 1);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Close", "Kapat"), ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ProtoEditorApp::drawValidationPanel() {
    DatasetState& dataset = activeDataset();
    if (!dataset.loaded) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Validation Panel", "Dogrulama Paneli"), &showValidationPanel_)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button(tr("Refresh validation", "Dogrulamayi yenile"))) {
        refreshValidation(dataset);
    }
    ImGui::SameLine();
    ImGui::Text(tr("Issues: %d", "Sorunlar: %d"), static_cast<int>(dataset.validationIssues.size()));
    ImGui::Separator();

    for (const auto& issue : dataset.validationIssues) {
        ImVec4 color = issue.severity == "error" ? ImVec4(0.93f, 0.34f, 0.34f, 1.0f) : ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        const std::string label = wideToUtf8(issue.message);
        if (ImGui::Selectable(label.c_str(), false)) {
            if (issue.row >= 0) {
                auto it = std::find(dataset.filteredRows.begin(), dataset.filteredRows.end(), static_cast<size_t>(issue.row));
                if (it != dataset.filteredRows.end()) {
                    dataset.selectedRow = static_cast<int>(std::distance(dataset.filteredRows.begin(), it));
                    dataset.selectedColumn = issue.column;
                    requestScrollToSelection_ = true;
                }
            }
        }
        ImGui::PopStyleColor();
    }

    if (dataset.validationIssues.empty()) {
        ImGui::TextUnformatted(tr("No validation issue found.", "Dogrulama hatasi bulunmadi."));
    }

    ImGui::End();
}

void ProtoEditorApp::drawHistoryPanel() {
    DatasetState& dataset = activeDataset();
    if (!dataset.loaded) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("History Panel", "Gecmis Paneli"), &showHistoryPanel_)) {
        ImGui::End();
        return;
    }

    ImGui::Text(tr("Undo stack: %d", "Geri alma yigini: %d"), static_cast<int>(dataset.undoStack.size()));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text(tr("Redo stack: %d", "Yineleme yigini: %d"), static_cast<int>(dataset.redoStack.size()));
    ImGui::Separator();

    int rewindTarget = -1;
    for (int i = static_cast<int>(dataset.undoStack.size()) - 1; i >= 0; --i) {
        const auto& change = dataset.undoStack[static_cast<size_t>(i)];
        std::string label = "#" + std::to_string(i + 1) + " ";
        if (change.column >= 0 && change.column < static_cast<int>(dataset.table.columnCount())) {
            label += wideToUtf8(dataset.table.header()[change.column]);
        } else {
            label += tr("column", "kolon");
        }
        label += " | ";
        label += tr("row", "satir");
        label += " ";
        label += std::to_string(static_cast<int>(change.row + 1));
        label += " | ";
        label += wideToUtf8(change.before) + " -> " + wideToUtf8(change.after);

        if (ImGui::Selectable(label.c_str(), false)) {
            rewindTarget = i;
        }
    }

    if (rewindTarget >= 0) {
        while (static_cast<int>(dataset.undoStack.size()) > rewindTarget) {
            undo(dataset);
        }
    }

    ImGui::End();
}

void ProtoEditorApp::drawSearchPanel() {
    DatasetState& dataset = activeDataset();
    if (!dataset.loaded) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Search Panel", "Arama Paneli"), &showSearchPanel_)) {
        ImGui::End();
        return;
    }

    if (ImGui::InputTextWithHint(tr("Search everywhere", "Her yerde ara"), tr("VNUM, name, flag, column...", "VNUM, isim, flag, kolon..."), &dataset.searchEverywhereBuffer)) {
        refreshSearchMatches(dataset);
    }

    ImGui::InputTextWithHint(tr("Advanced filter column", "Gelismis filtre kolonu"), tr("TYPE, LEVEL, IMMUNE...", "TYPE, LEVEL, IMMUNE..."), &dataset.advancedFilterColumn);
    ImGui::InputTextWithHint(tr("Operator", "Operator"), tr("contains, equals, >=, <=", "contains, equals, >=, <="), &dataset.advancedFilterOperator);
    ImGui::InputTextWithHint(tr("Value", "Deger"), tr("ITEM_WEAPON, 75, STUN...", "ITEM_WEAPON, 75, STUN..."), &dataset.advancedFilterValue);

    if (ImGui::Button(tr("Apply advanced filter", "Gelismis filtreyi uygula"))) {
        applyAdvancedFilter(dataset);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Clear advanced filter", "Gelismis filtreyi temizle"))) {
        dataset.advancedFilterColumn.clear();
        dataset.advancedFilterOperator.clear();
        dataset.advancedFilterValue.clear();
        rebuildFilteredRows(dataset);
    }

    ImGui::Separator();
    ImGui::Text(tr("Matches: %d", "Eslesmeler: %d"), static_cast<int>(dataset.searchMatches.size()));
    for (size_t i = 0; i < dataset.searchMatches.size(); ++i) {
        const size_t sourceRow = dataset.searchMatches[i];
        const auto& row = dataset.table.rows()[sourceRow];
        std::string label = row.empty() ? std::to_string(static_cast<int>(sourceRow + 1)) : wideToUtf8(row[0]);
        if (row.size() > 1) {
            label += " | " + wideToUtf8(row[1]);
        }

        if (ImGui::Selectable(label.c_str(), dataset.searchMatchIndex == static_cast<int>(i))) {
            dataset.searchMatchIndex = static_cast<int>(i);
            auto it = std::find(dataset.filteredRows.begin(), dataset.filteredRows.end(), sourceRow);
            if (it != dataset.filteredRows.end()) {
                dataset.selectedRow = static_cast<int>(std::distance(dataset.filteredRows.begin(), it));
                dataset.selectedColumn = 0;
                requestScrollToSelection_ = true;
            }
        }
    }

    ImGui::End();
}

void ProtoEditorApp::drawComparePanel() {
    DatasetState& dataset = activeDataset();
    if (!dataset.loaded) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(500.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Compare Panel", "Karsilastirma Paneli"), &showComparePanel_)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button(tr("Load compare proto", "Karsilastirma protosu yukle"))) {
        const std::wstring comparePath = openFileDialog(
            trw(L"Open compare proto", L"Karsilastirma protosunu ac").c_str(),
            trw(L"Proto files (*.txt;*.tsv)\0*.txt;*.tsv\0All files (*.*)\0*.*\0", L"Proto dosyalari (*.txt;*.tsv)\0*.txt;*.tsv\0Tum dosyalar (*.*)\0*.*\0").c_str());
        if (!comparePath.empty() && dataset.compareTable.load(comparePath)) {
            dataset.compareLoaded = true;
            refreshCompare(dataset);
            statusText_ = trs("Compare dataset loaded.", "Karsilastirma verisi yuklendi.");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Refresh compare", "Karsilastirmayi yenile")) && dataset.compareLoaded) {
        refreshCompare(dataset);
    }
    ImGui::SameLine();
    ImGui::Checkbox(tr("Only changed rows", "Sadece degisen satirlar"), &dataset.compareOnlyChanged);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        refreshCompare(dataset);
    }

    if (!dataset.compareLoaded) {
        ImGui::TextUnformatted(tr("No compare proto loaded yet.", "Henuz karsilastirma protosu yuklenmedi."));
        ImGui::End();
        return;
    }

    ImGui::Separator();
    ImGui::Text(tr("Diff entries: %d", "Fark kayitlari: %d"), static_cast<int>(dataset.compareEntries.size()));

    static int selectedCompareEntry = -1;
    for (size_t i = 0; i < dataset.compareEntries.size(); ++i) {
        const auto& entry = dataset.compareEntries[i];
        std::string label = wideToUtf8(entry.key) + " | " + wideToUtf8(entry.type) + " | " +
            std::to_string(entry.changedCells) + " " + tr("cells", "hucre");
        if (ImGui::Selectable(label.c_str(), selectedCompareEntry == static_cast<int>(i))) {
            selectedCompareEntry = static_cast<int>(i);
        }
    }

    if (selectedCompareEntry >= 0 && selectedCompareEntry < static_cast<int>(dataset.compareEntries.size())) {
        const std::wstring key = dataset.compareEntries[static_cast<size_t>(selectedCompareEntry)].key;
        if (ImGui::Button(tr("Merge compare row into active", "Karsilastirma satirini aktife birlestir"))) {
            std::map<std::wstring, size_t> activeMap;
            std::map<std::wstring, size_t> compareMap;
            for (size_t i = 0; i < dataset.table.rows().size(); ++i) {
                if (!dataset.table.rows()[i].empty()) {
                    activeMap[dataset.table.rows()[i][0]] = i;
                }
            }
            for (size_t i = 0; i < dataset.compareTable.rows().size(); ++i) {
                if (!dataset.compareTable.rows()[i].empty()) {
                    compareMap[dataset.compareTable.rows()[i][0]] = i;
                }
            }

            const auto compareIt = compareMap.find(key);
            if (compareIt != compareMap.end()) {
                const auto& compareRow = dataset.compareTable.rows()[compareIt->second];
                const auto activeIt = activeMap.find(key);
                if (activeIt == activeMap.end()) {
                    dataset.table.rows().push_back(compareRow);
                    dataset.modified = true;
                } else {
                    for (size_t column = 0; column < compareRow.size() && column < dataset.table.rows()[activeIt->second].size(); ++column) {
                        if (dataset.table.rows()[activeIt->second][column] != compareRow[column]) {
                            setCellValue(dataset, activeIt->second, static_cast<int>(column), compareRow[column]);
                        }
                    }
                }
                rebuildFilteredRows(dataset);
                refreshCompare(dataset);
            }
        }
    }

    ImGui::End();
}

void ProtoEditorApp::drawLinkedNamesPanel() {
    DatasetState& dataset = activeDataset();
    if (!dataset.loaded) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 240.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Linked Names Panel", "Bagli Names Paneli"), &showLinkedNamesPanel_)) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox(tr("Enable linked editing", "Bagli duzenlemeyi etkinlestir"), &dataset.linkedEditing);
    ImGui::SameLine();
    if (ImGui::Button(tr("Reload linked names", "Bagli names dosyasini yenile"))) {
        loadLinkedNames(dataset);
    }

    ImGui::TextWrapped(tr("Names file: %s", "Names dosyasi: %s"), dataset.namesPath.empty() ? "-" : wideToUtf8(dataset.namesPath).c_str());
    if (!dataset.namesLoaded) {
        ImGui::TextUnformatted(tr("Linked names file could not be found for the current locale.", "Mevcut locale icin bagli names dosyasi bulunamadi."));
        ImGui::End();
        return;
    }

    syncLinkedNamesFromProto(dataset);

    if (dataset.selectedRow >= 0 && dataset.selectedRow < static_cast<int>(dataset.filteredRows.size())) {
        const size_t sourceRow = dataset.filteredRows[dataset.selectedRow];
        const std::wstring key = dataset.table.rows()[sourceRow].empty() ? L"" : dataset.table.rows()[sourceRow][0];
        int nameRowIndex = -1;
        for (size_t i = 0; i < dataset.namesTable.rows().size(); ++i) {
            const auto& row = dataset.namesTable.rows()[i];
            if (!row.empty() && row[0] == key) {
                nameRowIndex = static_cast<int>(i);
                break;
            }
        }

        static std::wstring currentKey;
        static std::string nameBuffer;
        if (currentKey != key) {
            currentKey = key;
            if (nameRowIndex >= 0 && dataset.namesTable.rows()[static_cast<size_t>(nameRowIndex)].size() > 1) {
                nameBuffer = wideToUtf8(dataset.namesTable.rows()[static_cast<size_t>(nameRowIndex)][1]);
            } else {
                nameBuffer.clear();
            }
        }

        ImGui::Separator();
        ImGui::Text(tr("Linked VNUM: %s", "Bagli VNUM: %s"), wideToUtf8(key).c_str());
        ImGui::InputTextWithHint(tr("Display name", "Gorunen isim"), tr("Sword +9", "Kilic +9"), &nameBuffer);

        if (ImGui::Button(tr("Apply linked name", "Bagli ismi uygula"))) {
            if (nameRowIndex < 0) {
                std::vector<std::wstring> row = { key, utf8ToWide(nameBuffer) };
                dataset.namesTable.rows().push_back(row);
            } else {
                auto& targetRow = dataset.namesTable.rows()[static_cast<size_t>(nameRowIndex)];
                if (targetRow.size() < 2) {
                    targetRow.resize(2);
                }
                targetRow[1] = utf8ToWide(nameBuffer);
            }
            if (!dataset.namesPath.empty()) {
                dataset.namesTable.saveSafe(dataset.namesPath);
            }
            statusText_ = trs("Linked name updated.", "Bagli isim guncellendi.");
        }
    }

    ImGui::End();
}

void ProtoEditorApp::drawWorkspacePanel() {
    DatasetState& dataset = activeDataset();
    if (!dataset.loaded) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Workspace Presets", "Calisma Alani Presetleri"), &showWorkspacePanel_)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button(tr("Item Editing", "Item Duzenleme"))) {
        applyWorkspacePreset(dataset, "item_editing");
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Mob Balancing", "Mob Dengeleme"))) {
        applyWorkspacePreset(dataset, "mob_balancing");
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Flags Editing", "Flag Duzenleme"))) {
        applyWorkspacePreset(dataset, "flags_editing");
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Compare Mode", "Karsilastirma Modu"))) {
        applyWorkspacePreset(dataset, "compare_mode");
    }

    ImGui::Separator();
    ImGui::Text(tr("Preset: %s", "Preset: %s"), dataset.workspacePreset.c_str());
    for (size_t column = 0; column < dataset.table.header().size(); ++column) {
        ImGui::PushID(static_cast<int>(column));
        bool hidden = dataset.hiddenColumns.find(static_cast<int>(column)) != dataset.hiddenColumns.end();
        bool pinned = dataset.pinnedColumns.find(static_cast<int>(column)) != dataset.pinnedColumns.end();
        std::string label = wideToUtf8(dataset.table.header()[column]);
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine(220.0f);
        if (ImGui::Checkbox(tr("Hide", "Gizle"), &hidden)) {
            if (hidden) {
                dataset.hiddenColumns.insert(static_cast<int>(column));
            } else {
                dataset.hiddenColumns.erase(static_cast<int>(column));
            }
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(tr("Pin", "Sabitle"), &pinned)) {
            if (pinned) {
                dataset.pinnedColumns.insert(static_cast<int>(column));
            } else {
                dataset.pinnedColumns.erase(static_cast<int>(column));
            }
        }
        ImGui::PopID();
    }

    if (ImGui::Button(tr("Reset visibility and pins", "Gorunurluk ve sabitlemeyi sifirla"))) {
        dataset.hiddenColumns.clear();
        dataset.pinnedColumns.clear();
    }

    ImGui::Separator();
    ImGui::Text(tr("Snapshots: %d", "Snapshotlar: %d"), static_cast<int>(dataset.snapshots.size()));
    static int selectedSnapshot = -1;
    for (size_t i = 0; i < dataset.snapshots.size(); ++i) {
        const auto& snapshot = dataset.snapshots[i];
        if (ImGui::Selectable(wideToUtf8(snapshot.timestamp).c_str(), selectedSnapshot == static_cast<int>(i))) {
            selectedSnapshot = static_cast<int>(i);
        }
    }

    if (selectedSnapshot >= 0 && selectedSnapshot < static_cast<int>(dataset.snapshots.size())) {
        if (ImGui::Button(tr("Restore selected snapshot", "Secili snapshot'i geri yukle"))) {
            TsvFile snapshotTable;
            if (snapshotTable.load(dataset.snapshots[static_cast<size_t>(selectedSnapshot)].path)) {
                dataset.table = snapshotTable;
                dataset.modified = true;
                rebuildFilteredRows(dataset);
                statusText_ = trs("Snapshot restored into editor.", "Snapshot editor icine geri yuklendi.");
            }
        }
    }

    ImGui::End();
}

void ProtoEditorApp::drawExportModal() {
    if (exportModalOpen_) {
        ImGui::OpenPopup(tr("Export Tools###ExportTools", "Disa Aktarma Araclari###ExportTools"));
        exportModalOpen_ = false;
    }

    if (!ImGui::BeginPopupModal(tr("Export Tools###ExportTools", "Disa Aktarma Araclari###ExportTools"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    DatasetState& dataset = activeDataset();
    const char* formats[] = { "csv", "sql", "diff" };
    int exportIndex = exportFormat_ == "sql" ? 1 : exportFormat_ == "diff" ? 2 : 0;
    if (ImGui::Combo(tr("Format", "Format"), &exportIndex, formats, IM_ARRAYSIZE(formats))) {
        exportFormat_ = formats[exportIndex];
    }

    ImGui::Checkbox(tr("Changed rows only", "Sadece degisen satirlar"), &dataset.changedRowsOnlyExport);

    if (ImGui::Button(tr("Export", "Disa aktar"), ImVec2(120.0f, 0.0f))) {
        std::wstring defaultPath = dataset.filePath;
        if (exportFormat_ == "csv") {
            defaultPath += L".csv";
        } else if (exportFormat_ == "sql") {
            defaultPath += L".sql";
        } else {
            defaultPath += L".diff.txt";
        }

        const std::wstring targetPath = saveFileDialog(
            trw(L"Export file", L"Dosyayi disa aktar").c_str(),
            defaultPath,
            trw(L"All files (*.*)\0*.*\0", L"Tum dosyalar (*.*)\0*.*\0").c_str());

        bool ok = false;
        if (!targetPath.empty()) {
            if (exportFormat_ == "csv") {
                ok = exportCsv(dataset, targetPath, dataset.changedRowsOnlyExport);
            } else if (exportFormat_ == "sql") {
                ok = exportSql(dataset, targetPath);
            } else {
                ok = exportDiff(dataset, targetPath);
            }
        }

        statusText_ = ok ? trs("Export completed.", "Disa aktarma tamamlandi.") : trs("Export cancelled or failed.", "Disa aktarma iptal edildi veya basarisiz oldu.");
        if (ok) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Close", "Kapat"), ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ProtoEditorApp::drawRulePresetPanel() {
    DatasetState& dataset = activeDataset();
    if (!dataset.loaded) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 220.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Rule Presets", "Kural Presetleri"), &showRulePresetPanel_)) {
        ImGui::End();
        return;
    }

    for (const auto& preset : dataset.rulePresets) {
        ImGui::PushID(preset.id.c_str());
        ImGui::TextWrapped("%s", wideToUtf8(preset.name).c_str());
        ImGui::TextDisabled("%s", wideToUtf8(preset.description).c_str());
        if (ImGui::Button(tr("Run preset", "Preseti calistir"), ImVec2(-1.0f, 0.0f))) {
            executeRulePreset(dataset, preset.id);
        }
        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::End();
}

void ProtoEditorApp::drawThemeBuilder() {
    ImGui::SetNextWindowSize(ImVec2(420.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Theme Builder", u8"Tema Olu\u015Fturucu"), &showThemeBuilder_)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(tr("Build your own theme preset.", u8"Kendi teman\u0131z\u0131 olu\u015Fturun."));
    ImGui::Separator();

    bool rgbTheme = preferences_.rgbTheme;
    if (ImGui::Checkbox(tr("Animated RGB accent", u8"Animasyonlu RGB vurgu"), &rgbTheme)) {
        preferences_.rgbTheme = rgbTheme;
        applyTheme();
        preferences_.save();
    }
    int rgbSpeed = preferences_.rgbSpeed;
    if (ImGui::SliderInt(tr("RGB speed", u8"RGB h\u0131z\u0131"), &rgbSpeed, 5, 100)) {
        preferences_.rgbSpeed = rgbSpeed;
        if (preferences_.rgbTheme) {
            applyTheme();
        }
        preferences_.save();
    }
    ImGui::Separator();

    ImVec4 accent = colorRefToImVec4(preferences_.customAccentColor);
    ImVec4 background = colorRefToImVec4(preferences_.customBackgroundColor);
    ImVec4 text = colorRefToImVec4(preferences_.customTextColor);

    bool changed = false;
    changed |= ImGui::ColorEdit3(tr("Accent", u8"Vurgu"), reinterpret_cast<float*>(&accent));
    changed |= ImGui::ColorEdit3(tr("Background", u8"Arka plan"), reinterpret_cast<float*>(&background));
    changed |= ImGui::ColorEdit3(tr("Text", u8"Yaz\u0131"), reinterpret_cast<float*>(&text));

    if (changed) {
        preferences_.customAccentColor = imVec4ToColorRef(accent);
        preferences_.customBackgroundColor = imVec4ToColorRef(background);
        preferences_.customTextColor = imVec4ToColorRef(text);
        preferences_.themePreset = ThemePreset::Custom;
        applyTheme();
        preferences_.save();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Quick base presets", u8"H\u0131zl\u0131 temel presetler"));
    if (ImGui::Button(tr("Start from Midnight", u8"Gece Mavisinden Ba\u015Fla"), ImVec2(-1.0f, 0.0f))) {
        preferences_.themePreset = ThemePreset::Midnight;
        preferences_.customAccentColor = RGB(46, 115, 209);
        preferences_.customBackgroundColor = RGB(20, 23, 28);
        preferences_.customTextColor = RGB(235, 239, 247);
        preferences_.themePreset = ThemePreset::Custom;
        applyTheme();
        preferences_.save();
    }
    if (ImGui::Button(tr("Start from Ivory", u8"Fildi\u015Finden Ba\u015Fla"), ImVec2(-1.0f, 0.0f))) {
        preferences_.customAccentColor = RGB(49, 104, 189);
        preferences_.customBackgroundColor = RGB(242, 242, 237);
        preferences_.customTextColor = RGB(28, 31, 36);
        preferences_.themePreset = ThemePreset::Custom;
        applyTheme();
        preferences_.save();
    }

    ImGui::Spacing();
    if (ImGui::Button(tr("Close", "Kapat"), ImVec2(120.0f, 0.0f))) {
        showThemeBuilder_ = false;
    }

    ImGui::End();
}

void ProtoEditorApp::refreshValidation(DatasetState& dataset) {
    dataset.validationIssues.clear();
    if (!dataset.loaded) {
        return;
    }

    std::map<std::wstring, size_t> seenKeys;
    for (size_t rowIndex = 0; rowIndex < dataset.table.rows().size(); ++rowIndex) {
        const auto& row = dataset.table.rows()[rowIndex];
        if (row.size() != dataset.table.header().size()) {
            dataset.validationIssues.push_back({ "error",
                trw(L"Column count mismatch.", L"Kolon sayisi uyusmuyor."),
                static_cast<int>(rowIndex), -1 });
        }

        if (row.empty() || row[0].empty()) {
            dataset.validationIssues.push_back({ "warning",
                trw(L"Primary key is empty.", L"Birincil anahtar bos."),
                static_cast<int>(rowIndex), 0 });
        } else {
            const auto it = seenKeys.find(row[0]);
            if (it != seenKeys.end()) {
                dataset.validationIssues.push_back({ "error",
                    trw(L"Duplicate VNUM / primary key: ", L"Tekrarlanan VNUM / anahtar: ") + row[0],
                    static_cast<int>(rowIndex), 0 });
            } else {
                seenKeys[row[0]] = rowIndex;
            }
        }

        for (size_t column = 0; column < row.size() && column < dataset.table.header().size(); ++column) {
            const std::wstring& header = dataset.table.header()[column];
            for (const auto& def : dataset.config.columns()) {
                if (def.name != header) {
                    continue;
                }

                if (def.type == L"int" && !isIntegerValue(row[column])) {
                    dataset.validationIssues.push_back({ "error",
                        trw(L"Expected integer value in ", L"Tamsayi bekleniyor: ") + header,
                        static_cast<int>(rowIndex), static_cast<int>(column) });
                }

                if (def.type == L"flag") {
                    const auto& allowed = dataset.config.flagList(def.flagSet);
                    const auto tokens = splitFlags(row[column]);
                    for (const auto& token : tokens) {
                        if (std::find(allowed.begin(), allowed.end(), token) == allowed.end()) {
                            dataset.validationIssues.push_back({ "warning",
                                trw(L"Unknown flag in ", L"Bilinmeyen flag: ") + header + L" -> " + token,
                                static_cast<int>(rowIndex), static_cast<int>(column) });
                        }
                    }
                }
                break;
            }
        }
    }
}

void ProtoEditorApp::refreshSnapshots(DatasetState& dataset) {
    dataset.snapshots.clear();
    if (dataset.filePath.empty()) {
        return;
    }

    const std::filesystem::path filePath(dataset.filePath);
    const std::filesystem::path backupDir = filePath.parent_path() / L"backups";
    if (!std::filesystem::exists(backupDir)) {
        return;
    }

    const std::wstring prefix = filePath.stem().wstring() + L"_";
    for (const auto& entry : std::filesystem::directory_iterator(backupDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::wstring fileName = entry.path().filename().wstring();
        if (fileName.find(prefix) != 0) {
            continue;
        }

        dataset.snapshots.push_back({ entry.path().wstring(), fileName });
    }

    std::sort(dataset.snapshots.begin(), dataset.snapshots.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.timestamp > rhs.timestamp;
    });
}

bool ProtoEditorApp::createSnapshot(DatasetState& dataset) {
    if (!dataset.loaded || dataset.filePath.empty()) {
        return false;
    }

    const std::filesystem::path filePath(dataset.filePath);
    const std::filesystem::path backupDir = filePath.parent_path() / L"backups";
    std::error_code ec;
    std::filesystem::create_directories(backupDir, ec);

    const std::filesystem::path snapshotPath =
        backupDir / (filePath.stem().wstring() + L"_" + buildTimestampString() + filePath.extension().wstring());

    const bool saved = dataset.table.save(snapshotPath.wstring());
    if (saved) {
        refreshSnapshots(dataset);
    }
    return saved;
}

bool ProtoEditorApp::exportCsv(const DatasetState& dataset, const std::wstring& path, bool changedRowsOnly) const {
    std::ofstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return false;
    }

    for (size_t column = 0; column < dataset.table.header().size(); ++column) {
        if (column > 0) {
            file << ",";
        }
        file << escapeCsvValue(dataset.table.header()[column]);
    }
    file << "\r\n";

    std::set<size_t> changedRows;
    for (const auto& cell : dataset.modifiedCells) {
        changedRows.insert(cell.first);
    }

    for (size_t rowIndex = 0; rowIndex < dataset.table.rows().size(); ++rowIndex) {
        if (changedRowsOnly && changedRows.find(rowIndex) == changedRows.end()) {
            continue;
        }

        const auto& row = dataset.table.rows()[rowIndex];
        for (size_t column = 0; column < row.size(); ++column) {
            if (column > 0) {
                file << ",";
            }
            file << escapeCsvValue(row[column]);
        }
        file << "\r\n";
    }

    return true;
}

bool ProtoEditorApp::exportDiff(const DatasetState& dataset, const std::wstring& path) const {
    std::ofstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return false;
    }

    file << "Proto Editor Tools Diff Export\r\n";
    for (const auto& entry : dataset.compareEntries) {
        file << wideToUtf8(entry.key) << "\t" << wideToUtf8(entry.type) << "\t" << entry.changedCells << "\r\n";
    }
    return true;
}

bool ProtoEditorApp::exportSql(const DatasetState& dataset, const std::wstring& path) const {
    std::ofstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return false;
    }

    const std::string tableName = wideToUtf8(dataset.title);
    for (const auto& row : dataset.table.rows()) {
        file << "INSERT INTO " << tableName << " (";
        for (size_t column = 0; column < dataset.table.header().size(); ++column) {
            if (column > 0) {
                file << ", ";
            }
            file << wideToUtf8(dataset.table.header()[column]);
        }
        file << ") VALUES (";
        for (size_t column = 0; column < row.size(); ++column) {
            if (column > 0) {
                file << ", ";
            }
            file << "'" << escapeSqlValue(row[column]) << "'";
        }
        file << ");\n";
    }
    return true;
}

void ProtoEditorApp::refreshCompare(DatasetState& dataset) {
    dataset.compareEntries.clear();
    if (!dataset.loaded || !dataset.compareLoaded) {
        return;
    }

    std::map<std::wstring, std::vector<std::wstring>> activeRows;
    std::map<std::wstring, std::vector<std::wstring>> compareRows;
    for (const auto& row : dataset.table.rows()) {
        if (!row.empty()) {
            activeRows[row[0]] = row;
        }
    }
    for (const auto& row : dataset.compareTable.rows()) {
        if (!row.empty()) {
            compareRows[row[0]] = row;
        }
    }

    for (const auto& [key, row] : activeRows) {
        auto compareIt = compareRows.find(key);
        if (compareIt == compareRows.end()) {
            dataset.compareEntries.push_back({ key, trw(L"Only in active", L"Sadece aktifte"), static_cast<int>(row.size()) });
            continue;
        }

        int changedCells = 0;
        const auto& other = compareIt->second;
        for (size_t i = 0; i < (std::min)(row.size(), other.size()); ++i) {
            if (row[i] != other[i]) {
                ++changedCells;
            }
        }
        if (!dataset.compareOnlyChanged || changedCells > 0) {
            dataset.compareEntries.push_back({ key, trw(L"Changed", L"Degismis"), changedCells });
        }
    }

    for (const auto& [key, row] : compareRows) {
        if (activeRows.find(key) == activeRows.end()) {
            dataset.compareEntries.push_back({ key, trw(L"Only in compare", L"Sadece karsilastirmada"), static_cast<int>(row.size()) });
        }
    }
}

void ProtoEditorApp::refreshSearchMatches(DatasetState& dataset) {
    dataset.searchMatches.clear();
    dataset.searchMatchIndex = -1;
    if (!dataset.loaded || dataset.searchEverywhereBuffer.empty()) {
        return;
    }

    const std::wstring needle = utf8ToWide(dataset.searchEverywhereBuffer);
    for (size_t rowIndex = 0; rowIndex < dataset.table.rows().size(); ++rowIndex) {
        const auto& row = dataset.table.rows()[rowIndex];
        bool matched = false;
        for (size_t column = 0; column < row.size(); ++column) {
            if (containsCaseInsensitive(row[column], needle) ||
                (column < dataset.table.header().size() && containsCaseInsensitive(dataset.table.header()[column], needle))) {
                matched = true;
                break;
            }
        }

        if (matched) {
            dataset.searchMatches.push_back(rowIndex);
        }
    }
}

void ProtoEditorApp::applyAdvancedFilter(DatasetState& dataset) {
    rebuildFilteredRows(dataset);
}

void ProtoEditorApp::loadLinkedNames(DatasetState& dataset) {
    dataset.namesLoaded = false;
    dataset.namesPath.clear();
    dataset.namesTable.clear();

    if (dataset.filePath.empty()) {
        return;
    }

    const std::filesystem::path baseDir = std::filesystem::path(dataset.filePath).parent_path();
    const std::wstring baseName = dataset.kind == DatasetKind::Item ? L"item_names" : L"mob_names";
    const std::wstring localeSuffix = preferences_.language == UiLanguage::Turkish ? L"tr" : L"en";
    const std::vector<std::filesystem::path> candidates = {
        baseDir / (baseName + L"_" + localeSuffix + L".txt"),
        baseDir / (baseName + L"_" + localeSuffix + L".tsv"),
        baseDir / (baseName + L".txt"),
        baseDir / (baseName + L".tsv")
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate) && dataset.namesTable.load(candidate.wstring())) {
            dataset.namesPath = candidate.wstring();
            dataset.namesLoaded = true;
            break;
        }
    }
}

void ProtoEditorApp::syncLinkedNamesFromProto(DatasetState& dataset) {
    if (!dataset.loaded || !dataset.namesLoaded) {
        return;
    }

    if (dataset.namesTable.header().empty()) {
        dataset.namesTable.header() = { L"VNUM", L"NAME" };
    } else if (dataset.namesTable.header().size() == 1) {
        dataset.namesTable.header().push_back(L"NAME");
    }

    std::map<std::wstring, size_t> existing;
    for (size_t i = 0; i < dataset.namesTable.rows().size(); ++i) {
        const auto& row = dataset.namesTable.rows()[i];
        if (!row.empty()) {
            existing[row[0]] = i;
        }
    }

    for (const auto& row : dataset.table.rows()) {
        if (row.empty() || row[0].empty()) {
            continue;
        }
        if (existing.find(row[0]) == existing.end()) {
            dataset.namesTable.rows().push_back({ row[0], L"" });
        }
    }
}

void ProtoEditorApp::applyWorkspacePreset(DatasetState& dataset, const std::string& presetId) {
    dataset.workspacePreset = presetId;
    dataset.hiddenColumns.clear();
    dataset.pinnedColumns.clear();

    auto findColumn = [&](const std::wstring& name) -> int {
        for (size_t i = 0; i < dataset.table.header().size(); ++i) {
            if (dataset.table.header()[i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    dataset.pinnedColumns.insert(0);
    if (presetId == "item_editing") {
        for (const auto& name : { L"NAME", L"TYPE", L"SUBTYPE", L"ANTI_FLAG", L"FLAG" }) {
            const int index = findColumn(name);
            if (index >= 0) {
                dataset.pinnedColumns.insert(index);
            }
        }
    } else if (presetId == "mob_balancing") {
        for (const auto& name : { L"NAME", L"LEVEL", L"MAX_HP", L"DAMAGE_MIN", L"DAMAGE_MAX" }) {
            const int index = findColumn(name);
            if (index >= 0) {
                dataset.pinnedColumns.insert(index);
            }
        }
    } else if (presetId == "flags_editing") {
        for (size_t i = 0; i < dataset.table.header().size(); ++i) {
            if (dataset.config.isFlagColumn(dataset.table.header()[i])) {
                dataset.pinnedColumns.insert(static_cast<int>(i));
            } else if (i > 1) {
                dataset.hiddenColumns.insert(static_cast<int>(i));
            }
        }
    } else if (presetId == "compare_mode") {
        for (const auto& name : { L"NAME", L"TYPE", L"LEVEL" }) {
            const int index = findColumn(name);
            if (index >= 0) {
                dataset.pinnedColumns.insert(index);
            }
        }
    }
}

void ProtoEditorApp::executeRulePreset(DatasetState& dataset, const std::string& presetId) {
    auto findColumn = [&](const std::wstring& name) -> int {
        for (size_t i = 0; i < dataset.table.header().size(); ++i) {
            if (dataset.table.header()[i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    if (presetId == "stun_immune") {
        const int immuneColumn = findColumn(L"IMMUNE");
        if (immuneColumn >= 0) {
            for (size_t sourceRow : dataset.filteredRows) {
                std::wstring current = dataset.table.rows()[sourceRow][immuneColumn];
                if (!containsCaseInsensitive(current, L"STUN")) {
                    current = current.empty() ? L"IMMUNE_STUN" : current + L" | IMMUNE_STUN";
                    setCellValue(dataset, sourceRow, immuneColumn, current);
                }
            }
        }
    } else if (presetId == "anti_wolfman") {
        const int antiFlagColumn = findColumn(L"ANTI_FLAG");
        if (antiFlagColumn >= 0) {
            for (size_t sourceRow : dataset.filteredRows) {
                std::wstring current = dataset.table.rows()[sourceRow][antiFlagColumn];
                if (!containsCaseInsensitive(current, L"ANTI_WOLFMAN")) {
                    current = current.empty() ? L"ANTI_WOLFMAN" : current + L" | ANTI_WOLFMAN";
                    setCellValue(dataset, sourceRow, antiFlagColumn, current);
                }
            }
        }
    } else if (presetId == "refine_defaults") {
        const int refineColumn = findColumn(L"REFINESET");
        if (refineColumn >= 0) {
            for (size_t sourceRow : dataset.filteredRows) {
                if (dataset.table.rows()[sourceRow][refineColumn].empty()) {
                    setCellValue(dataset, sourceRow, refineColumn, L"0");
                }
            }
        }
    }

    statusText_ = trs("Rule preset applied.", "Kural preseti uygulandi.");
}

void ProtoEditorApp::rebuildFilteredRows(DatasetState& dataset) {
    const int previousColumn = dataset.selectedColumn;
    size_t previousSourceRow = static_cast<size_t>(-1);
    if (dataset.selectedRow >= 0 && dataset.selectedRow < static_cast<int>(dataset.filteredRows.size())) {
        previousSourceRow = dataset.filteredRows[dataset.selectedRow];
    }

    dataset.filteredRows.clear();
    dataset.selectedRow = -1;

    if (!dataset.loaded) {
        return;
    }

    const std::wstring needle = utf8ToWide(dataset.filterText);
    const auto& rows = dataset.table.rows();
    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const auto& row = rows[rowIndex];
        bool match = needle.empty();
        if (!match) {
            if (dataset.filterColumn >= 0 && dataset.filterColumn < static_cast<int>(row.size())) {
                match = containsCaseInsensitive(row[dataset.filterColumn], needle);
            } else {
                for (const std::wstring& value : row) {
                    if (containsCaseInsensitive(value, needle)) {
                        match = true;
                        break;
                    }
                }
            }
        }

        if (match && !dataset.advancedFilterValue.empty()) {
            const std::wstring advancedColumn = utf8ToWide(dataset.advancedFilterColumn);
            const std::wstring advancedOperator = toLowerCopy(utf8ToWide(dataset.advancedFilterOperator));
            const std::wstring advancedValue = utf8ToWide(dataset.advancedFilterValue);

            int columnIndex = -1;
            for (size_t i = 0; i < dataset.table.header().size(); ++i) {
                if (dataset.table.header()[i] == advancedColumn) {
                    columnIndex = static_cast<int>(i);
                    break;
                }
            }

            if (columnIndex >= 0 && columnIndex < static_cast<int>(row.size())) {
                const std::wstring& candidate = row[static_cast<size_t>(columnIndex)];
                if (advancedOperator == L"equals" || advancedOperator == L"==") {
                    match = candidate == advancedValue;
                } else if (advancedOperator == L">=") {
                    match = toNumber(candidate) >= toNumber(advancedValue);
                } else if (advancedOperator == L"<=") {
                    match = toNumber(candidate) <= toNumber(advancedValue);
                } else {
                    match = containsCaseInsensitive(candidate, advancedValue);
                }
            }
        }

        if (match) {
            dataset.filteredRows.push_back(rowIndex);
        }
    }

    sortFilteredRows(dataset);

    if (previousSourceRow != static_cast<size_t>(-1)) {
        auto it = std::find(dataset.filteredRows.begin(), dataset.filteredRows.end(), previousSourceRow);
        if (it != dataset.filteredRows.end()) {
            dataset.selectedRow = static_cast<int>(std::distance(dataset.filteredRows.begin(), it));
            dataset.selectedColumn = previousColumn;
        }
    }

    refreshSearchMatches(dataset);
    refreshValidation(dataset);
    refreshCompare(dataset);
}

void ProtoEditorApp::sortFilteredRows(DatasetState& dataset) {
    if (!dataset.loaded || dataset.filteredRows.empty()) {
        return;
    }

    const int sortColumn = std::clamp(dataset.sortColumn, 0, static_cast<int>(dataset.table.columnCount()) - 1);
    std::stable_sort(dataset.filteredRows.begin(), dataset.filteredRows.end(), [&](size_t lhsIndex, size_t rhsIndex) {
        const auto& lhs = dataset.table.rows()[lhsIndex];
        const auto& rhs = dataset.table.rows()[rhsIndex];
        const std::wstring& a = lhs[sortColumn];
        const std::wstring& b = rhs[sortColumn];

        bool result = false;
        if (sortColumn == 0) {
            const long long av = toNumber(a);
            const long long bv = toNumber(b);
            result = (av == bv) ? a < b : av < bv;
        } else {
            result = toLowerCopy(a) < toLowerCopy(b);
        }

        if (a == b) {
            return false;
        }

        return dataset.sortAscending ? result : !result;
    });
}

void ProtoEditorApp::loadDataset(DatasetKind kind, const std::wstring& explicitPath) {
    DatasetState& dataset = datasetByKind(kind);
    const std::wstring path = explicitPath.empty() ? detectDatasetPath(kind) : explicitPath;
    if (path.empty()) {
        statusText_ = trs("No file selected.", "Dosya secilmedi.");
        return;
    }

    if (dataset.loaded && dataset.modified && explicitPath != dataset.filePath && !confirmDiscardChanges(dataset, trw(L"reload", L"yeniden yukleme").c_str())) {
        return;
    }

    dataset.table.clear();
    dataset.config = ProtoConfig();
    dataset.filteredRows.clear();
    dataset.modifiedCells.clear();
    dataset.loaded = false;
    dataset.modified = false;
    dataset.selectedColumn = -1;
    dataset.selectedRow = -1;
    dataset.filterText.clear();
    dataset.filterColumn = -1;
    dataset.sortColumn = 0;
    dataset.sortAscending = true;
    dataset.undoStack.clear();
    dataset.redoStack.clear();
    dataset.gotoRowBuffer.clear();
    dataset.validationIssues.clear();
    dataset.snapshots.clear();
    dataset.compareEntries.clear();
    dataset.hiddenColumns.clear();
    dataset.pinnedColumns.clear();
    dataset.searchEverywhereBuffer.clear();
    dataset.searchMatches.clear();
    dataset.searchMatchIndex = -1;
    dataset.advancedFilterColumn.clear();
    dataset.advancedFilterOperator.clear();
    dataset.advancedFilterValue.clear();
    dataset.compareLoaded = false;
    dataset.compareTable.clear();
    dataset.namesLoaded = false;
    dataset.namesPath.clear();
    dataset.namesTable.clear();
    dataset.linkedEditing = false;
    dataset.changedRowsOnlyExport = false;
    dataset.workspacePreset = "default";

    dataset.configPath = findConfigPath(kind);
    if (!dataset.table.load(path)) {
        statusText_ = trs("Failed to load file: ", "Dosya yuklenemedi: ") + wideToUtf8(path);
        MessageBoxW(hwnd_, dataset.table.lastError().c_str(), trw(L"Load Error", L"Yukleme Hatasi").c_str(), MB_OK | MB_ICONERROR);
        return;
    }

    bool configLoaded = false;
    if (!dataset.configPath.empty()) {
        configLoaded = dataset.config.load(dataset.configPath);
    }
    if (!configLoaded) {
        configLoaded = dataset.config.loadFromText(kind == DatasetKind::Item
            ? ProtoConfig::embeddedItemConfig()
            : ProtoConfig::embeddedMobConfig());
        if (configLoaded) {
            dataset.configPath = trw(L"[embedded default config]", L"[gömülü varsayılan config]");
        }
    }

    dataset.filePath = path;
    dataset.loaded = true;
    dataset.rulePresets = {
        { "stun_immune", trw(L"Add stun immune", L"Stun immune ekle"), trw(L"Adds IMMUNE_STUN to filtered rows.", L"Filtreli satirlara IMMUNE_STUN ekler.") },
        { "anti_wolfman", trw(L"Add anti wolfman", L"Anti wolfman ekle"), trw(L"Adds ANTI_WOLFMAN to filtered rows.", L"Filtreli satirlara ANTI_WOLFMAN ekler.") },
        { "refine_defaults", trw(L"Fill refine defaults", L"Refine varsayilanlarini doldur"), trw(L"Fills empty REFINESET values with 0.", L"Bos REFINESET degerlerini 0 ile doldurur.") }
    };
    loadLinkedNames(dataset);
    refreshSnapshots(dataset);
    rebuildFilteredRows(dataset);
    activeDatasetIndex_ = kind == DatasetKind::Item ? 0 : 1;
    statusText_ = trs("Loaded: ", "Yuklendi: ") + wideToUtf8(path);
}

bool ProtoEditorApp::saveDataset(DatasetState& dataset, bool saveAs) {
    if (!dataset.loaded) {
        return false;
    }

    std::wstring targetPath = dataset.filePath;
    if (saveAs || targetPath.empty()) {
        targetPath = saveFileDialog(trw(L"Save proto file", L"Proto dosyasini kaydet").c_str(), dataset.filePath, trw(L"Proto files (*.txt;*.tsv)\0*.txt;*.tsv\0All files (*.*)\0*.*\0", L"Proto dosyalari (*.txt;*.tsv)\0*.txt;*.tsv\0Tum dosyalar (*.*)\0*.*\0").c_str());
        if (targetPath.empty()) {
            return false;
        }
    }

    if (!saveAs && !dataset.filePath.empty()) {
        createSnapshot(dataset);
    }

    if (!dataset.table.saveSafe(targetPath)) {
        MessageBoxW(hwnd_, dataset.table.lastError().c_str(), trw(L"Save Error", L"Kaydetme Hatasi").c_str(), MB_OK | MB_ICONERROR);
        statusText_ = trs("Save failed: ", "Kaydetme basarisiz: ") + wideToUtf8(targetPath);
        return false;
    }

    dataset.filePath = targetPath;
    if (dataset.namesLoaded && !dataset.namesPath.empty()) {
        dataset.namesTable.saveSafe(dataset.namesPath);
    }
    dataset.modified = false;
    clearModified(dataset);
    refreshSnapshots(dataset);
    statusText_ = trs("Saved: ", "Kaydedildi: ") + wideToUtf8(targetPath);
    return true;
}

void ProtoEditorApp::setCellValue(DatasetState& dataset, size_t sourceRow, int column, const std::wstring& value) {
    if (sourceRow >= dataset.table.rows().size() || column < 0 || column >= static_cast<int>(dataset.table.rows()[sourceRow].size())) {
        return;
    }

    std::wstring& current = dataset.table.rows()[sourceRow][column];
    if (current == value) {
        return;
    }

    DatasetState::CellChange change;
    change.row = sourceRow;
    change.column = column;
    change.before = current;
    change.after = value;
    dataset.undoStack.push_back(change);
    dataset.redoStack.clear();

    current = value;
    markModified(dataset, sourceRow, column);
    rebuildFilteredRows(dataset);

    if (dataset.linkedEditing && dataset.namesLoaded && column == 0) {
        syncLinkedNamesFromProto(dataset);
    }
    statusText_ = trs("Cell updated.", "Hucre guncellendi.");
}

void ProtoEditorApp::applyCellChange(DatasetState& dataset, const DatasetState::CellChange& change, bool useAfterValue, bool trackReverse) {
    if (change.row >= dataset.table.rows().size() || change.column < 0 || change.column >= static_cast<int>(dataset.table.rows()[change.row].size())) {
        return;
    }

    auto& target = dataset.table.rows()[change.row][change.column];
    const std::wstring previousValue = target;
    target = useAfterValue ? change.after : change.before;

    if (trackReverse) {
        DatasetState::CellChange reverse = change;
        reverse.before = previousValue;
        reverse.after = target;
        if (useAfterValue) {
            dataset.undoStack.push_back(reverse);
        } else {
            dataset.redoStack.push_back(reverse);
        }
    }

    markModified(dataset, change.row, change.column);
    rebuildFilteredRows(dataset);
}

void ProtoEditorApp::openCellEditor(DatasetState& dataset, size_t sourceRow, int column) {
    if (sourceRow >= dataset.table.rows().size() || column < 0 || column >= static_cast<int>(dataset.table.rows()[sourceRow].size())) {
        return;
    }

    editSourceRow_ = sourceRow;
    editColumn_ = column;
    editBufferWide_ = dataset.table.rows()[sourceRow][column];
    editBuffer_ = wideToUtf8(editBufferWide_);
    editModalOpen_ = true;
}

void ProtoEditorApp::openFlagEditor(DatasetState& dataset, size_t sourceRow, int column) {
    if (sourceRow >= dataset.table.rows().size() || column < 0 || column >= static_cast<int>(dataset.table.rows()[sourceRow].size())) {
        return;
    }

    const std::wstring& columnName = dataset.table.header()[column];
    if (!dataset.config.isFlagColumn(columnName)) {
        openCellEditor(dataset, sourceRow, column);
        return;
    }

    flagSourceRow_ = sourceRow;
    flagColumn_ = column;
    flagOriginalValue_ = dataset.table.rows()[sourceRow][column];
    flagSelection_ = splitFlags(flagOriginalValue_);
    flagSearch_.clear();
    flagModalOpen_ = true;
}

void ProtoEditorApp::duplicateSelectedRow(DatasetState& dataset) {
    if (!dataset.loaded || dataset.selectedRow < 0 || dataset.selectedRow >= static_cast<int>(dataset.filteredRows.size())) {
        return;
    }

    const size_t sourceRow = dataset.filteredRows[dataset.selectedRow];
    auto rowCopy = dataset.table.rows()[sourceRow];
    dataset.table.rows().insert(dataset.table.rows().begin() + static_cast<std::ptrdiff_t>(sourceRow + 1), rowCopy);
    dataset.modified = true;
    rebuildFilteredRows(dataset);

    auto it = std::find(dataset.filteredRows.begin(), dataset.filteredRows.end(), sourceRow + 1);
    if (it != dataset.filteredRows.end()) {
        dataset.selectedRow = static_cast<int>(std::distance(dataset.filteredRows.begin(), it));
    }

    statusText_ = trs("Row duplicated.", "Satir kopyalandi.");
}

void ProtoEditorApp::insertEmptyRow(DatasetState& dataset) {
    if (!dataset.loaded) {
        return;
    }

    std::vector<std::wstring> newRow(dataset.table.columnCount(), L"");
    size_t insertAt = dataset.table.rows().size();
    if (dataset.selectedRow >= 0 && dataset.selectedRow < static_cast<int>(dataset.filteredRows.size())) {
        insertAt = dataset.filteredRows[dataset.selectedRow] + 1;
    }

    dataset.table.rows().insert(dataset.table.rows().begin() + static_cast<std::ptrdiff_t>(insertAt), newRow);
    dataset.modified = true;
    rebuildFilteredRows(dataset);

    auto it = std::find(dataset.filteredRows.begin(), dataset.filteredRows.end(), insertAt);
    if (it != dataset.filteredRows.end()) {
        dataset.selectedRow = static_cast<int>(std::distance(dataset.filteredRows.begin(), it));
        dataset.selectedColumn = 0;
    }

    statusText_ = trs("Empty row inserted.", "Bos satir eklendi.");
}

void ProtoEditorApp::deleteSelectedRow(DatasetState& dataset) {
    if (!dataset.loaded || dataset.selectedRow < 0 || dataset.selectedRow >= static_cast<int>(dataset.filteredRows.size())) {
        return;
    }

    const size_t sourceRow = dataset.filteredRows[dataset.selectedRow];
    dataset.table.rows().erase(dataset.table.rows().begin() + static_cast<std::ptrdiff_t>(sourceRow));
    dataset.modified = true;
    dataset.selectedRow = -1;
    dataset.selectedColumn = -1;
    rebuildFilteredRows(dataset);
    statusText_ = trs("Row deleted.", "Satir silindi.");
}

void ProtoEditorApp::gotoRowByFirstColumn(DatasetState& dataset, const std::wstring& key) {
    if (!dataset.loaded || key.empty() || dataset.table.rows().empty()) {
        return;
    }

    for (size_t rowIndex = 0; rowIndex < dataset.table.rows().size(); ++rowIndex) {
        const auto& row = dataset.table.rows()[rowIndex];
        if (!row.empty() && row[0] == key) {
            auto it = std::find(dataset.filteredRows.begin(), dataset.filteredRows.end(), rowIndex);
            if (it == dataset.filteredRows.end()) {
                dataset.filterText.clear();
                dataset.filterColumn = -1;
                rebuildFilteredRows(dataset);
                it = std::find(dataset.filteredRows.begin(), dataset.filteredRows.end(), rowIndex);
            }

            if (it != dataset.filteredRows.end()) {
                dataset.selectedRow = static_cast<int>(std::distance(dataset.filteredRows.begin(), it));
                dataset.selectedColumn = 0;
                requestScrollToSelection_ = true;
                statusText_ = trs("Row found.", "Satir bulundu.");
                return;
            }
        }
    }

    statusText_ = trs("Requested row not found.", "Istenen satir bulunamadi.");
}

void ProtoEditorApp::undo(DatasetState& dataset) {
    if (dataset.undoStack.empty()) {
        return;
    }

    const DatasetState::CellChange change = dataset.undoStack.back();
    dataset.undoStack.pop_back();
    dataset.redoStack.push_back(change);
    applyCellChange(dataset, change, false, false);
    statusText_ = trs("Undo applied.", "Geri alma uygulandi.");
}

void ProtoEditorApp::redo(DatasetState& dataset) {
    if (dataset.redoStack.empty()) {
        return;
    }

    const DatasetState::CellChange change = dataset.redoStack.back();
    dataset.redoStack.pop_back();
    dataset.undoStack.push_back(change);
    applyCellChange(dataset, change, true, false);
    statusText_ = trs("Redo applied.", "Yineleme uygulandi.");
}

void ProtoEditorApp::addColumn(DatasetState& dataset, const std::wstring& columnName, int insertAfter) {
    if (!dataset.loaded) {
        return;
    }

    std::wstring finalName = columnName;
    if (finalName.empty()) {
        finalName = L"NEW_COLUMN";
    }

    int insertIndex = static_cast<int>(dataset.table.header().size());
    if (insertAfter >= 0 && insertAfter < static_cast<int>(dataset.table.header().size())) {
        insertIndex = insertAfter + 1;
    }

    dataset.table.header().insert(dataset.table.header().begin() + insertIndex, finalName);
    for (auto& row : dataset.table.rows()) {
        row.insert(row.begin() + insertIndex, L"0");
    }

    std::set<int> updatedHidden;
    for (int column : dataset.hiddenColumns) {
        updatedHidden.insert(column >= insertIndex ? column + 1 : column);
    }
    dataset.hiddenColumns = std::move(updatedHidden);

    std::set<int> updatedPinned;
    for (int column : dataset.pinnedColumns) {
        updatedPinned.insert(column >= insertIndex ? column + 1 : column);
    }
    dataset.pinnedColumns = std::move(updatedPinned);

    if (dataset.filterColumn >= insertIndex) {
        dataset.filterColumn++;
    }

    dataset.modified = true;
    dataset.selectedColumn = insertIndex;
    rebuildFilteredRows(dataset);
    statusText_ = trs("Column added.", "Kolon eklendi.");
}

void ProtoEditorApp::deleteColumn(DatasetState& dataset, int columnIndex) {
    if (!dataset.loaded || columnIndex < 0 || columnIndex >= static_cast<int>(dataset.table.header().size())) {
        return;
    }

    dataset.table.header().erase(dataset.table.header().begin() + columnIndex);
    for (auto& row : dataset.table.rows()) {
        if (columnIndex < static_cast<int>(row.size())) {
            row.erase(row.begin() + columnIndex);
        }
    }

    std::set<std::pair<size_t, int>> updatedModified;
    for (const auto& entry : dataset.modifiedCells) {
        if (entry.second == columnIndex) {
            continue;
        }
        if (entry.second > columnIndex) {
            updatedModified.insert({ entry.first, entry.second - 1 });
        } else {
            updatedModified.insert(entry);
        }
    }
    dataset.modifiedCells = std::move(updatedModified);

    std::set<int> updatedHidden;
    for (int column : dataset.hiddenColumns) {
        if (column == columnIndex) {
            continue;
        }
        updatedHidden.insert(column > columnIndex ? column - 1 : column);
    }
    dataset.hiddenColumns = std::move(updatedHidden);

    std::set<int> updatedPinned;
    for (int column : dataset.pinnedColumns) {
        if (column == columnIndex) {
            continue;
        }
        updatedPinned.insert(column > columnIndex ? column - 1 : column);
    }
    dataset.pinnedColumns = std::move(updatedPinned);

    if (dataset.filterColumn == columnIndex) {
        dataset.filterColumn = -1;
        dataset.filterText.clear();
    } else if (dataset.filterColumn > columnIndex) {
        dataset.filterColumn--;
    }

    dataset.modified = true;
    if (dataset.table.header().empty()) {
        dataset.selectedColumn = -1;
    } else if (dataset.selectedColumn >= static_cast<int>(dataset.table.header().size())) {
        dataset.selectedColumn = static_cast<int>(dataset.table.header().size()) - 1;
    }

    rebuildFilteredRows(dataset);
    statusText_ = trs("Column deleted.", "Kolon silindi.");
}

void ProtoEditorApp::renameColumn(DatasetState& dataset, int columnIndex, const std::wstring& newName) {
    if (!dataset.loaded || columnIndex < 0 || columnIndex >= static_cast<int>(dataset.table.header().size())) {
        return;
    }

    std::wstring finalName = newName;
    if (finalName.empty()) {
        finalName = L"RENAMED_COLUMN";
    }

    if (dataset.table.header()[columnIndex] == finalName) {
        return;
    }

    dataset.table.header()[columnIndex] = finalName;
    dataset.modified = true;
    statusText_ = trs("Column renamed.", "Kolon yeniden adlandirildi.");
}

void ProtoEditorApp::moveColumn(DatasetState& dataset, int fromIndex, int toIndex) {
    if (!dataset.loaded) {
        return;
    }

    const int columnCount = static_cast<int>(dataset.table.header().size());
    if (fromIndex < 0 || toIndex < 0 || fromIndex >= columnCount || toIndex >= columnCount || fromIndex == toIndex) {
        return;
    }

    auto& header = dataset.table.header();
    std::wstring movedHeader = header[fromIndex];
    header.erase(header.begin() + fromIndex);
    header.insert(header.begin() + toIndex, movedHeader);

    for (auto& row : dataset.table.rows()) {
        if (fromIndex < static_cast<int>(row.size()) && toIndex < static_cast<int>(row.size())) {
            std::wstring movedValue = row[fromIndex];
            row.erase(row.begin() + fromIndex);
            row.insert(row.begin() + toIndex, movedValue);
        }
    }

    std::set<std::pair<size_t, int>> updatedModified;
    for (const auto& entry : dataset.modifiedCells) {
        int col = entry.second;
        if (col == fromIndex) {
            col = toIndex;
        } else if (fromIndex < toIndex) {
            if (col > fromIndex && col <= toIndex) {
                col--;
            }
        } else {
            if (col >= toIndex && col < fromIndex) {
                col++;
            }
        }
        updatedModified.insert({ entry.first, col });
    }
    dataset.modifiedCells = std::move(updatedModified);

    auto remapColumnIndex = [&](int column) -> int {
        if (column == fromIndex) {
            return toIndex;
        }
        if (fromIndex < toIndex) {
            if (column > fromIndex && column <= toIndex) {
                return column - 1;
            }
        } else {
            if (column >= toIndex && column < fromIndex) {
                return column + 1;
            }
        }
        return column;
    };

    std::set<int> updatedHidden;
    for (int column : dataset.hiddenColumns) {
        updatedHidden.insert(remapColumnIndex(column));
    }
    dataset.hiddenColumns = std::move(updatedHidden);

    std::set<int> updatedPinned;
    for (int column : dataset.pinnedColumns) {
        updatedPinned.insert(remapColumnIndex(column));
    }
    dataset.pinnedColumns = std::move(updatedPinned);

    if (dataset.filterColumn == fromIndex) {
        dataset.filterColumn = toIndex;
    } else if (fromIndex < toIndex) {
        if (dataset.filterColumn > fromIndex && dataset.filterColumn <= toIndex) {
            dataset.filterColumn--;
        }
    } else {
        if (dataset.filterColumn >= toIndex && dataset.filterColumn < fromIndex) {
            dataset.filterColumn++;
        }
    }

    dataset.selectedColumn = toIndex;
    dataset.modified = true;
    rebuildFilteredRows(dataset);
    statusText_ = trs("Column moved.", "Kolon tasindi.");
}

void ProtoEditorApp::bulkSetColumnValue(DatasetState& dataset, int columnIndex, const std::wstring& value, bool visibleRowsOnly) {
    if (!dataset.loaded || columnIndex < 0 || columnIndex >= static_cast<int>(dataset.table.columnCount())) {
        return;
    }

    const std::wstring finalValue = value.empty() ? L"0" : value;
    int changedCount = 0;

    if (visibleRowsOnly) {
        for (size_t sourceRow : dataset.filteredRows) {
            if (columnIndex < static_cast<int>(dataset.table.rows()[sourceRow].size()) && dataset.table.rows()[sourceRow][columnIndex] != finalValue) {
                setCellValue(dataset, sourceRow, columnIndex, finalValue);
                ++changedCount;
            }
        }
    } else {
        for (size_t sourceRow = 0; sourceRow < dataset.table.rows().size(); ++sourceRow) {
            if (columnIndex < static_cast<int>(dataset.table.rows()[sourceRow].size()) && dataset.table.rows()[sourceRow][columnIndex] != finalValue) {
                setCellValue(dataset, sourceRow, columnIndex, finalValue);
                ++changedCount;
            }
        }
    }

    statusText_ = trs("Bulk set applied to ", "Toplu deger atama uygulandi: ") + std::to_string(changedCount) + trs(" cells.", " hucre.");
}

void ProtoEditorApp::bulkReplaceColumnValue(DatasetState& dataset, int columnIndex, const std::wstring& findValue, const std::wstring& replaceValue, bool visibleRowsOnly) {
    if (!dataset.loaded || columnIndex < 0 || columnIndex >= static_cast<int>(dataset.table.columnCount()) || findValue.empty()) {
        return;
    }

    int changedCount = 0;

    auto replaceInRow = [&](size_t sourceRow) {
        if (columnIndex >= static_cast<int>(dataset.table.rows()[sourceRow].size())) {
            return;
        }

        const std::wstring& current = dataset.table.rows()[sourceRow][columnIndex];
        if (current.find(findValue) == std::wstring::npos) {
            return;
        }

        std::wstring updated = current;
        size_t pos = 0;
        while ((pos = updated.find(findValue, pos)) != std::wstring::npos) {
            updated.replace(pos, findValue.length(), replaceValue);
            pos += replaceValue.length();
        }

        if (updated != current) {
            setCellValue(dataset, sourceRow, columnIndex, updated);
            ++changedCount;
        }
    };

    if (visibleRowsOnly) {
        for (size_t sourceRow : dataset.filteredRows) {
            replaceInRow(sourceRow);
        }
    } else {
        for (size_t sourceRow = 0; sourceRow < dataset.table.rows().size(); ++sourceRow) {
            replaceInRow(sourceRow);
        }
    }

    statusText_ = trs("Bulk replace applied to ", "Toplu degistirme uygulandi: ") + std::to_string(changedCount) + trs(" cells.", " hucre.");
}

bool ProtoEditorApp::confirmDiscardChanges(const DatasetState& dataset, const wchar_t* action) const {
    if (!dataset.modified) {
        return true;
    }

    std::wstring message = trw(L"Unsaved changes will be lost.\nDo you want to ", L"Kaydedilmemis degisiklikler kaybolacak.\nSunu yapmak istiyor musunuz: ");
    message += action;
    message += trw(L"?", L"?");
    return MessageBoxW(hwnd_, message.c_str(), trw(L"Unsaved Changes", L"Kaydedilmemis Degisiklikler").c_str(), MB_YESNO | MB_ICONWARNING) == IDYES;
}

void ProtoEditorApp::markModified(DatasetState& dataset, size_t sourceRow, int column) {
    dataset.modified = true;
    dataset.modifiedCells.insert({ sourceRow, column });
}

void ProtoEditorApp::clearModified(DatasetState& dataset) {
    dataset.modifiedCells.clear();
}

std::wstring ProtoEditorApp::findConfigPath(DatasetKind kind) const {
    const wchar_t* fileName = kind == DatasetKind::Item ? L"item_proto_conf.yaml" : L"mob_proto_conf.yaml";
    std::vector<std::filesystem::path> candidates;

    const std::filesystem::path current = std::filesystem::current_path();
    candidates.push_back(current / fileName);
    candidates.push_back(current.parent_path() / fileName);
    candidates.push_back(current.parent_path().parent_path() / fileName);

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir(exePath);
    exeDir = exeDir.parent_path();
    candidates.push_back(exeDir / fileName);
    candidates.push_back(exeDir.parent_path() / fileName);
    candidates.push_back(exeDir.parent_path().parent_path() / fileName);

    for (const auto& path : candidates) {
        if (!path.empty() && std::filesystem::exists(path)) {
            return path.wstring();
        }
    }

    return {};
}

std::wstring ProtoEditorApp::detectDatasetPath(DatasetKind kind) const {
    const wchar_t* fileName = kind == DatasetKind::Item ? L"item_proto.txt" : L"mob_proto.txt";
    const std::filesystem::path current = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates = {
        current / fileName,
        current / L"cpp" / L"build" / L"Release" / fileName,
        current / L"build" / L"Release" / fileName
    };

    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path.wstring();
        }
    }

    return {};
}

std::wstring ProtoEditorApp::openFileDialog(const wchar_t* title, const wchar_t* filter) const {
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&ofn) ? std::wstring(fileName) : std::wstring();
}

std::wstring ProtoEditorApp::saveFileDialog(const wchar_t* title, const std::wstring& defaultPath, const wchar_t* filter) const {
    wchar_t fileName[MAX_PATH] = {};
    if (!defaultPath.empty()) {
        wcsncpy_s(fileName, defaultPath.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    return GetSaveFileNameW(&ofn) ? std::wstring(fileName) : std::wstring();
}

LRESULT CALLBACK ProtoEditorApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }

    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = reinterpret_cast<ProtoEditorApp*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        return TRUE;
    }

    auto* app = reinterpret_cast<ProtoEditorApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app ? app->handleMessage(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ProtoEditorApp::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (device_ != nullptr && wParam != SIZE_MINIMIZED) {
            cleanupRenderTarget();
            swapChain_->ResizeBuffers(0, static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)), DXGI_FORMAT_UNKNOWN, 0);
            createRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_CLOSE:
        for (const DatasetState& dataset : datasets_) {
            if (dataset.modified && !confirmDiscardChanges(dataset, trw(L"exit", L"cikis").c_str())) {
                return 0;
            }
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        running_ = false;
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (hasActiveDataset()) {
            DatasetState& dataset = activeDataset();
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'S') {
                saveDataset(dataset, false);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Z') {
                undo(dataset);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Y') {
                redo(dataset);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'G') {
                gotoRowModalOpen_ = true;
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'N') {
                insertEmptyRow(dataset);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'D') {
                duplicateSelectedRow(dataset);
                return 0;
            }
            if (wParam == VK_DELETE) {
                deleteSelectedRow(dataset);
                return 0;
            }
        }
        break;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

cmake_minimum_required(VERSION 3.16)
project(ProtoEditor LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FetchContent_Declare(
  imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${imgui_SOURCE_DIR}/misc/cpp
)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)

if(WIN32)
  add_executable(ProtoEditor WIN32
    src/main.cpp
    src/ProtoEditorApp.cpp
    src/TsvFile.cpp
    src/ProtoConfig.cpp
    src/AppPreferences.cpp
    src/res/app.rc
  )
  target_compile_definitions(ProtoEditor PRIVATE UNICODE _UNICODE)
  target_compile_options(ProtoEditor PRIVATE /utf-8)
  target_include_directories(ProtoEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(ProtoEditor PRIVATE
    imgui
    comctl32
    d3d11
    dxgi
    d3dcompiler
    Comdlg32
  )
endif()

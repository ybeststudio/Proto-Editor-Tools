# Proto Editor Tools | by Best Studio

Modern, native and production-focused `Metin2` proto editing suite built with `C++`, `Dear ImGui` and `DirectX 11`.

`Proto Editor Tools` is designed for developers who work directly with:

- `item_proto.txt`
- `mob_proto.txt`
- linked `item_names` / `mob_names`
- flag-heavy Metin2 data flows
- large TSV based balancing and maintenance work

---

## Overview

This project is not a generic table editor.

It is a dedicated `Metin2` tool built to make proto workflows faster, safer and more professional with:

- native desktop performance
- encoding-aware file handling
- smart flag editing
- bulk edit operations
- compare and validation tooling
- linked names workflow
- workspace presets
- snapshot backup protection
- embedded default proto config support
- modern theming and RGB accent support

---

## Feature Highlights

### Core Editing

- direct cell editing
- smart flag editor
- row insert / duplicate / delete
- go to VNUM
- column add / rename / delete / reorder
- column visibility and pin workflow

### Mass Editing

- bulk set
- bulk replace
- bulk prefix
- bulk suffix
- numeric add

### Safety

- temp-file safe save flow
- snapshot backup generation
- validation panel
- undo / redo
- history panel
- modified cell tracking

### Advanced Tooling

- proto compare panel
- merge compare row into active dataset
- linked `item_names` / `mob_names`
- global search
- advanced filtering
- export support
- rule presets

### UI / UX

- Dear ImGui desktop interface
- Turkish / English UI support
- 5 built-in theme presets
- theme builder
- animated RGB accent mode
- integrated application icon

---

## Built For Metin2

`Proto Editor Tools` is intended for real `Metin2` development workflows such as:

- client / server proto maintenance
- item balancing
- mob balancing
- flag management
- names synchronization
- large refactor passes
- safer batch editing before live deployment

---

## Video

Program video:

- [YouTube Demo](https://youtu.be/mJRztzZI8FU)

---

## Build

### Requirements

- `Windows 10 / 11`
- `Visual Studio 2022`
- `Desktop development with C++`
- `CMake 3.16+`

### Recommended Build

```bat
cd C:\ProtoEditor\cpp
build_imgui.bat
```

### Visual Studio Solution Build

```bat
cd C:\ProtoEditor\cpp
build.bat
```

After that open:

```text
C:\ProtoEditor\cpp\build\ProtoEditor.sln
```

---

## Runtime Notes

The editor now ships with embedded default proto config definitions.

If an external config file exists in the future, it can still be used as an override layer, but it is no longer required for normal distribution.

Encoding is preserved as accurately as possible during load/save:

- `UTF-8`
- `UTF-8 BOM`
- `CP1254`

This is especially important for Turkish `Metin2` proto and names files.

---

## Project Structure

```text
ProtoEditor/
├─ README.md
└─ cpp/
   ├─ CMakeLists.txt
   ├─ build.bat
   ├─ build_imgui.bat
   ├─ app.rc
   ├─ app.manifest
   └─ src/
      ├─ main.cpp
      ├─ ProtoEditorApp.cpp
      ├─ ProtoEditorApp.h
      ├─ TsvFile.cpp
      ├─ TsvFile.h
      ├─ ProtoConfig.cpp
      ├─ ProtoConfig.h
      ├─ AppPreferences.cpp
      ├─ AppPreferences.h
      └─ favicon.ico
```

---

## Creator

**Best Studio**

`Proto Editor Tools | by Best Studio`

---

## Contact

- GitHub: [github.com/ybeststudio](https://github.com/ybeststudio)
- Discord Server: [discord.gg/NXmc6JrwYr](https://discord.gg/NXmc6JrwYr)
- Discord ID: `beststudio`
- Web: [bestpro.dev](https://bestwebstudio.com.tr)
- E-Posta: [yakupp6@gmail.com](mailto:yakupp6@gmail.com)
- YouTube: [youtube.com/@ybeststudio](https://www.youtube.com/@ybeststudio)
- Instagram: [instagram.com/ybeststudio](https://www.instagram.com/ybeststudio)
- Facebook: [facebook.com/ybeststudio](https://www.facebook.com/ybeststudio/)
- Twitter: [twitter.com/ybeststudio](https://twitter.com/ybeststudio)
- TikTok: [tiktok.com/@ybeststudio](https://tiktok.com/@ybeststudio)

---

## Forum Profiles

- Turkmmo: [forum.turkmmo.com](https://forum.turkmmo.com/uye/2104546-best-studio/)
- Metin2Dev: [metin2.dev](https://metin2.dev/profile/7925-bestpro/)
- Mmotutkunları: [mmotutkunlari.com](https://www.mmotutkunlari.com/uye/best-production.5943/)
- Elitepvpers: [elitepvpers.com](https://www.elitepvpers.com/forum/members/6304596-bestpro01.html)

---

## Notes

This repository has been cleaned to keep only the active source set for the current `Dear ImGui` based application.

Legacy Win32 UI remnants, extra build README files and unrelated leftover assets were removed from the repo.

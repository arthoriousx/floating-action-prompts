# FAP - Floating Action Prompts

An F4SE C++20 plugin for Fallout 4 that anchors interaction prompts, QuickLoot/QuickContainer panels, and contextual item/NPC info cards directly in 3D world space over the targeted object.

Designed to work either in **Modern (GTA 6)** borderless overlay mode rendered via ImGui/DirectX 11 or in **Vanilla** floating Scaleform mode, with full live in-game customization.

---

## Features

- **3D World-Space Anchoring**
  - Projects prompts, workbenches, doors, containers, loose items, and NPCs onto their live 3D world positions with smooth frame interpolation and configurable screen-edge clamping/fade.
  - Per-category world offsets (Door, NPC/Actor, Container, Loose Item, Furniture/Terminal, and QuickContainer).
  - Smart anchor selection: tracks door handles/knobs/locks, actor torso/chest/head bones, and static visible-geometry bounds while filtering out particle systems, flames, billboards, `BSValueNode` effects, and furniture markers, plus transform/scale-keyed anchor caching for terminals and workbenches.
  - Renders in Power Armor by default, with an optional vanilla visor bypass.
  - Configurable companion interaction range (in and out of combat), loot focus deprioritization, remote talk/trade blocking with hold-to-draw on `R`, and configurable grab hold delay (`E`).

- **Two Visual Modes: Modern (GTA 6) & Vanilla**
  - **Modern (GTA 6)** renders crisp borderless floating prompts (Action Only, Action + Name, or Name on World Objects) with per-element sizing, colors, margins, shadows, optional pill background/border, stealing/crime prompt text color highlighting synced with the crosshair state, and **Stacked Vertical** or **Inline Horizontal** multi-action layouts (supporting multi-prompt companions, workbenches, and modded activators).
  - **Vanilla** preserves native Fallout 4 Scaleform prompt styling while translating the HUD rollover clip in world space.

- **5 Input Glyph Styles**
  - `Keyboard (E/R)`
  - `PlayStation (Colored)` & `Xbox (Colored)` (uses F4SE Menu Framework gamepad PNGs with vector circle fallbacks)
  - `PlayStation (Monochrome)` & `Xbox (Monochrome)` (high-contrast monochrome badges with optically centered bold glyphs and scaling via Inner Key Text Size)

- **Native FallUI Icon Library + FontAwesome Fallback**
  - Parses installed `FallUI_IconLib.swf` / Icon Expansion Pack SWF+XML definitions and rasterizes matching item/FIS sorter icons into a D3D11 texture atlas (`Data/Interface/ItemSorter`), falling back cleanly to built-in FontAwesome category glyphs when a tag or SWF is missing.
  - Per-category icon visibility toggles and optional per-category accent colors.

- **Smooth Mini Info Card**
  - Displays weapon/armor ratings, ammo type, value, weight, scrap components, and NPC stats beside/above/below the prompt or fixed to a screen corner.
  - Configurable hover dwell delay so the card appears only after aiming at a target for *X* seconds.
  - Supports inspecting items highlighted inside the QuickLoot list.

- **Floating QuickContainer / QuickLoot & Workbenches**
  - Anchors containers, workbenches, and Power Armor inventories in 3D world space with independent deadzone/smoothing, fixed or auto width, per-item icons, stack signature aggregation, top/bottom `...` overflow indicators, and configurable visible row count (including expanded Power Armor inventory rows while preserving vanilla `[E]` Enter / `[R]` Transfer).
  - Single-press QuickLoot taking without key-release double-takes, plus optional take debounce.
  - 19 granular QuickLoot sort categories (Keys, Medicines, Chems, Drinks, Food, Ammo, Explosives, Pistols, Rifles, Heavy Weapons, Melee, Power Armor, Armor, Clothing, Mods, Books/Notes, Holotapes, Misc, Junk) with per-instance classification, pinned priorities (Quest, Legendary, Tagged for Search, Caps), and live drag-and-drop priority reordering in the F1 menu.

- **Custom Fonts & Live In-Game Editor**
  - Drop any `.ttf` or `.otf` font files into `Data/F4SE/Plugins/FloatingActionPrompts/Fonts/` and assign independent fonts for **Prompt**, **QuickLoot**, and **Info Card**.
  - Live **F4SE Menu Framework** UI (press `F1` -> `FAP - Floating Action Prompts`) with 5 tabs (`General`, `Visual Style`, `Info Card`, `QuickContainer`, `3D Offsets`) and **MCM** integration.

---

## Requirements

- **Fallout 4** Next-Gen / Anniversary Edition (`1.10.980+` / `1.11.x`)
- **[F4SE (Fallout 4 Script Extender)](https://f4se.silverlock.org/)**
- **[Runtime Database (CommonLibF4RD)](https://www.nexusmods.com/fallout4/mods/108394)** (`f4rd-runtime.bin` for NG/AE runtime address resolution)
- **[Address Library for F4SE Plugins](https://www.nexusmods.com/fallout4/mods/47327)** (legacy fallback and required by F4SE Menu Framework)
- **[F4SE Menu Framework](https://www.nexusmods.com/fallout4/mods/105090)** (required for Modern overlay rendering, custom fonts, and the live F1 editor)
- **Microsoft Visual C++ Redistributable 2015–2022 (x64)** (typically already installed with Fallout 4 / Windows)
- **Optional:**
  - **[Mod Configuration Menu (MCM)](https://www.nexusmods.com/fallout4/mods/21497)**
  - **[FallUI - Icon Library](https://www.nexusmods.com/fallout4/mods/60579)** / **FIS (FallUI Item Sorter)** (automatically detected; falls back to FontAwesome if not installed)

---

## Installation

1. Install all required dependencies listed above.
2. Copy the `F4SE/` and `MCM/` folders from the release archive into your Fallout 4 `Data/` directory (or install via Vortex / Mod Organizer 2).
3. Launch the game through `f4se_loader.exe`.

### Configuration Load Order

Settings are loaded on startup in this order (later files override earlier ones):

1. `Data/MCM/Config/FloatingActionPrompts/settings.ini` (mod defaults)
2. `Data/F4SE/Plugins/FloatingActionPrompts.ini` (plugin defaults)
3. `Data/MCM/Settings/FloatingActionPrompts.ini` (user-saved MCM / F1 Menu overrides)

You can customize everything in-game without restarting via **F1 (F4SE Menu Framework)** -> `FAP - Floating Action Prompts` -> **Save Settings**, or through the **MCM**.

---

## Building from Source

### Prerequisites

- Windows x64 with **Visual Studio 2022 / Build Tools** (MSVC with C++20 support)
- **CMake** `>= 3.21`
- **vcpkg** (`VCPKG_ROOT` set or Visual Studio bundled vcpkg)

### External Dependencies (placed under `external/`)

Clone both upstream headers/libraries into `external/` inside the repository root:

```powershell
git clone https://github.com/Zzyxz/CommonLibF4RD.git external/CommonLibF4RD
git clone https://github.com/DCCStudios/F4SEMenuFrameworkExample.git external/F4SEMenuFrameworkExample
```

### Configure, Build, and Test

```powershell
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

After a successful build, the compiled plugin is at `build/Release/FloatingActionPrompts.dll`. All unit tests compile standalone (`FAP_UNIT_TEST`) and run via CTest without launching Fallout 4.

---

## Credits

- **Author:** ArthoriousX
- **CommonLibF4RD:** Zzyxz and contributors
- **F4SE Menu Framework:** DCCStudios
- **FallUI / FIS conventions:** m8r98a4f2

## License

Licensed under the [MIT License](LICENSE).

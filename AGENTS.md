# AGENTS.md

Developer and coding-agent guide for **FAP - Floating Action Prompts** (`FloatingActionPrompts`), an F4SE C++20 plugin targeting Fallout 4 OG/NG/AE (`1.10.163` / `1.10.980+` / `1.11.x`) using CommonLibF4RD and F4SE Menu Framework.

## Architecture & Module Map

```text
CMakeLists.txt
vcpkg.json
F4SE/Plugins/FloatingActionPrompts.ini          # Plugin default INI
F4SE/Plugins/FloatingActionPrompts/Fonts/       # User .ttf/.otf custom fonts
MCM/Config/FloatingActionPrompts/config.json    # MCM UI schema
MCM/Config/FloatingActionPrompts/settings.ini   # MCM default INI
src/
  main.cpp               # F4SE exports, VersionData, F4SE message listener (kPostLoad / kGameDataReady)
  Version.h              # Plugin::NAME, Plugin::AUTHOR, VERSION_MAJOR/MINOR/PATCH/STRING
  PCH.h                  # Precompiled header (CommonLibF4 + spdlog/fmt)
  Config.h / .cpp        # Thread-safe Config singleton, ConfigData snapshot, clamping, 3-file INI loader/saver
  AnchorManager.h / .cpp # Reference classification, door/actor/static-mesh anchor resolution, FX/particle/RTTI filtering
  Projection.h / .cpp    # World-to-screen projection, viewport clamping/fade, frame-rate-independent smoothing
  TargetInfo.h / .cpp    # Metadata extraction (scrap, value/weight, weapon/armor/NPC stats), sorter tag cleaning
  OverlayLayout.h / .cpp # InfoCard geometry/clamping, hover dwell alpha curve, QuickLoot list window, FontAwesome map
  FallUIIcons.h / .cpp   # Zlib + SWF tag parser, FIS/Color XML parser, 32x32 CPU vector rasterizer for FallUI icons
  HUDController.h / .cpp # HUDMenu::AdvanceMovie hook, ButtonHintBar scanner, QuickContainer merge/expand, OverlaySnapshot
  MenuFrameworkUI.h/.cpp # ImGui D3D11 HUD overlay renderer, dynamic framework probing, gamepad badges, 6-tab F1 editor
tests/
  check.h
  test_scaffold.cpp
  test_config.cpp
  test_anchor.cpp
  test_projection.cpp
  test_hud_controller.cpp
  test_target_info.cpp
  test_overlay_layout.cpp
  test_fallui_icons.cpp
```

## Critical Invariants

1. **Thread Boundary (Game/HUD vs D3D11/ImGui Render):**
   - `HUDController::Controller` runs inside `HUDMenu::AdvanceMovie` (game HUD thread) and owns Scaleform `GFxValue` objects and `TESObjectREFR*` pointers.
   - `MenuFrameworkUI` renders on the D3D11 ImGui render thread and must **only** read immutable snapshots (`Controller::GetOverlaySnapshot()`, `Config::GetSnapshot()`) and atomics (`_hoverTargetKey`, `_hoverFirstSeenMs`, `_qcHoverItemHash`). Never dereference `TESObjectREFR*` or Scaleform values from the render thread.
   - `Controller::ResetState()` / `Controller::InvalidateQuickContainerCache()` may be called from the render thread (e.g., F1 menu "Reset Defaults"); they set atomic flags and clear `_overlaySnapshot`, while actual Scaleform/cache resets execute on the HUD thread inside `Controller::PerformDeferredResets()`.

2. **Standalone Unit Test Separation (`FAP_UNIT_TEST`):**
   - Pure logic in `Config`, `Projection`, `OverlayLayout`, `TargetInfo`, `AnchorManager`, `FallUIIcons`, and `HUDController` (`HUDTransformState`, `ResolveMergedQuickContainerWindow`, string helpers) compiles under `#ifndef FAP_UNIT_TEST` guards in `CMakeLists.txt` without CommonLibF4 or game globals. Keep runtime `RE::*` types isolated behind `#ifndef FAP_UNIT_TEST`.

3. **Runtime Safety & Compatibility:**
   - Gamepad image rendering (`ImDrawList_AddImage`), circle primitives (`AddCircle` / `AddCircleFilled`), custom font atlases (`ImFontAtlas::AddFontFromFileTTF`), and glyph optical centering (`ImFont_FindGlyph`) are probed dynamically at startup via `GetProcAddress` against `F4SEMenuFramework.dll`. Always maintain graceful fallbacks if any export is absent.
   - Furniture and workbench anchoring (`AnchorManager::IsExcludedNodeOrEffect` + `HUDController::Controller::ResolveStableAnchorWorldPosition`) must filter transient particle systems, `BSValueNode`, `BSMasterParticleSystem`, `NiBillboardNode`, and tokenized FX nodes without excluding legitimate static meshes (`ChemistryStation`, `ViseClamps`, `FireBarrel`, etc.), and never cache unpopulated Frame-1 bounds (`rootPtr == nullptr` or `!usedGeometry`).

4. **INI Encoding & Clamping:**
   - `Config::ApplySnapshot` clamps every numeric setting to safe bounds before storing or saving.
   - Keep default values synchronized across all configuration layers whenever a default changes:
     - `src/Config.h` (`ConfigData` member initializers)
     - `F4SE/Plugins/FloatingActionPrompts.ini`
     - `MCM/Config/FloatingActionPrompts/settings.ini`
     - `MCM/Config/FloatingActionPrompts/config.json` (ranges/options/IDs)
     - `src/MenuFrameworkUI.cpp` (slider/combo bounds)
     - `tests/test_config.cpp` & `tests/test_anchor.cpp`

5. **Version & Exports (`src/Version.h`, `CMakeLists.txt`, `config.json`, `tests/test_scaffold.cpp`):**
   - Fallout 4 NG/AE F4SE requires all three DLL exports: `F4SEPlugin_Load`, `F4SEPlugin_Query`, and `F4SEPlugin_Version`.
   - `CMakeLists.txt`, `src/Version.h`, `MCM/Config/FloatingActionPrompts/config.json`, and `tests/test_scaffold.cpp` must share the exact same semantic version.

## Build, Test & Verification Workflow

```powershell
# Configure (uses $env:VCPKG_ROOT automatically when set)
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# Build Release DLL + all 8 unit test binaries
cmake --build build --config Release

# Run full unit test suite (all 8 tests must pass)
ctest --test-dir build -C Release --output-on-failure

# Validate MCM config JSON syntax
Get-Content -Raw MCM\Config\FloatingActionPrompts\config.json | ConvertFrom-Json | Out-Null

# Verify DLL exports (from VS Developer prompt or MSVC dumpbin)
dumpbin /exports build\Release\FloatingActionPrompts.dll
```

### Mandatory Post-Change Code Review

- After every code change (bug fix, refactor, or feature), **always** dispatch an independent read-only code reviewer subagent (following the `requesting-code-review` workflow) to inspect `git diff HEAD` / branch range against the **Critical Invariants** above (thread boundaries, `FAP_UNIT_TEST` isolation, INI/MCM sync, runtime safety), C++20/F4SE edge cases, and unit test coverage.
- Fix all **Critical** and **Important** findings before deploying the DLL, committing, or merging, then re-run the Release build and `ctest` suite.

## Repository Hygiene

- Never commit compiled binaries (`F4SE/Plugins/*.dll`, `build/`), third-party clones (`external/`), logs (`*.log`), or local planning notes (`Docs/`).
- Avoid hardcoding machine-specific filesystem paths or personal credentials in tracked files.

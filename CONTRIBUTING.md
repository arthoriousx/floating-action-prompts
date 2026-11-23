# Contributing to FAP - Floating Action Prompts

Thank you for your interest in contributing to **FAP - Floating Action Prompts**!

See [AGENTS.md](AGENTS.md) for the full module map, architectural invariants, and design constraints before making larger changes.

---

## Workflow

1. Fork the repository on GitHub and create a topic branch from `main` (e.g., `fix/workbench-anchor` or `feat/new-badge-style`).
2. Keep changes focused and covered by unit tests where pure logic is affected.
3. Verify your build and tests locally (see below).
4. Open a Pull Request targeting `main` and complete the PR checklist.

---

## Local Build & Test Setup

### Option A: Fast Unit-Test-Only Build (No External Dependencies)

All 8 unit test executables compile standalone (`FAP_UNIT_TEST`) without `CommonLibF4RD`, `F4SEMenuFrameworkExample`, `vcpkg`, or Fallout 4 game files:

```powershell
cmake -B build-tests -S . -DFAP_BUILD_PLUGIN=OFF -A x64
cmake --build build-tests --config Release --parallel
ctest --test-dir build-tests -C Release --output-on-failure
```

### Option B: Full Plugin DLL + Unit Tests Build

Prerequisites:
- Windows x64, MSVC with C++20 support (Visual Studio 2022 or newer)
- CMake >= 3.21
- vcpkg (`VCPKG_ROOT` set or Visual Studio bundled vcpkg)

Clone the external dependencies under `external/` (gitignored):

```powershell
git clone https://github.com/Zzyxz/CommonLibF4RD.git external/CommonLibF4RD
git clone https://github.com/DCCStudios/F4SEMenuFrameworkExample.git external/F4SEMenuFrameworkExample
```

Configure, build Release, and run CTest:

```powershell
cmake -B build -S . -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

---

## Architecture & Coding Rules

1. **Thread Safety (`Game/HUD Thread` vs `D3D11/ImGui Render Thread`):**
   - `HUDController::Controller` runs in `HUDMenu::AdvanceMovie` on the HUD thread.
   - `MenuFrameworkUI` runs on the D3D11/ImGui render thread and must **only** read immutable snapshots (`Controller::GetOverlaySnapshot()`, `Config::GetSnapshot()`) and atomics. Never dereference `TESObjectREFR*` or Scaleform values from the render thread.
   - Defer Scaleform / cache resets triggered from the F1 menu via atomics consumed in `Controller::PerformDeferredResets()`.

2. **F4SE Menu Framework Compatibility:**
   - Probe optional exports on `F4SEMenuFramework.dll` at startup (`GetProcAddress`) and preserve safe fallbacks if an export is missing.

3. **Keep Unit Tests Standalone (`FAP_UNIT_TEST`):**
   - Isolate `RE::*` engine types and hooks behind `#ifndef FAP_UNIT_TEST` so all test targets continue to build without runtime game headers.

4. **Synchronize Config Schema Changes:**
   Whenever adding, renaming, or changing default settings, keep all layers in sync:
   - `src/Config.h` (`ConfigData` defaults)
   - `src/Config.cpp` (INI parsing, writing, and `ClampFinite` bounds)
   - `F4SE/Plugins/FloatingActionPrompts.ini`
   - `MCM/Config/FloatingActionPrompts/settings.ini` (keep identical defaults to the plugin INI)
   - `MCM/Config/FloatingActionPrompts/config.json` (validate with `ConvertFrom-Json`)
   - `src/MenuFrameworkUI.cpp` (F1 menu controls)
   - `tests/test_config.cpp`

5. **Repository Hygiene:**
   - Never commit compiled binaries (`*.dll`, `build/`), cloned third-party trees (`external/`), log files (`*.log`), or machine-specific local paths.

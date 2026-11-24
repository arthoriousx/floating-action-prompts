#include "MenuFrameworkUI.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <d3d11.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Config.h"
#include "F4SEMenuFramework.h"
#include "FallUIIcons.h"
#include "HUDController.h"
#include "OverlayLayout.h"
#include "TargetInfo.h"

namespace MenuFrameworkUI
{
    namespace
    {
        std::string g_statusMessage;
        F4SEMenuFramework::Model::HudElement* g_hudElement{ nullptr };
        std::atomic<bool> g_hudOverlayActive{ false };
        std::atomic<bool> g_drawImageSupported{ false };
        std::atomic<bool> g_circlePrimitivesSupported{ false };
        std::atomic<bool> g_findGlyphSupported{ false };

        struct GamepadTextureCache
        {
            bool attempted{ false };
            ImGuiMCP::ImTextureID psCross{ nullptr };
            ImGuiMCP::ImTextureID psSquare{ nullptr };
            ImGuiMCP::ImTextureID psTriangle{ nullptr };
            ImGuiMCP::ImTextureID psCircle{ nullptr };
            ImGuiMCP::ImTextureID xboxA{ nullptr };
            ImGuiMCP::ImTextureID xboxX{ nullptr };
            ImGuiMCP::ImTextureID xboxY{ nullptr };
            ImGuiMCP::ImTextureID xboxB{ nullptr };
        };

        GamepadTextureCache g_gamepadCache{};

        inline constexpr std::size_t kMaxFontDirEntries = 256;
        inline constexpr std::size_t kMaxCachedFonts = 8;

        struct FontChoice
        {
            std::string displayName;
            std::string settingValue;
            std::filesystem::path fullPath;
        };

        struct LoadedFontEntry
        {
            ImGuiMCP::ImFontAtlas* atlas{ nullptr };
            ImGuiMCP::ImFont* font{ nullptr };
            ID3D11ShaderResourceView* srv{ nullptr };
            bool failed{ false };
        };

        struct DeferredFontFree
        {
            ImGuiMCP::ImFontAtlas* atlas{ nullptr };
            ID3D11ShaderResourceView* srv{ nullptr };
            int framesLeft{ 4 };
        };

        std::atomic<bool> g_customFontsSupported{ false };
        std::mutex g_fontMutex;
        ID3D11Device* g_cachedFontDevice{ nullptr };
        std::vector<FontChoice> g_fontChoices;
        bool g_fontDiscoveryDone{ false };
        std::unordered_map<std::string, LoadedFontEntry> g_loadedFonts;
        std::vector<DeferredFontFree> g_deferredFontFrees;

        [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& a_p)
        {
            const std::u8string u8 = a_p.u8string();
            return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
        }

        [[nodiscard]] std::filesystem::path Utf8ToPath(std::string_view a_utf8)
        {
            return std::filesystem::path(
                std::u8string_view(reinterpret_cast<const char8_t*>(a_utf8.data()), a_utf8.size()));
        }

        [[nodiscard]] std::string NormalizeSlashStr(std::string a_s)
        {
            std::replace(a_s.begin(), a_s.end(), '\\', '/');
            return a_s;
        }

        [[nodiscard]] std::string NormalizeSlashPath(const std::filesystem::path& a_p)
        {
            return NormalizeSlashStr(PathToUtf8(a_p));
        }

        void DestroyFontEntryNow(LoadedFontEntry& a_entry) noexcept
        {
            if (a_entry.srv) {
                a_entry.srv->Release();
                a_entry.srv = nullptr;
            }
            if (a_entry.atlas) {
                ImGuiMCP::ImFontAtlasManager::Destroy(a_entry.atlas);
                a_entry.atlas = nullptr;
            }
            a_entry.font = nullptr;
        }

        void FlushDeferredFontFrees(bool a_immediate)
        {
            std::lock_guard lock{ g_fontMutex };
            for (auto it = g_deferredFontFrees.begin(); it != g_deferredFontFrees.end();) {
                if (!a_immediate && --(it->framesLeft) > 0) {
                    ++it;
                    continue;
                }
                if (it->srv) {
                    it->srv->Release();
                }
                if (it->atlas) {
                    ImGuiMCP::ImFontAtlasManager::Destroy(it->atlas);
                }
                it = g_deferredFontFrees.erase(it);
            }
        }

        void ScanFontDir(
            const std::filesystem::path& a_dir,
            const std::string& a_prefix,
            bool a_useRelativeSetting,
            std::vector<FontChoice>& a_out,
            std::unordered_map<std::string, bool>& a_seenLowerNames)
        {
            std::error_code ec;
            if (!std::filesystem::exists(a_dir, ec) || !std::filesystem::is_directory(a_dir, ec)) {
                return;
            }

            std::vector<std::filesystem::path> found;
            for (auto it = std::filesystem::directory_iterator(a_dir, ec);
                 !ec && it != std::filesystem::directory_iterator{};
                 it.increment(ec)) {
                if (!it->is_regular_file(ec)) {
                    continue;
                }
                const auto ext = TargetInfo::ToUpperAscii(PathToUtf8(it->path().extension()));
                if (ext != ".TTF" && ext != ".OTF") {
                    continue;
                }
                const std::string fileName = PathToUtf8(it->path().filename());
                const std::string upperName = TargetInfo::ToUpperAscii(fileName);
                if (upperName.rfind("FA-", 0) == 0) {
                    continue;
                }
                if (a_seenLowerNames.contains(upperName)) {
                    continue;
                }
                a_seenLowerNames[upperName] = true;
                found.push_back(it->path());
                if (found.size() >= kMaxFontDirEntries) {
                    break;
                }
            }

            std::sort(found.begin(), found.end(),
                [](const std::filesystem::path& a, const std::filesystem::path& b) {
                    return TargetInfo::ToUpperAscii(PathToUtf8(a.filename())) <
                           TargetInfo::ToUpperAscii(PathToUtf8(b.filename()));
                });

            for (const auto& p : found) {
                FontChoice c;
                const std::string stem = PathToUtf8(p.stem());
                const std::string fname = PathToUtf8(p.filename());
                c.displayName = a_prefix + stem + " (" + fname + ")";
                c.settingValue = a_useRelativeSetting ? NormalizeSlashPath(p) : fname;
                c.fullPath = p;
                a_out.push_back(std::move(c));
            }
        }

        std::vector<FontChoice> DiscoverAvailableFonts(bool a_forceRefresh = false)
        {
            std::lock_guard lock{ g_fontMutex };
            if (g_fontDiscoveryDone && !a_forceRefresh) {
                return g_fontChoices;
            }
            if (a_forceRefresh) {
                for (auto it = g_loadedFonts.begin(); it != g_loadedFonts.end();) {
                    if (it->second.failed) {
                        it = g_loadedFonts.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            std::vector<FontChoice> choices;
            choices.push_back({ "Default (F4SE Menu Framework)", "", {} });

            std::unordered_map<std::string, bool> seenNames;
            ScanFontDir("Data/F4SE/Plugins/FloatingActionPrompts/Fonts", "[Mod] ", true, choices, seenNames);
            ScanFontDir("Data/F4SE/Plugins/F4SEMenuFramework/Fonts", "[FMF] ", true, choices, seenNames);
            ScanFontDir("Data/Interface/ImGuiFonts", "[Game] ", true, choices, seenNames);

            static constexpr std::pair<const char*, const char*> kWindowsFonts[] = {
                { "bahnschrift.ttf", "Bahnschrift" },
                { "segoeui.ttf", "Segoe UI" },
                { "segoeuib.ttf", "Segoe UI Bold" },
                { "segoeuisl.ttf", "Segoe UI Semilight" },
                { "arial.ttf", "Arial" },
                { "arialbd.ttf", "Arial Bold" },
                { "calibri.ttf", "Calibri" },
                { "calibrib.ttf", "Calibri Bold" },
                { "consola.ttf", "Consolas" },
                { "consolab.ttf", "Consolas Bold" },
                { "tahoma.ttf", "Tahoma" },
                { "tahomabd.ttf", "Tahoma Bold" },
                { "verdana.ttf", "Verdana" },
                { "trebuc.ttf", "Trebuchet MS" },
                { "Candara.ttf", "Candara" }
            };

            std::error_code ec;
            const std::filesystem::path winFontDir = "C:/Windows/Fonts";
            for (const auto& [file, label] : kWindowsFonts) {
                const std::string upperName = TargetInfo::ToUpperAscii(file);
                if (seenNames.contains(upperName)) {
                    continue;
                }
                const auto full = winFontDir / file;
                if (std::filesystem::exists(full, ec) && std::filesystem::is_regular_file(full, ec)) {
                    seenNames[upperName] = true;
                    choices.push_back({
                        std::string("[Win] ") + label,
                        NormalizeSlashPath(full),
                        full
                    });
                }
            }

            g_fontChoices = std::move(choices);
            g_fontDiscoveryDone = true;
            return g_fontChoices;
        }

        [[nodiscard]] ImGuiMCP::ImFont* ResolveCustomFont(
            const std::string& a_setting,
            ImGuiMCP::ImFont* a_fallback,
            const ConfigData& a_cfg)
        {
            if (a_setting.empty() || !g_customFontsSupported.load(std::memory_order_acquire)) {
                return a_fallback;
            }

            auto* rd = RE::BSGraphics::RendererData::GetSingleton();
            if (!rd || !rd->initialized || !rd->device) {
                return a_fallback;
            }

            const std::string normalizedSetting = NormalizeSlashStr(a_setting);
            const std::string cacheKey = TargetInfo::ToUpperAscii(normalizedSetting);
            {
                std::lock_guard lock{ g_fontMutex };
                if (g_cachedFontDevice != rd->device) {
                    for (auto& [k, e] : g_loadedFonts) {
                        DestroyFontEntryNow(e);
                    }
                    g_loadedFonts.clear();
                    for (auto& df : g_deferredFontFrees) {
                        if (df.srv) df.srv->Release();
                        if (df.atlas) ImGuiMCP::ImFontAtlasManager::Destroy(df.atlas);
                    }
                    g_deferredFontFrees.clear();
                    g_cachedFontDevice = rd->device;
                } else if (const auto it = g_loadedFonts.find(cacheKey); it != g_loadedFonts.end()) {
                    return (it->second.font && !it->second.failed) ? it->second.font : a_fallback;
                }
            }

            const auto choices = DiscoverAvailableFonts(false);
            std::filesystem::path resolvedPath;
            for (const auto& c : choices) {
                if (c.settingValue.empty()) {
                    continue;
                }
                if (TargetInfo::ToUpperAscii(c.settingValue) == cacheKey ||
                    TargetInfo::ToUpperAscii(PathToUtf8(c.fullPath.filename())) == cacheKey) {
                    resolvedPath = c.fullPath;
                    break;
                }
            }

            std::error_code ec;
            if (resolvedPath.empty()) {
                std::filesystem::path direct = Utf8ToPath(normalizedSetting);
                if (std::filesystem::exists(direct, ec) && std::filesystem::is_regular_file(direct, ec)) {
                    const std::string ext = TargetInfo::ToUpperAscii(PathToUtf8(direct.extension()));
                    if (ext == ".TTF" || ext == ".OTF") {
                        resolvedPath = std::move(direct);
                    }
                }
            }

            LoadedFontEntry entry{};
            if (resolvedPath.empty() || !std::filesystem::exists(resolvedPath, ec)) {
                entry.failed = true;
                logger::warn("Custom font '{}' not found; falling back to default UI font", a_setting);
                std::lock_guard lock{ g_fontMutex };
                if (g_loadedFonts.size() < 64) {
                    g_loadedFonts[cacheKey] = entry;
                }
                return a_fallback;
            }

            const std::string pathStr = PathToUtf8(resolvedPath);
            ImGuiMCP::ImFontAtlas* atlas = ImGuiMCP::ImFontAtlasManager::Create();
            if (!atlas) {
                entry.failed = true;
                std::lock_guard lock{ g_fontMutex };
                if (g_loadedFonts.size() < 64) {
                    g_loadedFonts[cacheKey] = entry;
                }
                return a_fallback;
            }

            static constexpr ImGuiMCP::ImWchar kExtendedRanges[] = {
                0x0020, 0x024F, // Basic Latin + Latin-1 + Latin Extended A/B
                0x0370, 0x04FF, // Greek + Cyrillic
                0x2010, 0x203A, // Dashes, quotes, bullets, ellipsis
                0x2190, 0x2193, // Arrows
                0
            };
            ImGuiMCP::ImFont* font = ImGuiMCP::ImFontAtlasManager::AddFontFromFileTTF(
                atlas, pathStr.c_str(), 32.0f, nullptr, kExtendedRanges);
            if (!font || !ImGuiMCP::ImFontAtlasManager::Build(atlas)) {
                ImGuiMCP::ImFontAtlasManager::Destroy(atlas);
                entry.failed = true;
                logger::warn("Failed to build ImGui font atlas for '{}'", pathStr);
                std::lock_guard lock{ g_fontMutex };
                if (g_loadedFonts.size() < 64) {
                    g_loadedFonts[cacheKey] = entry;
                }
                return a_fallback;
            }

            unsigned char* pixels = nullptr;
            int texW = 0;
            int texH = 0;
            ImGuiMCP::ImFontAtlasManager::GetTexDataAsRGBA32(atlas, &pixels, &texW, &texH, nullptr);
            if (!pixels || texW <= 0 || texH <= 0) {
                ImGuiMCP::ImFontAtlasManager::Destroy(atlas);
                entry.failed = true;
                std::lock_guard lock{ g_fontMutex };
                if (g_loadedFonts.size() < 64) {
                    g_loadedFonts[cacheKey] = entry;
                }
                return a_fallback;
            }

            D3D11_TEXTURE2D_DESC texDesc{};
            texDesc.Width = static_cast<UINT>(texW);
            texDesc.Height = static_cast<UINT>(texH);
            texDesc.MipLevels = 1;
            texDesc.ArraySize = 1;
            texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            texDesc.SampleDesc.Count = 1;
            texDesc.Usage = D3D11_USAGE_IMMUTABLE;
            texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA initData{};
            initData.pSysMem = pixels;
            initData.SysMemPitch = static_cast<UINT>(texW * 4);

            ID3D11Texture2D* tex2d = nullptr;
            HRESULT hr = rd->device->CreateTexture2D(&texDesc, &initData, &tex2d);
            ID3D11ShaderResourceView* srv = nullptr;
            if (SUCCEEDED(hr) && tex2d) {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                hr = rd->device->CreateShaderResourceView(tex2d, &srvDesc, &srv);
                tex2d->Release();
            }

            if (FAILED(hr) || !srv) {
                ImGuiMCP::ImFontAtlasManager::Destroy(atlas);
                entry.failed = true;
                logger::warn("Failed to create D3D11 SRV for custom font '{}'", pathStr);
                std::lock_guard lock{ g_fontMutex };
                if (g_loadedFonts.size() < 64) {
                    g_loadedFonts[cacheKey] = entry;
                }
                return a_fallback;
            }

            ImGuiMCP::ImFontAtlasManager::SetTexID(atlas, reinterpret_cast<ImGuiMCP::ImTextureID>(srv));
            ImGuiMCP::ImFontAtlasManager::ClearTexData(atlas);

            entry.atlas = atlas;
            entry.font = font;
            entry.srv = srv;
            entry.failed = false;

            const std::string prot1 = TargetInfo::ToUpperAscii(NormalizeSlashStr(a_cfg.promptFont));
            const std::string prot2 = TargetInfo::ToUpperAscii(NormalizeSlashStr(a_cfg.qcFont));
            const std::string prot3 = TargetInfo::ToUpperAscii(NormalizeSlashStr(a_cfg.infoCardFont));

            logger::info("Loaded custom font '{}' from '{}'", a_setting, pathStr);
            {
                std::lock_guard lock{ g_fontMutex };
                if (const auto existing = g_loadedFonts.find(cacheKey);
                    existing != g_loadedFonts.end() && existing->second.font && !existing->second.failed) {
                    DestroyFontEntryNow(entry);
                    return existing->second.font;
                }

                // Evict stale inactive font entries past kMaxCachedFonts via deferred GPU free
                if (g_loadedFonts.size() >= kMaxCachedFonts) {
                    for (auto it = g_loadedFonts.begin(); it != g_loadedFonts.end() && g_loadedFonts.size() >= kMaxCachedFonts;) {
                        if (it->first != cacheKey && it->first != prot1 && it->first != prot2 && it->first != prot3) {
                            if (it->second.srv || it->second.atlas) {
                                g_deferredFontFrees.push_back({ it->second.atlas, it->second.srv, 4 });
                            }
                            it = g_loadedFonts.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                g_loadedFonts[cacheKey] = entry;
            }
            return font;
        }

        bool DrawFontCombo(const char* a_label, std::string& a_fontSetting)
        {
            const auto baseChoices = DiscoverAvailableFonts(false);
            std::vector<FontChoice> items = baseChoices;

            int currentIdx = -1;
            const std::string normalizedCur = NormalizeSlashStr(a_fontSetting);
            const std::string curUpper = TargetInfo::ToUpperAscii(normalizedCur);
            if (curUpper.empty()) {
                currentIdx = 0;
            } else {
                for (int i = 1; i < static_cast<int>(items.size()); ++i) {
                    const auto& c = items[static_cast<std::size_t>(i)];
                    if (TargetInfo::ToUpperAscii(c.settingValue) == curUpper ||
                        TargetInfo::ToUpperAscii(PathToUtf8(c.fullPath.filename())) == curUpper) {
                        currentIdx = i;
                        break;
                    }
                }
            }

            if (currentIdx < 0) {
                items.push_back({ "Custom: " + a_fontSetting, a_fontSetting, {} });
                currentIdx = static_cast<int>(items.size()) - 1;
            }

            std::vector<const char*> labels;
            labels.reserve(items.size());
            for (const auto& it : items) {
                labels.push_back(it.displayName.c_str());
            }

            bool changed = false;
            if (ImGuiMCP::Combo(a_label, &currentIdx, labels.data(), static_cast<int>(labels.size()), 12)) {
                if (currentIdx >= 0 && currentIdx < static_cast<int>(items.size())) {
                    a_fontSetting = items[static_cast<std::size_t>(currentIdx)].settingValue;
                    changed = true;
                }
            }
            return changed;
        }

        [[nodiscard]] ImGuiMCP::ImU32 MakeCol(std::uint8_t a_r, std::uint8_t a_g, std::uint8_t a_b, float a_alpha) noexcept
        {
            using ImGuiMCP::ImU32;
            const int alphaByte = std::clamp(static_cast<int>(a_alpha * 255.0f + 0.5f), 0, 255);
            return IM_COL32(a_r, a_g, a_b, alphaByte);
        }

        [[nodiscard]] ImGuiMCP::ImU32 MakeRgbCol(std::uint32_t a_rgb, float a_alpha) noexcept
        {
            const auto r = static_cast<std::uint8_t>((a_rgb >> 16) & 0xFFu);
            const auto g = static_cast<std::uint8_t>((a_rgb >> 8) & 0xFFu);
            const auto b = static_cast<std::uint8_t>(a_rgb & 0xFFu);
            return MakeCol(r, g, b, a_alpha);
        }

        [[nodiscard]] std::int64_t SteadyNowMs() noexcept
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        }

        [[nodiscard]] float EffectiveEdgeMargin(const ConfigData& a_cfg, float a_screenW, float a_screenH) noexcept
        {
            if (!a_cfg.enableClamping) {
                return 0.0f;
            }
            const float maxAllowed = std::max(0.0f, std::min(a_screenW, a_screenH) * 0.45f);
            return std::clamp(a_cfg.edgeMargin, 0.0f, maxAllowed);
        }

        void EnsureGamepadTexturesLoaded()
        {
            if (g_gamepadCache.attempted) {
                return;
            }
            g_gamepadCache.attempted = true;
            g_gamepadCache.psCross = F4SEMenuFramework::LoadTexture("Data\\F4SE\\Plugins\\F4SEMenuFramework\\Gamepad\\ps_cross.png", { 32.0f, 32.0f });
            g_gamepadCache.psSquare = F4SEMenuFramework::LoadTexture("Data\\F4SE\\Plugins\\F4SEMenuFramework\\Gamepad\\ps_square.png", { 32.0f, 32.0f });
            g_gamepadCache.psTriangle = F4SEMenuFramework::LoadTexture("Data\\F4SE\\Plugins\\F4SEMenuFramework\\Gamepad\\ps_triangle.png", { 32.0f, 32.0f });
            g_gamepadCache.psCircle = F4SEMenuFramework::LoadTexture("Data\\F4SE\\Plugins\\F4SEMenuFramework\\Gamepad\\ps_circle.png", { 32.0f, 32.0f });
            g_gamepadCache.xboxA = F4SEMenuFramework::LoadTexture("Data\\F4SE\\Plugins\\F4SEMenuFramework\\Gamepad\\xbox_a.png", { 32.0f, 32.0f });
            g_gamepadCache.xboxX = F4SEMenuFramework::LoadTexture("Data\\F4SE\\Plugins\\F4SEMenuFramework\\Gamepad\\xbox_x.png", { 32.0f, 32.0f });
            g_gamepadCache.xboxY = F4SEMenuFramework::LoadTexture("Data\\F4SE\\Plugins\\F4SEMenuFramework\\Gamepad\\xbox_y.png", { 32.0f, 32.0f });
            g_gamepadCache.xboxB = F4SEMenuFramework::LoadTexture("Data\\F4SE\\Plugins\\F4SEMenuFramework\\Gamepad\\xbox_b.png", { 32.0f, 32.0f });
        }

        [[nodiscard]] ImGuiMCP::ImVec2 MeasureTextScaled(
            ImGuiMCP::ImFont* a_font,
            float a_fontSize,
            const char* a_text)
        {
            ImGuiMCP::ImVec2 sz{ 0.0f, 0.0f };
            if (!a_text || a_text[0] == '\0') {
                return sz;
            }
            if (a_font && a_fontSize > 0.0f) {
                ImGuiMCP::ImFontManger::CalcTextSizeA(&sz, a_font, a_fontSize, 10000.0f, 0.0f, a_text, nullptr, nullptr);
            } else {
                ImGuiMCP::CalcTextSize(&sz, a_text, nullptr, false, -1.0f);
            }
            return sz;
        }

        [[nodiscard]] bool NeedsCustomAtlasTexture(ImGuiMCP::ImFont* a_font)
        {
            if (!g_customFontsSupported.load(std::memory_order_acquire) ||
                !a_font || !a_font->ContainerAtlas || !a_font->ContainerAtlas->TexID) {
                return false;
            }
            auto* io = ImGuiMCP::GetIO();
            const ImGuiMCP::ImTextureID baseTex = (io && io->Fonts) ? io->Fonts->TexID : nullptr;
            return a_font->ContainerAtlas->TexID != baseTex;
        }

        void DrawTextScaled(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImFont* a_font,
            float a_fontSize,
            ImGuiMCP::ImVec2 a_pos,
            ImGuiMCP::ImU32 a_col,
            const char* a_text)
        {
            if (!a_dl || !a_text || a_text[0] == '\0') {
                return;
            }
            if (a_font && a_fontSize > 0.0f) {
                const bool pushTex = NeedsCustomAtlasTexture(a_font);
                if (pushTex) {
                    ImGuiMCP::ImDrawListManager::PushTextureID(a_dl, a_font->ContainerAtlas->TexID);
                }
                ImGuiMCP::ImDrawListManager::AddText(a_dl, a_font, a_fontSize, a_pos, a_col, a_text, nullptr, 0.0f, nullptr);
                if (pushTex) {
                    ImGuiMCP::ImDrawListManager::PopTextureID(a_dl);
                }
            } else {
                ImGuiMCP::ImDrawListManager::AddText(a_dl, a_pos, a_col, a_text, nullptr);
            }
        }

        void DrawTextShadowed(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImFont* a_font,
            float a_fontSize,
            ImGuiMCP::ImVec2 a_pos,
            ImGuiMCP::ImU32 a_col,
            float a_shadowAlpha,
            bool a_shadow,
            float a_scale,
            const char* a_text)
        {
            if (!a_dl || !a_text || a_text[0] == '\0') {
                return;
            }
            const bool pushTex = a_font && a_fontSize > 0.0f && NeedsCustomAtlasTexture(a_font);
            if (pushTex) {
                ImGuiMCP::ImDrawListManager::PushTextureID(a_dl, a_font->ContainerAtlas->TexID);
            }
            if (a_shadow && a_shadowAlpha > 0.01f) {
                const ImGuiMCP::ImU32 shadowCol = MakeCol(0, 0, 0, 0.85f * a_shadowAlpha);
                const ImGuiMCP::ImVec2 sPos{ a_pos.x + 1.0f * a_scale, a_pos.y + 1.5f * a_scale };
                if (a_font && a_fontSize > 0.0f) {
                    ImGuiMCP::ImDrawListManager::AddText(a_dl, a_font, a_fontSize, sPos, shadowCol, a_text, nullptr, 0.0f, nullptr);
                } else {
                    ImGuiMCP::ImDrawListManager::AddText(a_dl, sPos, shadowCol, a_text, nullptr);
                }
            }
            if (a_font && a_fontSize > 0.0f) {
                ImGuiMCP::ImDrawListManager::AddText(a_dl, a_font, a_fontSize, a_pos, a_col, a_text, nullptr, 0.0f, nullptr);
            } else {
                ImGuiMCP::ImDrawListManager::AddText(a_dl, a_pos, a_col, a_text, nullptr);
            }
            if (pushTex) {
                ImGuiMCP::ImDrawListManager::PopTextureID(a_dl);
            }
        }

        inline constexpr int kFallUIAtlasGrid = 32;
        inline constexpr int kFallUIMaxSlots = kFallUIAtlasGrid * kFallUIAtlasGrid;
        inline constexpr UINT kFallUIAtlasPx = static_cast<UINT>(kFallUIAtlasGrid * FallUIIcons::kIconTileSize);

        struct FallUIAtlasSlot
        {
            bool valid{ false };
            bool usedFallback{ false };
            ImGuiMCP::ImVec2 uv0{};
            ImGuiMCP::ImVec2 uv1{};
        };

        struct FallUIAtlasState
        {
            ID3D11Device* device{ nullptr };
            ID3D11Texture2D* texture{ nullptr };
            ID3D11ShaderResourceView* srv{ nullptr };
            int nextSlot{ 0 };
            std::unordered_map<std::string, FallUIAtlasSlot> slots;
            bool registryAttempted{ false };
            bool registryReady{ false };
            bool hasBaseSwf{ false };
            FallUIIcons::FallUIIconRegistry registry;
        };

        std::mutex g_falluiAtlasMutex;
        FallUIAtlasState g_falluiAtlas{};

        void DestroyFallUIAtlasResources() noexcept
        {
            if (g_falluiAtlas.srv) {
                g_falluiAtlas.srv->Release();
                g_falluiAtlas.srv = nullptr;
            }
            if (g_falluiAtlas.texture) {
                g_falluiAtlas.texture->Release();
                g_falluiAtlas.texture = nullptr;
            }
            g_falluiAtlas.slots.clear();
            g_falluiAtlas.nextSlot = 0;
        }

        [[nodiscard]] std::filesystem::path ResolveGameInterfaceDir()
        {
            std::error_code ec;
            std::vector<wchar_t> buf(512, L'\0');
            for (;;) {
                const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
                if (len == 0) {
                    break;
                }
                if (len < buf.size() - 1) {
                    const std::filesystem::path exePath(std::wstring_view(buf.data(), len));
                    const std::filesystem::path candidate = exePath.parent_path() / L"Data" / L"Interface";
                    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
                        return candidate;
                    }
                    break;
                }
                if (buf.size() >= 32768) {
                    break;
                }
                buf.resize(buf.size() * 2, L'\0');
            }
            return std::filesystem::path("Data/Interface");
        }

        void EnsureFallUIRegistryLoaded()
        {
            if (g_falluiAtlas.registryAttempted) {
                return;
            }
            g_falluiAtlas.registryAttempted = true;
            const auto interfaceDir = ResolveGameInterfaceDir();
            g_falluiAtlas.registryReady = g_falluiAtlas.registry.InitializeFromInterfaceDir(interfaceDir);
            std::error_code ec;
            g_falluiAtlas.hasBaseSwf = std::filesystem::exists(interfaceDir / "FallUI_IconLib.swf", ec);
            logger::info(
                "FallUI Icon Registry initialized: ready={} tags={} colors={} baseSwf={}",
                g_falluiAtlas.registryReady,
                g_falluiAtlas.registry.TagCount(),
                g_falluiAtlas.registry.ColorCount(),
                g_falluiAtlas.hasBaseSwf);
        }

        [[nodiscard]] std::string GetFallUIRegistryStatusText()
        {
            std::lock_guard lock{ g_falluiAtlasMutex };
            EnsureFallUIRegistryLoaded();
            if (!g_falluiAtlas.registryReady) {
                return "FIS XML not found in Data/Interface/ItemSorter; using FontAwesome fallback.";
            }
            if (!g_falluiAtlas.hasBaseSwf) {
                return "FallUI_IconLib.swf missing in Data/Interface; using FontAwesome fallback.";
            }
            return "Loaded " + std::to_string(g_falluiAtlas.registry.TagCount()) +
                   " FIS tags (" + std::to_string(g_falluiAtlas.nextSlot) + " icons cached in VRAM).";
        }

        [[nodiscard]] bool EnsureFallUID3DTexture(ID3D11Device* a_dev)
        {
            if (!a_dev) {
                return false;
            }
            if (g_falluiAtlas.device != a_dev) {
                DestroyFallUIAtlasResources();
                g_falluiAtlas.device = a_dev;
            }
            if (g_falluiAtlas.texture && g_falluiAtlas.srv) {
                return true;
            }

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = kFallUIAtlasPx;
            desc.Height = kFallUIAtlasPx;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            std::vector<std::uint32_t> emptyPixels(
                static_cast<std::size_t>(kFallUIAtlasPx) * static_cast<std::size_t>(kFallUIAtlasPx), 0u);
            D3D11_SUBRESOURCE_DATA initData{};
            initData.pSysMem = emptyPixels.data();
            initData.SysMemPitch = kFallUIAtlasPx * 4u;

            if (FAILED(a_dev->CreateTexture2D(&desc, &initData, &g_falluiAtlas.texture)) || !g_falluiAtlas.texture) {
                g_falluiAtlas.texture = nullptr;
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;

            if (FAILED(a_dev->CreateShaderResourceView(g_falluiAtlas.texture, &srvDesc, &g_falluiAtlas.srv)) ||
                !g_falluiAtlas.srv) {
                g_falluiAtlas.srv = nullptr;
                g_falluiAtlas.texture->Release();
                g_falluiAtlas.texture = nullptr;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool TryResolveFallUIAtlasTile(
            const ConfigData& a_cfg,
            std::string_view a_rawName,
            TargetInfo::ItemKind a_kind,
            ImGuiMCP::ImTextureID& a_outTex,
            FallUIAtlasSlot& a_outSlot,
            std::uint32_t& a_outRgb,
            bool& a_outHasColor)
        {
            if (a_cfg.iconLibrary != 1 || !g_drawImageSupported.load(std::memory_order_acquire)) {
                return false;
            }

            std::lock_guard lock{ g_falluiAtlasMutex };
            EnsureFallUIRegistryLoaded();
            if (!g_falluiAtlas.registryReady) {
                return false;
            }

            FallUIIcons::ResolvedFallUIIcon resolved;
            if (!g_falluiAtlas.registry.ResolveIcon(a_rawName, a_kind, resolved)) {
                return false;
            }

            auto* rd = RE::BSGraphics::RendererData::GetSingleton();
            if (!rd || !rd->initialized || !rd->device || !EnsureFallUID3DTexture(rd->device)) {
                return false;
            }

            const std::string tileKey = TargetInfo::ToUpperAscii(resolved.swfRelPath + "|" + resolved.symbolName);
            auto it = g_falluiAtlas.slots.find(tileKey);
            if (it == g_falluiAtlas.slots.end()) {
                if (g_falluiAtlas.nextSlot >= kFallUIMaxSlots) {
                    return false;
                }
                FallUIAtlasSlot newSlot{};
                std::vector<std::uint8_t> rgba;
                bool usedFallback = false;
                if (g_falluiAtlas.registry.RasterizeResolvedIcon(
                        resolved, FallUIIcons::kIconTileSize, rgba, &usedFallback) &&
                    rgba.size() == static_cast<std::size_t>(FallUIIcons::kIconTileSize) *
                                       static_cast<std::size_t>(FallUIIcons::kIconTileSize) * 4u) {
                    ID3D11DeviceContext* ctx = nullptr;
                    g_falluiAtlas.device->GetImmediateContext(&ctx);
                    if (ctx) {
                        const int slotIdx = g_falluiAtlas.nextSlot++;
                        const UINT tileX = static_cast<UINT>((slotIdx % kFallUIAtlasGrid) * FallUIIcons::kIconTileSize);
                        const UINT tileY = static_cast<UINT>((slotIdx / kFallUIAtlasGrid) * FallUIIcons::kIconTileSize);
                        const D3D11_BOX box{
                            tileX,
                            tileY,
                            0u,
                            tileX + static_cast<UINT>(FallUIIcons::kIconTileSize),
                            tileY + static_cast<UINT>(FallUIIcons::kIconTileSize),
                            1u
                        };
                        ctx->UpdateSubresource(
                            g_falluiAtlas.texture, 0u, &box, rgba.data(),
                            static_cast<UINT>(FallUIIcons::kIconTileSize * 4), 0u);
                        ctx->Release();

                        const float inv = 1.0f / static_cast<float>(kFallUIAtlasPx);
                        newSlot.uv0 = { static_cast<float>(tileX) * inv, static_cast<float>(tileY) * inv };
                        newSlot.uv1 = {
                            static_cast<float>(tileX + static_cast<UINT>(FallUIIcons::kIconTileSize)) * inv,
                            static_cast<float>(tileY + static_cast<UINT>(FallUIIcons::kIconTileSize)) * inv
                        };
                        newSlot.valid = true;
                        newSlot.usedFallback = usedFallback;
                    }
                }
                it = g_falluiAtlas.slots.emplace(tileKey, newSlot).first;
            }

            if (!it->second.valid) {
                return false;
            }

            a_outTex = reinterpret_cast<ImGuiMCP::ImTextureID>(g_falluiAtlas.srv);
            a_outSlot = it->second;
            a_outRgb = it->second.usedFallback ? resolved.fallbackRgbColor : resolved.rgbColor;
            a_outHasColor = it->second.usedFallback ? resolved.fallbackHasColor : resolved.hasColor;
            return true;
        }

        void DrawFallUIQuad(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImTextureID a_tex,
            const FallUIAtlasSlot& a_slot,
            ImGuiMCP::ImVec2 a_pos,
            float a_sizePx,
            std::uint32_t a_tintRgb,
            float a_alpha,
            bool a_shadow,
            float a_shadowScale)
        {
            if (!a_dl || !a_tex || !a_slot.valid || a_sizePx <= 0.0f || a_alpha <= 0.01f) {
                return;
            }
            const ImGuiMCP::ImVec2 pMax{ a_pos.x + a_sizePx, a_pos.y + a_sizePx };
            if (a_shadow) {
                const float sOff = std::max(1.0f, 1.2f * a_shadowScale);
                ImGuiMCP::ImDrawListManager::AddImage(
                    a_dl, a_tex,
                    { a_pos.x + sOff, a_pos.y + sOff },
                    { pMax.x + sOff, pMax.y + sOff },
                    a_slot.uv0, a_slot.uv1,
                    MakeCol(0, 0, 0, 0.72f * a_alpha));
            }
            ImGuiMCP::ImDrawListManager::AddImage(
                a_dl, a_tex, a_pos, pMax,
                a_slot.uv0, a_slot.uv1,
                MakeRgbCol(a_tintRgb, a_alpha));
        }

        [[nodiscard]] std::string TruncateTextToFit(
            ImGuiMCP::ImFont* a_font,
            float a_fontSize,
            const std::string& a_text,
            float a_maxWidthPx)
        {
            if (a_text.empty() || a_maxWidthPx <= 0.0f) {
                return a_text;
            }
            if (MeasureTextScaled(a_font, a_fontSize, a_text.c_str()).x <= a_maxWidthPx) {
                return a_text;
            }
            static constexpr const char* kEllipsis = "...";
            if (MeasureTextScaled(a_font, a_fontSize, kEllipsis).x > a_maxWidthPx) {
                return (a_maxWidthPx < 8.0f) ? std::string{} : std::string(kEllipsis);
            }
            std::size_t lo = 0;
            std::size_t hi = a_text.size();
            std::string best = kEllipsis;
            while (lo <= hi) {
                const std::size_t mid = lo + (hi - lo) / 2;
                std::string candidate = OverlayLayout::TruncateUtf8Prefix(a_text, mid) + kEllipsis;
                const float w = MeasureTextScaled(a_font, a_fontSize, candidate.c_str()).x;
                if (w <= a_maxWidthPx) {
                    best = std::move(candidate);
                    lo = mid + 1;
                } else {
                    if (mid == 0) {
                        break;
                    }
                    hi = mid - 1;
                }
            }
            return best;
        }

        [[nodiscard]] std::string FormatQuickLootRowLabel(
            ImGuiMCP::ImFont* a_font,
            float a_fontSize,
            const HUDController::QuickContainerItemEntry& a_item,
            bool a_cleanTags,
            float a_maxWidthPx)
        {
            std::string base = TargetInfo::CleanDisplayName(a_item.name, a_cleanTags);
            if (a_item.isLegendary) {
                base = "* " + base;
            }
            const std::string suffix = (a_item.count > 1) ? (" (" + std::to_string(a_item.count) + ")") : std::string{};
            const std::string full = base + suffix;
            if (a_maxWidthPx <= 0.0f || MeasureTextScaled(a_font, a_fontSize, full.c_str()).x <= a_maxWidthPx) {
                return full;
            }
            const float suffixW = suffix.empty() ? 0.0f : MeasureTextScaled(a_font, a_fontSize, suffix.c_str()).x;
            const float ellipsisW = MeasureTextScaled(a_font, a_fontSize, "...").x;
            if (!suffix.empty() && (a_maxWidthPx - suffixW) < std::max(16.0f, ellipsisW)) {
                return TruncateTextToFit(a_font, a_fontSize, full, a_maxWidthPx);
            }
            return TruncateTextToFit(a_font, a_fontSize, base, a_maxWidthPx - suffixW) + suffix;
        }

        [[nodiscard]] const char* FallbackKeyForIndex(std::size_t a_idx)
        {
            if (a_idx == 0) {
                return "E";
            }
            if (a_idx == 1) {
                return "R";
            }
            return "SPACE";
        }

        enum class GamepadFaceButton
        {
            kSouth,
            kWest,
            kNorth,
            kEast
        };

        [[nodiscard]] GamepadFaceButton ResolveGamepadFaceButton(
            const std::string& a_keyLabel,
            std::size_t a_actionIdx)
        {
            if (!a_keyLabel.empty()) {
                const std::string up = TargetInfo::ToUpperAscii(a_keyLabel);
                if (up == "SPACE" || up == "Y" || up == "TRIANGLE") {
                    return GamepadFaceButton::kNorth;
                }
                if (up == "TAB" || up == "B" || up == "CIRCLE" || up == "ESC") {
                    return GamepadFaceButton::kEast;
                }
                if (up == "R" || up == "X" || up == "SQUARE") {
                    return GamepadFaceButton::kWest;
                }
                if (up == "E" || up == "A" || up == "CROSS") {
                    return GamepadFaceButton::kSouth;
                }
            }
            if (a_actionIdx == 1) {
                return GamepadFaceButton::kWest;
            }
            if (a_actionIdx == 2) {
                return GamepadFaceButton::kNorth;
            }
            if (a_actionIdx >= 3) {
                return GamepadFaceButton::kEast;
            }
            return GamepadFaceButton::kSouth;
        }

        void DrawPlayStationSymbol(
            ImGuiMCP::ImDrawList* a_dl,
            const ImGuiMCP::ImVec2& a_center,
            float a_symR,
            float a_thick,
            GamepadFaceButton a_face,
            ImGuiMCP::ImU32 a_col)
        {
            switch (a_face) {
            case GamepadFaceButton::kWest: {
                const float sq = a_symR * 0.82f;
                ImGuiMCP::ImDrawListManager::AddRect(
                    a_dl, { a_center.x - sq, a_center.y - sq }, { a_center.x + sq, a_center.y + sq },
                    a_col, 0.0f, 0, a_thick);
                break;
            }
            case GamepadFaceButton::kNorth:
                ImGuiMCP::ImDrawListManager::AddTriangle(
                    a_dl,
                    { a_center.x, a_center.y - a_symR * 0.92f },
                    { a_center.x - a_symR * 0.92f, a_center.y + a_symR * 0.72f },
                    { a_center.x + a_symR * 0.92f, a_center.y + a_symR * 0.72f },
                    a_col, a_thick);
                break;
            case GamepadFaceButton::kEast:
                ImGuiMCP::ImDrawListManager::AddCircle(
                    a_dl, a_center, a_symR * 0.86f, a_col, 24, a_thick);
                break;
            case GamepadFaceButton::kSouth:
            default: {
                const float d = a_symR * 0.85f;
                ImGuiMCP::ImDrawListManager::AddLine(
                    a_dl, { a_center.x - d, a_center.y - d }, { a_center.x + d, a_center.y + d }, a_col, a_thick);
                ImGuiMCP::ImDrawListManager::AddLine(
                    a_dl, { a_center.x + d, a_center.y - d }, { a_center.x - d, a_center.y + d }, a_col, a_thick);
                break;
            }
            }
        }

        void DrawCenteredBoldGlyph(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImFont* a_font,
            float a_fontSize,
            const ImGuiMCP::ImVec2& a_center,
            ImGuiMCP::ImU32 a_col,
            const char* a_glyph)
        {
            if (!a_dl || !a_glyph || a_glyph[0] == '\0' || a_fontSize <= 0.0f) {
                return;
            }

            ImGuiMCP::ImFont* f = a_font ? a_font : ImGuiMCP::GetFont();
            if (f && f->FontSize <= 0.0f) {
                f = nullptr;
            }
            const std::uint32_t alphaByte = (a_col >> 24) & 0xFFu;
            if (alphaByte == 0) {
                return;
            }
            const float targetAlpha = static_cast<float>(alphaByte) / 255.0f;
            const float passAlpha = (targetAlpha >= 0.995f)
                ? 1.0f
                : (1.0f - std::pow(1.0f - targetAlpha, 0.25f));
            const auto passAlphaByte = static_cast<ImGuiMCP::ImU32>(
                std::clamp(std::lround(passAlpha * 255.0f), 1L, 255L));
            const ImGuiMCP::ImU32 passCol =
                (a_col & 0x00FFFFFFu) | (passAlphaByte << 24);

            ImGuiMCP::ImVec2 pos{ 0.0f, 0.0f };
            bool usedGlyphBounds = false;

            if (f && f->FontSize > 0.0f && a_glyph[1] == '\0' &&
                g_findGlyphSupported.load(std::memory_order_acquire)) {
                const auto* g = ImGuiMCP::ImFontManger::FindGlyph(
                    f, static_cast<ImGuiMCP::ImWchar>(static_cast<unsigned char>(a_glyph[0])));
                if (g && g->Visible && g->X1 > g->X0 && g->Y1 > g->Y0) {
                    const float scaleRatio = a_fontSize / f->FontSize;
                    pos = {
                        a_center.x - 0.5f * (g->X0 + g->X1) * scaleRatio,
                        a_center.y - 0.5f * (g->Y0 + g->Y1) * scaleRatio
                    };
                    usedGlyphBounds = true;
                }
            }

            if (!usedGlyphBounds) {
                const auto gSz = MeasureTextScaled(f, a_fontSize, a_glyph);
                pos = { a_center.x - gSz.x * 0.5f, a_center.y - gSz.y * 0.5f };
            }

            const float bx = std::clamp(a_fontSize * 0.045f, 0.45f, 1.05f);
            const float by = bx * 0.45f;
            const std::array<ImGuiMCP::ImVec2, 4> offsets{ {
                { pos.x - bx, pos.y },
                { pos.x + bx, pos.y },
                { pos.x, pos.y - by },
                { pos.x, pos.y + by }
            } };

            if (f) {
                const bool pushTex = NeedsCustomAtlasTexture(f);
                if (pushTex) {
                    ImGuiMCP::ImDrawListManager::PushTextureID(a_dl, f->ContainerAtlas->TexID);
                }
                for (const auto& p : offsets) {
                    ImGuiMCP::ImDrawListManager::AddText(a_dl, f, a_fontSize, p, passCol, a_glyph, nullptr, 0.0f, nullptr);
                }
                if (pushTex) {
                    ImGuiMCP::ImDrawListManager::PopTextureID(a_dl);
                }
            } else {
                for (const auto& p : offsets) {
                    ImGuiMCP::ImDrawListManager::AddText(a_dl, p, passCol, a_glyph, nullptr);
                }
            }
        }

        void DrawKeycapOrGamepadBadge(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImFont* a_font,
            const ImGuiMCP::ImVec2& a_center,
            float a_size,
            float a_keyTextScale,
            int a_buttonIconStyle,
            const std::string& a_keyLabel,
            std::size_t a_actionIdx,
            float a_rounding,
            bool a_shadow,
            float a_fade)
        {
            const float half = a_size * 0.5f;
            const float relScale = std::clamp(a_keyTextScale, 0.40f, 1.20f);
            const float shadowDx = 1.0f * (a_size / 20.0f);
            const float shadowDy = 1.5f * (a_size / 20.0f);
            const ImGuiMCP::ImVec2 pMin{ a_center.x - half, a_center.y - half };
            const ImGuiMCP::ImVec2 pMax{ a_center.x + half, a_center.y + half };

            if (a_buttonIconStyle >= 1 && a_buttonIconStyle <= 4) {
                const GamepadFaceButton face = ResolveGamepadFaceButton(a_keyLabel, a_actionIdx);
                const bool isPS = (a_buttonIconStyle == 1 || a_buttonIconStyle == 3);
                const bool isMono = (a_buttonIconStyle == 3 || a_buttonIconStyle == 4);

                const char* glyph = "A";
                switch (face) {
                case GamepadFaceButton::kWest:
                    glyph = isPS ? "[]" : "X";
                    break;
                case GamepadFaceButton::kNorth:
                    glyph = isPS ? "^" : "Y";
                    break;
                case GamepadFaceButton::kEast:
                    glyph = isPS ? "O" : "B";
                    break;
                case GamepadFaceButton::kSouth:
                default:
                    glyph = isPS ? "X" : "A";
                    break;
                }

                if (!isMono) {
                    ImGuiMCP::ImTextureID tex = nullptr;
                    ImGuiMCP::ImU32 circleCol = MakeCol(82, 186, 82, a_fade);
                    if (g_drawImageSupported.load(std::memory_order_acquire)) {
                        EnsureGamepadTexturesLoaded();
                    }
                    switch (face) {
                    case GamepadFaceButton::kWest:
                        tex = isPS ? g_gamepadCache.psSquare : g_gamepadCache.xboxX;
                        circleCol = isPS ? MakeCol(214, 114, 180, a_fade) : MakeCol(56, 152, 236, a_fade);
                        break;
                    case GamepadFaceButton::kNorth:
                        tex = isPS ? g_gamepadCache.psTriangle : g_gamepadCache.xboxY;
                        circleCol = isPS ? MakeCol(74, 194, 148, a_fade) : MakeCol(240, 192, 54, a_fade);
                        break;
                    case GamepadFaceButton::kEast:
                        tex = isPS ? g_gamepadCache.psCircle : g_gamepadCache.xboxB;
                        circleCol = isPS ? MakeCol(232, 92, 84, a_fade) : MakeCol(224, 72, 68, a_fade);
                        break;
                    case GamepadFaceButton::kSouth:
                    default:
                        tex = isPS ? g_gamepadCache.psCross : g_gamepadCache.xboxA;
                        circleCol = isPS ? MakeCol(104, 159, 255, a_fade) : MakeCol(82, 186, 82, a_fade);
                        break;
                    }

                    if (tex != nullptr && g_drawImageSupported.load(std::memory_order_acquire)) {
                        ImGuiMCP::ImDrawListManager::AddImage(
                            a_dl, tex, pMin, pMax, { 0.0f, 0.0f }, { 1.0f, 1.0f }, MakeCol(255, 255, 255, a_fade));
                        return;
                    }

                    if (g_circlePrimitivesSupported.load(std::memory_order_acquire)) {
                        if (a_shadow) {
                            ImGuiMCP::ImDrawListManager::AddCircleFilled(
                                a_dl, { a_center.x + shadowDx, a_center.y + shadowDy }, half, MakeCol(0, 0, 0, 0.55f * a_fade), 24);
                        }
                        ImGuiMCP::ImDrawListManager::AddCircleFilled(a_dl, a_center, half, circleCol, 24);
                        if (isPS) {
                            const float symR = half * std::clamp(relScale * 0.62f, 0.30f, 0.72f);
                            const float thick = std::clamp(symR * 0.34f, 1.0f, 3.5f);
                            DrawPlayStationSymbol(a_dl, a_center, symR, thick, face, MakeCol(255, 255, 255, a_fade));
                        } else {
                            const float glyphFontSize = a_size * std::clamp(relScale, 0.45f, 1.05f);
                            DrawCenteredBoldGlyph(
                                a_dl, a_font, glyphFontSize, a_center,
                                MakeCol(255, 255, 255, a_fade), glyph);
                        }
                        return;
                    }
                    // Intentional fallthrough to safe rounded keycap fallback if circle primitives are unavailable
                } else if (g_circlePrimitivesSupported.load(std::memory_order_acquire)) {
                    // Styles 3 & 4: Clean Monochrome (GTA 6 white badge + dark crisp glyph/symbol)
                    if (a_shadow) {
                        ImGuiMCP::ImDrawListManager::AddCircleFilled(
                            a_dl, { a_center.x + shadowDx, a_center.y + shadowDy }, half, MakeCol(0, 0, 0, 0.55f * a_fade), 28);
                    }
                    ImGuiMCP::ImDrawListManager::AddCircleFilled(
                        a_dl, a_center, half, MakeCol(246, 248, 252, 0.96f * a_fade), 28);
                    ImGuiMCP::ImDrawListManager::AddCircle(
                        a_dl, a_center, half, MakeCol(150, 162, 178, 0.72f * a_fade), 28, 1.0f);

                    const ImGuiMCP::ImU32 darkCol = MakeCol(16, 20, 26, a_fade);
                    if (isPS) {
                        const float symR = half * std::clamp(relScale * 0.62f, 0.30f, 0.72f);
                        const float thick = std::clamp(symR * 0.34f, 1.0f, 3.5f);
                        DrawPlayStationSymbol(a_dl, a_center, symR, thick, face, darkCol);
                        return;
                    }

                    const float glyphFontSize = a_size * std::clamp(relScale, 0.45f, 1.05f);
                    DrawCenteredBoldGlyph(a_dl, a_font, glyphFontSize, a_center, darkCol, glyph);
                    return;
                }
            }

            // Style 0 (and safe fallback if gamepad primitives are unsupported): crisp rounded keycap
            const std::string keyTxt = a_keyLabel.empty() ? std::string(FallbackKeyForIndex(a_actionIdx)) : a_keyLabel;
            const float capFontSize = a_size * relScale;
            const auto tSz = MeasureTextScaled(a_font, capFontSize, keyTxt.c_str());
            const float capHalfW = std::max(half, tSz.x * 0.5f + a_size * 0.20f);
            const float capHalfH = std::max(half, tSz.y * 0.42f + a_size * 0.08f);
            const ImGuiMCP::ImVec2 capMin{ a_center.x - capHalfW, a_center.y - capHalfH };
            const ImGuiMCP::ImVec2 capMax{ a_center.x + capHalfW, a_center.y + capHalfH };
            const float r = std::clamp(a_rounding * 0.65f, 0.0f, 10.0f);

            if (a_shadow) {
                ImGuiMCP::ImDrawListManager::AddRectFilled(
                    a_dl, { capMin.x + shadowDx, capMin.y + shadowDy }, { capMax.x + shadowDx, capMax.y + shadowDy },
                    MakeCol(0, 0, 0, 0.55f * a_fade), r, 0);
            }
            ImGuiMCP::ImDrawListManager::AddRectFilled(
                a_dl, capMin, capMax, MakeCol(246, 248, 252, 0.96f * a_fade), r, 0);
            ImGuiMCP::ImDrawListManager::AddRect(
                a_dl, capMin, capMax, MakeCol(150, 162, 178, 0.72f * a_fade), r, 0, 1.0f);
            DrawTextScaled(
                a_dl, a_font, capFontSize,
                { a_center.x - tSz.x * 0.5f, a_center.y - tSz.y * 0.5f },
                MakeCol(16, 20, 26, a_fade), keyTxt.c_str());
        }

        [[nodiscard]] float MeasureKeySpanW(
            ImGuiMCP::ImFont* a_font,
            float a_badgeSize,
            float a_keyTextScale,
            int a_buttonIconStyle,
            const std::string& a_keyLabel)
        {
            if (a_buttonIconStyle == 0) {
                const std::string keyTxt = a_keyLabel.empty() ? std::string("E") : a_keyLabel;
                const float relScale = std::clamp(a_keyTextScale, 0.40f, 1.20f);
                const auto kSz = MeasureTextScaled(a_font, a_badgeSize * relScale, keyTxt.c_str());
                return std::max(a_badgeSize, kSz.x + a_badgeSize * 0.40f);
            }
            return a_badgeSize;
        }

        [[nodiscard]] float MeasureKeyBadgeH(
            ImGuiMCP::ImFont* a_font,
            float a_badgeSize,
            float a_keyTextScale,
            int a_buttonIconStyle,
            const std::string& a_keyLabel)
        {
            if (a_buttonIconStyle == 0) {
                const std::string keyTxt = a_keyLabel.empty() ? std::string("E") : a_keyLabel;
                const float relScale = std::clamp(a_keyTextScale, 0.40f, 1.20f);
                const auto kSz = MeasureTextScaled(a_font, a_badgeSize * relScale, keyTxt.c_str());
                return std::max(a_badgeSize, kSz.y * 0.84f + a_badgeSize * 0.16f);
            }
            return a_badgeSize;
        }

        void DrawVanillaInlineSummaryPill(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImFont* a_font,
            float a_baseFontSize,
            float a_px,
            float a_py,
            float a_screenW,
            float a_screenH,
            float a_fade,
            const ConfigData& a_cfg,
            const TargetInfo::TargetMetadata& a_target,
            float& a_outPromptHalfW,
            float& a_outPromptHalfH)
        {
            const std::string summary = TargetInfo::FormatInlineSummary(a_target, a_cfg);
            if (summary.empty()) {
                return;
            }

            const float scale = std::clamp(a_cfg.infoCardScale, 0.75f, 1.8f);
            const float fontSize = a_baseFontSize * 0.85f * scale;
            const auto textSz = MeasureTextScaled(a_font, fontSize, summary.c_str());

            const float padX = 10.0f * scale;
            const float padY = 4.0f * scale;
            const float pillW = textSz.x + padX * 2.0f;
            const float pillH = textSz.y + padY * 2.0f;
            const float margin = EffectiveEdgeMargin(a_cfg, a_screenW, a_screenH);

            float x = a_px - pillW * 0.5f;
            float y = a_py + 28.0f * scale;
            if (a_cfg.enableClamping) {
                const float maxX = std::max(margin, a_screenW - margin - pillW);
                const float maxY = std::max(margin, a_screenH - margin - pillH);
                x = std::clamp(x, margin, maxX);
                y = std::clamp(y, margin, maxY);
            }

            const ImGuiMCP::ImVec2 pMin{ x, y };
            const ImGuiMCP::ImVec2 pMax{ x + pillW, y + pillH };

            ImGuiMCP::ImDrawListManager::AddRectFilled(
                a_dl, { x + 1.0f, y + 2.0f }, { x + pillW + 1.0f, y + pillH + 2.0f },
                MakeCol(0, 0, 0, 0.45f * a_fade), 4.0f * scale, 0);
            ImGuiMCP::ImDrawListManager::AddRectFilled(
                a_dl, pMin, pMax, MakeRgbCol(a_cfg.infoCardBgColor, 0.82f * a_fade), 4.0f * scale, 0);
            ImGuiMCP::ImDrawListManager::AddRect(
                a_dl, pMin, pMax, MakeCol(255, 255, 255, 0.16f * a_fade), 4.0f * scale, 0, 1.0f);
            DrawTextScaled(
                a_dl, a_font, fontSize, { x + padX, y + padY },
                MakeCol(235, 240, 246, a_fade), summary.c_str());

            a_outPromptHalfW = std::max(a_outPromptHalfW, pillW * 0.5f);
            a_outPromptHalfH = std::max(a_outPromptHalfH, (y + pillH) - a_py);
        }

        void DrawModernPrompt(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImFont* a_baseFont,
            float a_baseFontSize,
            float& a_ioAnchorX,
            float& a_ioAnchorY,
            float a_screenW,
            float a_screenH,
            const ConfigData& a_cfg,
            const HUDController::OverlaySnapshot& a_snap,
            float& a_outPromptHalfW,
            float& a_outPromptHalfH)
        {
            const float scale = std::clamp(a_cfg.modernScale, 0.5f, 2.5f);
            const float fade = std::clamp(a_snap.edgeFade, 0.0f, 1.0f);
            const float overallAlpha = std::clamp(a_cfg.modernOpacity, 0.1f, 1.0f) * fade;
            const float bgAlpha = std::clamp(a_cfg.modernBgOpacity, 0.0f, 1.0f) * overallAlpha;

            a_ioAnchorX += std::clamp(a_cfg.modernScreenOffsetX, -200.0f, 200.0f);
            a_ioAnchorY += std::clamp(a_cfg.modernScreenOffsetY, -200.0f, 200.0f);

            const float actionFontSize = a_baseFontSize * 0.92f * scale * std::clamp(a_cfg.modernActionTextScale, 0.5f, 2.5f);
            const float badgeSize = std::max(10.0f, a_baseFontSize * 1.06f * scale * std::clamp(a_cfg.modernButtonSize, 0.5f, 2.5f));
            const float iconFontSize = a_baseFontSize * 0.85f * scale * std::clamp(a_cfg.modernIconScale, 0.5f, 2.5f);
            const float keyTextScale = std::clamp(a_cfg.modernKeyTextScale, 0.40f, 1.20f);
            const float padX = std::clamp(a_cfg.modernPadX, 0.0f, 32.0f) * scale;
            const float padY = std::clamp(a_cfg.modernPadY, 0.0f, 24.0f) * scale;
            const float itemGap = std::clamp(a_cfg.modernItemGap, 2.0f, 24.0f) * scale;
            const float actionGap = std::clamp(a_cfg.modernActionGap, 0.0f, 24.0f) * scale;
            const float rounding = std::clamp(a_cfg.modernRounding, 0.0f, 24.0f) * scale;

            std::vector<HUDController::ButtonHintEntry> actions = a_snap.actions;
            if (actions.empty()) {
                actions.push_back({
                    a_snap.actionKey.empty() ? std::string("E") : a_snap.actionKey,
                    a_snap.actionVerb.empty() ? std::string("ACTIVATE") : a_snap.actionVerb,
                    true
                });
                if (!a_snap.secondaryVerb.empty()) {
                    actions.push_back({
                        a_snap.secondaryKey.empty() ? std::string("R") : a_snap.secondaryKey,
                        a_snap.secondaryVerb,
                        true
                    });
                }
            }

            std::string doorHeader;
            if (a_cfg.showDoorDestination && a_snap.target.isLoadDoor && !a_snap.target.doorDestination.empty()) {
                doorHeader = TargetInfo::ToUpperAscii(
                    TargetInfo::CleanDisplayName(a_snap.target.doorDestination, a_cfg.cleanSorterTags));
            }

            std::string name;
            if (doorHeader.empty() &&
                (a_cfg.modernTextMode == 1 || (a_cfg.modernTextMode == 2 && !a_snap.target.hasItemStats))) {
                name = TargetInfo::CleanDisplayName(a_snap.target.displayName, a_cfg.cleanSorterTags);
                if (!name.empty() && TargetInfo::ToUpperAscii(name) == actions[0].label) {
                    name.clear();
                }
            }

            const float doorHeaderFontSize = actionFontSize * 0.86f;
            const auto doorHeaderSz = doorHeader.empty()
                ? ImGuiMCP::ImVec2{ 0.0f, 0.0f }
                : MeasureTextScaled(a_baseFont, doorHeaderFontSize, doorHeader.c_str());
            const float doorHeaderBlockH = doorHeader.empty() ? 0.0f : (doorHeaderSz.y + 4.0f * scale);

            const auto nameSz = name.empty() ? ImGuiMCP::ImVec2{ 0.0f, 0.0f } : MeasureTextScaled(a_baseFont, actionFontSize, name.c_str());
            const float sepW = name.empty() ? 0.0f : (8.0f * scale);

            // Per-category icon check
            bool hasFallIcon = false;
            ImGuiMCP::ImTextureID fallTex = nullptr;
            FallUIAtlasSlot fallSlot{};
            std::uint32_t fallRgb = 0xFFFFFFu;
            bool fallHasColor = false;
            std::string iconUtf8;
            ImGuiMCP::ImVec2 iconSz{ 0.0f, 0.0f };
            if (OverlayLayout::IsCategoryIconEnabled(a_snap.target.itemKind, a_cfg)) {
                const std::string& rawForIcon = !a_snap.target.rawDisplayName.empty()
                    ? a_snap.target.rawDisplayName
                    : a_snap.target.displayName;
                hasFallIcon = TryResolveFallUIAtlasTile(
                    a_cfg, rawForIcon, a_snap.target.itemKind,
                    fallTex, fallSlot, fallRgb, fallHasColor);
                if (hasFallIcon) {
                    const float szPx = std::max(8.0f, iconFontSize * 1.05f);
                    iconSz = { szPx, szPx };
                } else {
                    const unsigned int cp = OverlayLayout::GetItemIconCodepoint(a_snap.target.itemKind);
                    iconUtf8 = FontAwesome::UnicodeToUtf8(cp);
                    FontAwesome::PushSolid();
                    ImGuiMCP::ImFont* faFont = ImGuiMCP::GetFont();
                    iconSz = MeasureTextScaled(faFont, iconFontSize, iconUtf8.c_str());
                    FontAwesome::Pop();
                }
            }

            std::vector<std::string> keyTexts(actions.size());
            std::vector<float> keySpans(actions.size(), badgeSize);
            std::vector<ImGuiMCP::ImVec2> verbSzs(actions.size());
            std::vector<float> rowWidths(actions.size(), 0.0f);
            std::vector<float> rowHeights(actions.size(), badgeSize);

            for (std::size_t i = 0; i < actions.size(); ++i) {
                keyTexts[i] = actions[i].key.empty() ? FallbackKeyForIndex(i) : actions[i].key;
                keySpans[i] = MeasureKeySpanW(a_baseFont, badgeSize, keyTextScale, a_cfg.buttonIconStyle, keyTexts[i]);
                verbSzs[i] = MeasureTextScaled(a_baseFont, actionFontSize, actions[i].label.c_str());
                float w = keySpans[i] + itemGap + verbSzs[i].x;
                float h = std::max(
                    MeasureKeyBadgeH(a_baseFont, badgeSize, keyTextScale, a_cfg.buttonIconStyle, keyTexts[i]),
                    verbSzs[i].y);
                if (i == 0) {
                    if (iconSz.x > 0.0f) {
                        w += iconSz.x + itemGap;
                        h = std::max(h, iconSz.y);
                    }
                    if (!name.empty()) {
                        w += sepW + nameSz.x;
                        h = std::max(h, nameSz.y);
                    }
                }
                rowWidths[i] = w;
                rowHeights[i] = h;
            }

            const bool stacked = (a_cfg.multiActionLayout == 0);
            float actionsContentW = 0.0f;
            float actionsContentH = 0.0f;
            const float inlineSep = actionGap + 10.0f * scale;
            if (stacked) {
                for (std::size_t i = 0; i < actions.size(); ++i) {
                    actionsContentW = std::max(actionsContentW, rowWidths[i]);
                    actionsContentH += rowHeights[i];
                    if (i + 1 < actions.size()) {
                        actionsContentH += actionGap;
                    }
                }
            } else {
                for (std::size_t i = 0; i < actions.size(); ++i) {
                    actionsContentW += rowWidths[i];
                    if (i + 1 < actions.size()) {
                        actionsContentW += inlineSep;
                    }
                    actionsContentH = std::max(actionsContentH, rowHeights[i]);
                }
            }

            const float contentW = std::max(actionsContentW, doorHeaderSz.x);
            const float contentH = doorHeaderBlockH + actionsContentH;

            const float pillW = padX * 2.0f + contentW;
            const float pillH = padY * 2.0f + contentH;

            const float margin = EffectiveEdgeMargin(a_cfg, a_screenW, a_screenH);
            float x = a_ioAnchorX - pillW * 0.5f;
            float y = a_ioAnchorY - pillH * 0.5f;
            if (a_cfg.enableClamping) {
                const float maxX = std::max(margin, a_screenW - margin - pillW);
                const float maxY = std::max(margin, a_screenH - margin - pillH);
                x = std::clamp(x, margin, maxX);
                y = std::clamp(y, margin, maxY);
            }
            a_ioAnchorX = x + pillW * 0.5f;
            a_ioAnchorY = y + pillH * 0.5f;

            const ImGuiMCP::ImVec2 pMin{ x, y };
            const ImGuiMCP::ImVec2 pMax{ x + pillW, y + pillH };

            if (bgAlpha > 0.005f) {
                if (a_cfg.modernTextShadow) {
                    ImGuiMCP::ImDrawListManager::AddRectFilled(
                        a_dl, { x + 1.5f * scale, y + 2.5f * scale }, { x + pillW + 1.5f * scale, y + pillH + 2.5f * scale },
                        MakeCol(0, 0, 0, 0.42f * bgAlpha), rounding, 0);
                }
                ImGuiMCP::ImDrawListManager::AddRectFilled(
                    a_dl, pMin, pMax, MakeRgbCol(a_cfg.modernBgColor, bgAlpha), rounding, 0);
            }
            if (a_cfg.modernBorder) {
                ImGuiMCP::ImDrawListManager::AddRect(
                    a_dl, pMin, pMax, MakeRgbCol(a_cfg.modernBorderColor, 0.35f * overallAlpha), rounding, 0, 1.0f);
            }

            auto drawSingleAction = [&](std::size_t a_idx, float a_startX, float a_centerY) {
                const float alphaMult = actions[a_idx].enabled ? 1.0f : 0.45f;
                const float rowAlpha = overallAlpha * alphaMult;
                float cx = a_startX;

                DrawKeycapOrGamepadBadge(
                    a_dl, a_baseFont, { cx + keySpans[a_idx] * 0.5f, a_centerY },
                    badgeSize, keyTextScale, a_cfg.buttonIconStyle, keyTexts[a_idx],
                    a_idx, rounding, a_cfg.modernTextShadow, rowAlpha);
                cx += keySpans[a_idx] + itemGap;

                if (a_idx == 0 && iconSz.x > 0.0f) {
                    const std::uint32_t baseColorRgb = OverlayLayout::GetItemIconColorRgb(
                        a_snap.target.itemKind, a_cfg.modernIconColor, a_cfg.coloredCategoryIcons);
                    const std::uint32_t iconRgb = (hasFallIcon && a_cfg.coloredCategoryIcons && fallHasColor)
                        ? fallRgb
                        : baseColorRgb;
                    if (hasFallIcon) {
                        DrawFallUIQuad(
                            a_dl, fallTex, fallSlot,
                            { cx, a_centerY - iconSz.y * 0.5f },
                            iconSz.x, iconRgb, rowAlpha, a_cfg.modernTextShadow, scale);
                    } else {
                        FontAwesome::PushSolid();
                        ImGuiMCP::ImFont* faFont = ImGuiMCP::GetFont();
                        DrawTextShadowed(
                            a_dl, faFont, iconFontSize,
                            { cx, a_centerY - iconSz.y * 0.5f },
                            MakeRgbCol(iconRgb, rowAlpha),
                            rowAlpha, a_cfg.modernTextShadow, scale, iconUtf8.c_str());
                        FontAwesome::Pop();
                    }
                    cx += iconSz.x + itemGap;
                }

                DrawTextShadowed(
                    a_dl, a_baseFont, actionFontSize,
                    { cx, a_centerY - verbSzs[a_idx].y * 0.5f },
                    MakeRgbCol(a_cfg.modernTextColor, rowAlpha),
                    rowAlpha, a_cfg.modernTextShadow, scale, actions[a_idx].label.c_str());
                cx += verbSzs[a_idx].x;

                if (a_idx == 0 && !name.empty()) {
                    const float midX = cx + sepW * 0.5f;
                    ImGuiMCP::ImDrawListManager::AddLine(
                        a_dl,
                        { midX, a_centerY - rowHeights[0] * 0.30f },
                        { midX, a_centerY + rowHeights[0] * 0.30f },
                        MakeRgbCol(a_cfg.modernNameColor, 0.35f * rowAlpha), 1.0f);
                    cx += sepW;
                    DrawTextShadowed(
                        a_dl, a_baseFont, actionFontSize,
                        { cx, a_centerY - nameSz.y * 0.5f },
                        MakeRgbCol(a_cfg.modernNameColor, 0.96f * rowAlpha),
                        rowAlpha, a_cfg.modernTextShadow, scale, name.c_str());
                }
            };

            float contentTopY = y + padY;
            if (!doorHeader.empty()) {
                const bool useHeaderShadow = a_cfg.modernTextShadow || bgAlpha < 0.2f;
                const float headerX = stacked ? (x + padX) : (x + (pillW - doorHeaderSz.x) * 0.5f);
                DrawTextShadowed(
                    a_dl, a_baseFont, doorHeaderFontSize, { headerX, contentTopY },
                    MakeRgbCol(a_cfg.modernAccentColor, overallAlpha),
                    overallAlpha, useHeaderShadow, scale, doorHeader.c_str());
                contentTopY += doorHeaderBlockH;
            }

            if (stacked) {
                float rowTop = contentTopY;
                for (std::size_t i = 0; i < actions.size(); ++i) {
                    drawSingleAction(i, x + padX, rowTop + rowHeights[i] * 0.5f);
                    rowTop += rowHeights[i] + actionGap;
                }
            } else {
                float curX = x + (pillW - actionsContentW) * 0.5f;
                const float centerY = contentTopY + actionsContentH * 0.5f;
                for (std::size_t i = 0; i < actions.size(); ++i) {
                    drawSingleAction(i, curX, centerY);
                    curX += rowWidths[i] + inlineSep;
                }
            }

            a_outPromptHalfW = pillW * 0.5f;
            a_outPromptHalfH = pillH * 0.5f;

            // Optional compact inline info line (off by default when Card is preferred)
            if (a_cfg.inlineItemInfo) {
                const std::string summary = TargetInfo::FormatInlineSummary(a_snap.target, a_cfg);
                if (!summary.empty()) {
                    const float subFontSize = a_baseFontSize * 0.78f * scale;
                    const auto subSz = MeasureTextScaled(a_baseFont, subFontSize, summary.c_str());
                    const float subPadX = 8.0f * scale;
                    const float subPadY = 3.0f * scale;
                    const float subW = subSz.x + subPadX * 2.0f;
                    const float subH = subSz.y + subPadY * 2.0f;
                    const auto subPos = OverlayLayout::ComputeInlineSubPillPos(
                        x, y, pillW, pillH, subW, subH, 4.0f * scale,
                        a_screenW, a_screenH, margin, a_cfg.enableClamping);

                    if (bgAlpha > 0.005f) {
                        ImGuiMCP::ImDrawListManager::AddRectFilled(
                            a_dl, { subPos.x, subPos.y }, { subPos.x + subW, subPos.y + subH },
                            MakeRgbCol(a_cfg.modernBgColor, bgAlpha * 0.92f), 4.0f * scale, 0);
                    }
                    if (a_cfg.modernBorder) {
                        ImGuiMCP::ImDrawListManager::AddRect(
                            a_dl, { subPos.x, subPos.y }, { subPos.x + subW, subPos.y + subH },
                            MakeRgbCol(a_cfg.modernBorderColor, 0.22f * overallAlpha), 4.0f * scale, 0, 1.0f);
                    }
                    DrawTextShadowed(
                        a_dl, a_baseFont, subFontSize, { subPos.x + subPadX, subPos.y + subPadY },
                        MakeRgbCol(a_cfg.modernNameColor, overallAlpha),
                        overallAlpha, a_cfg.modernTextShadow, scale, summary.c_str());

                    const float leftEdge = std::min(x, subPos.x);
                    const float rightEdge = std::max(x + pillW, subPos.x + subW);
                    const float topEdge = std::min(y, subPos.y);
                    const float bottomEdge = std::max(y + pillH, subPos.y + subH);
                    a_ioAnchorX = 0.5f * (leftEdge + rightEdge);
                    a_ioAnchorY = 0.5f * (topEdge + bottomEdge);
                    a_outPromptHalfW = 0.5f * (rightEdge - leftEdge);
                    a_outPromptHalfH = 0.5f * (bottomEdge - topEdge);
                }
            }
        }

        void DrawMiniInfoCard(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImFont* a_baseFont,
            float a_baseFontSize,
            float a_px,
            float a_py,
            float a_promptHalfW,
            float a_promptHalfH,
            float a_screenW,
            float a_screenH,
            const ConfigData& a_cfg,
            const HUDController::OverlaySnapshot& a_snap);

        void DrawModernQuickContainer(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImFont* a_baseFont,
            ImGuiMCP::ImFont* a_infoCardFont,
            float a_baseFontSize,
            float a_screenW,
            float a_screenH,
            const ConfigData& a_cfg,
            const HUDController::OverlaySnapshot& a_snap)
        {
            const auto& qc = a_snap.quickContainer;
            const float qcScale = std::clamp(a_cfg.qcScale, 0.5f, 2.5f);
            const float fade = std::clamp(qc.edgeFade, 0.0f, 1.0f);
            const float overallAlpha = std::clamp(a_cfg.modernOpacity, 0.1f, 1.0f) * fade;
            const float margin = EffectiveEdgeMargin(a_cfg, a_screenW, a_screenH);

            const float headerFontSize = a_baseFontSize * 0.94f * qcScale * std::clamp(a_cfg.qcHeaderTextScale, 0.5f, 2.5f);
            const float rowFontSize = a_baseFontSize * 0.85f * qcScale * std::clamp(a_cfg.qcItemTextScale, 0.5f, 2.5f);
            const float btnBadgeSize = std::max(10.0f, a_baseFontSize * 1.00f * qcScale * std::clamp(a_cfg.qcButtonSize, 0.5f, 2.5f));
            const float btnLabelFontSize = a_baseFontSize * 0.86f * qcScale * std::clamp(a_cfg.qcButtonTextScale, 0.5f, 2.5f);
            const float btnKeyTextScale = std::clamp(a_cfg.qcKeyTextScale, 0.40f, 1.20f);
            const float itemGap = std::clamp(a_cfg.modernItemGap, 2.0f, 24.0f) * qcScale;
            const float actionGap = std::clamp(a_cfg.modernActionGap, 0.0f, 24.0f) * qcScale;
            const float rounding = std::clamp(a_cfg.modernRounding, 0.0f, 24.0f) * qcScale;

            const std::string header = TargetInfo::ToUpperAscii(
                TargetInfo::CleanDisplayName(qc.headerTitle, a_cfg.cleanSorterTags));

            std::vector<HUDController::ButtonHintEntry> buttons = qc.buttons;
            if (buttons.empty()) {
                if (!qc.items.empty()) {
                    buttons.push_back({ "E", "TAKE", true });
                    buttons.push_back({ "R", "TRANSFER", true });
                } else if (qc.isWorkbench) {
                    buttons.push_back({ "E", "CRAFT", true });
                    buttons.push_back({ "R", "TRANSFER", true });
                } else {
                    buttons.push_back({ "R", "TRANSFER", true });
                }
            }

            // Case A: Empty / Workbench / Workshop QuickContainer (supports stacked multi-action!)
            if (qc.items.empty()) {
                const float padX = std::clamp(a_cfg.modernPadX, 0.0f, 32.0f) * qcScale;
                const float padY = std::clamp(a_cfg.modernPadY, 0.0f, 24.0f) * qcScale;
                const float bgAlpha = std::clamp(a_cfg.modernBgOpacity, 0.0f, 1.0f) * overallAlpha;
                const float titleFontSize = headerFontSize * 0.86f;

                const auto titleSz = header.empty()
                    ? ImGuiMCP::ImVec2{ 0.0f, 0.0f }
                    : MeasureTextScaled(a_baseFont, titleFontSize, header.c_str());

                std::vector<std::string> keyTxts(buttons.size());
                std::vector<float> keySpans(buttons.size(), btnBadgeSize);
                std::vector<ImGuiMCP::ImVec2> labelSzs(buttons.size());
                std::vector<float> btnWidths(buttons.size(), 0.0f);
                const float btnGap = 14.0f * qcScale;
                float inlineBtnW = 0.0f;
                float maxSingleBtnW = 0.0f;
                float singleBtnH = std::max(btnBadgeSize, btnLabelFontSize);

                for (std::size_t i = 0; i < buttons.size(); ++i) {
                    keyTxts[i] = buttons[i].key.empty() ? FallbackKeyForIndex(i) : buttons[i].key;
                    keySpans[i] = MeasureKeySpanW(a_baseFont, btnBadgeSize, btnKeyTextScale, a_cfg.buttonIconStyle, keyTxts[i]);
                    singleBtnH = std::max(
                        singleBtnH,
                        MeasureKeyBadgeH(a_baseFont, btnBadgeSize, btnKeyTextScale, a_cfg.buttonIconStyle, keyTxts[i]));
                    labelSzs[i] = MeasureTextScaled(a_baseFont, btnLabelFontSize, buttons[i].label.c_str());
                    btnWidths[i] = keySpans[i] + itemGap + labelSzs[i].x;
                    maxSingleBtnW = std::max(maxSingleBtnW, btnWidths[i]);
                    inlineBtnW += btnWidths[i];
                    if (i + 1 < buttons.size()) {
                        inlineBtnW += btnGap;
                    }
                }

                const bool stacked = (a_cfg.multiActionLayout == 0);
                const float btnsBlockW = stacked ? maxSingleBtnW : inlineBtnW;
                const float btnsBlockH = stacked
                    ? (static_cast<float>(buttons.size()) * singleBtnH +
                       (buttons.size() > 1 ? actionGap * static_cast<float>(buttons.size() - 1) : 0.0f))
                    : singleBtnH;

                const float innerW = std::max(titleSz.x, btnsBlockW);
                const float titleBlockH = header.empty() ? 0.0f : (titleSz.y + 4.0f * qcScale);
                const float boxW = padX * 2.0f + innerW;
                const float boxH = padY * 2.0f + titleBlockH + btnsBlockH;

                float x = qc.docked
                    ? (qc.normScreenX * a_screenW)
                    : (qc.normScreenX * a_screenW - boxW * 0.5f);
                float y = qc.normScreenY * a_screenH - boxH * 0.5f;
                if (a_cfg.enableClamping) {
                    x = std::clamp(x, margin, std::max(margin, a_screenW - margin - boxW));
                    y = std::clamp(y, margin, std::max(margin, a_screenH - margin - boxH));
                }

                if (bgAlpha > 0.005f) {
                    if (a_cfg.modernTextShadow) {
                        ImGuiMCP::ImDrawListManager::AddRectFilled(
                            a_dl, { x + 1.5f * qcScale, y + 2.5f * qcScale }, { x + boxW + 1.5f * qcScale, y + boxH + 2.5f * qcScale },
                            MakeCol(0, 0, 0, 0.42f * bgAlpha), rounding, 0);
                    }
                    ImGuiMCP::ImDrawListManager::AddRectFilled(
                        a_dl, { x, y }, { x + boxW, y + boxH }, MakeRgbCol(a_cfg.modernBgColor, bgAlpha), rounding, 0);
                }
                if (a_cfg.modernBorder) {
                    ImGuiMCP::ImDrawListManager::AddRect(
                        a_dl, { x, y }, { x + boxW, y + boxH },
                        MakeRgbCol(a_cfg.modernBorderColor, 0.35f * overallAlpha), rounding, 0, 1.0f);
                }

                float curY = y + padY;
                const bool useTextShadow = a_cfg.modernTextShadow || bgAlpha < 0.2f;
                if (!header.empty()) {
                    const float titleX = stacked ? (x + padX) : (x + (boxW - titleSz.x) * 0.5f);
                    DrawTextShadowed(
                        a_dl, a_baseFont, titleFontSize, { titleX, curY },
                        MakeRgbCol(a_cfg.modernAccentColor, overallAlpha),
                        overallAlpha, useTextShadow, qcScale, header.c_str());
                    curY += titleBlockH;
                }

                if (stacked) {
                    for (std::size_t i = 0; i < buttons.size(); ++i) {
                        const float alphaMult = buttons[i].enabled ? 1.0f : 0.45f;
                        const float btnCenterY = curY + singleBtnH * 0.5f;
                        float curX = x + padX;
                        DrawKeycapOrGamepadBadge(
                            a_dl, a_baseFont, { curX + keySpans[i] * 0.5f, btnCenterY },
                            btnBadgeSize, btnKeyTextScale, a_cfg.buttonIconStyle, keyTxts[i],
                            i, rounding, useTextShadow, overallAlpha * alphaMult);
                        curX += keySpans[i] + itemGap;
                        DrawTextShadowed(
                            a_dl, a_baseFont, btnLabelFontSize, { curX, btnCenterY - labelSzs[i].y * 0.5f },
                            MakeRgbCol(a_cfg.modernTextColor, overallAlpha * alphaMult),
                            overallAlpha * alphaMult, useTextShadow, qcScale, buttons[i].label.c_str());
                        curY += singleBtnH + actionGap;
                    }
                } else {
                    float curX = x + (boxW - inlineBtnW) * 0.5f;
                    const float btnCenterY = curY + singleBtnH * 0.5f;
                    for (std::size_t i = 0; i < buttons.size(); ++i) {
                        const float alphaMult = buttons[i].enabled ? 1.0f : 0.45f;
                        DrawKeycapOrGamepadBadge(
                            a_dl, a_baseFont, { curX + keySpans[i] * 0.5f, btnCenterY },
                            btnBadgeSize, btnKeyTextScale, a_cfg.buttonIconStyle, keyTxts[i],
                            i, rounding, useTextShadow, overallAlpha * alphaMult);
                        curX += keySpans[i] + itemGap;
                        DrawTextShadowed(
                            a_dl, a_baseFont, btnLabelFontSize, { curX, btnCenterY - labelSzs[i].y * 0.5f },
                            MakeRgbCol(a_cfg.modernTextColor, overallAlpha * alphaMult),
                            overallAlpha * alphaMult, useTextShadow, qcScale, buttons[i].label.c_str());
                        curX += labelSzs[i].x + btnGap;
                    }
                }
                return;
            }

            // Case B: Populated Container Loot List (fixed-width safe & QuickLoot InfoCard supported)
            const float pad = std::clamp(a_cfg.infoCardPad, 2.0f, 28.0f) * qcScale;
            const float rowGap = std::clamp(a_cfg.infoCardRowGap, 0.0f, 16.0f) * qcScale;
            const float rowPadY = std::clamp(a_cfg.qcRowHeightPad, 0.0f, 20.0f) * qcScale;
            const float bgAlpha = std::clamp(a_cfg.infoCardBgOpacity, 0.0f, 1.0f) * overallAlpha;
            const float panelRounding = std::clamp(a_cfg.infoCardRounding, 0.0f, 24.0f) * qcScale;
            const int visibleCount = static_cast<int>(qc.items.size());
            const int total = std::max(qc.totalItemCount, qc.windowStartIndex + visibleCount);
            const int sel = std::clamp(qc.selectedIndex, 0, std::max(0, total - 1));
            const bool hasMoreItems = (total > visibleCount) || (qc.windowStartIndex > 0);
            const bool fixedWidth = (a_cfg.qcWidth > 1.0f);
            const float fixedInnerW = fixedWidth ? std::max(120.0f * qcScale, a_cfg.qcWidth * qcScale) : 0.0f;

            std::string rangeStr;
            ImGuiMCP::ImVec2 rSz{ 0.0f, 0.0f };
            if (qc.hasExactTotal && hasMoreItems && total > 0) {
                rangeStr = std::to_string(sel + 1) + "/" + std::to_string(total);
                rSz = MeasureTextScaled(a_baseFont, rowFontSize * 0.9f, rangeStr.c_str());
            }

            const std::string rawHeader = header.empty() ? std::string("CONTAINER") : header;
            const float availHeaderW = fixedInnerW - (rSz.x > 0.0f ? (rSz.x + 12.0f * qcScale) : 0.0f);
            const float headerMaxW = fixedWidth ? std::max(12.0f, availHeaderW) : 0.0f;
            const std::string displayHeader = fixedWidth ? TruncateTextToFit(a_baseFont, headerFontSize, rawHeader, headerMaxW) : rawHeader;
            const auto headerSz = MeasureTextScaled(a_baseFont, headerFontSize, displayHeader.c_str());

            float maxRowW = 210.0f * qcScale;
            float rowsH = 0.0f;
            const float rowTextIndent = 5.0f * qcScale;
            const float rowIconSize = rowFontSize * 0.92f;
            const bool showTopEllipsis = qc.hasMoreAbove && visibleCount > 0;
            const bool showBottomEllipsis = qc.hasMoreBelow && visibleCount > 0;
            const float ellipsisFontSize = std::max(8.0f, rowFontSize * 0.85f);
            const ImGuiMCP::ImVec2 ellipsisSz = (showTopEllipsis || showBottomEllipsis)
                ? MeasureTextScaled(a_baseFont, ellipsisFontSize, "...")
                : ImGuiMCP::ImVec2{ 0.0f, 0.0f };
            const float ellipsisRowH = (showTopEllipsis || showBottomEllipsis)
                ? (ellipsisSz.y + 2.0f * qcScale)
                : 0.0f;
            bool hasRowIcons = false;
            bool hasAnyFallRow = false;
            bool hasAnyFaRow = false;
            float iconColW = 0.0f;
            ImGuiMCP::ImTextureID rowFallTex = nullptr;
            std::vector<std::string> rowIconUtf8(static_cast<std::size_t>(std::max(0, visibleCount)));
            std::vector<ImGuiMCP::ImVec2> rowIconSzs(static_cast<std::size_t>(std::max(0, visibleCount)));
            std::vector<FallUIAtlasSlot> rowFallSlots(static_cast<std::size_t>(std::max(0, visibleCount)));
            std::vector<std::uint32_t> rowFallRgb(static_cast<std::size_t>(std::max(0, visibleCount)), 0xFFFFFFu);
            std::vector<bool> rowFallHasColor(static_cast<std::size_t>(std::max(0, visibleCount)), false);

            if (a_cfg.qcShowItemIcons && visibleCount > 0) {
                const float fallIconPx = std::max(8.0f, rowIconSize * 1.12f);
                for (int i = 0; i < visibleCount; ++i) {
                    const std::size_t idx = static_cast<std::size_t>(i);
                    const auto& item = qc.items[idx];
                    if (item.itemKind == TargetInfo::ItemKind::None) {
                        continue;
                    }
                    ImGuiMCP::ImTextureID tex = nullptr;
                    FallUIAtlasSlot slot{};
                    std::uint32_t rgb = 0xFFFFFFu;
                    bool hasCol = false;
                    if (TryResolveFallUIAtlasTile(a_cfg, item.name, item.itemKind, tex, slot, rgb, hasCol)) {
                        rowFallTex = tex;
                        rowFallSlots[idx] = slot;
                        rowFallRgb[idx] = rgb;
                        rowFallHasColor[idx] = hasCol;
                        rowIconSzs[idx] = { fallIconPx, fallIconPx };
                        hasRowIcons = true;
                        hasAnyFallRow = true;
                    } else {
                        hasAnyFaRow = true;
                    }
                }
                if (hasAnyFaRow) {
                    hasRowIcons = true;
                    FontAwesome::PushSolid();
                    ImGuiMCP::ImFont* faFont = ImGuiMCP::GetFont();
                    for (int i = 0; i < visibleCount; ++i) {
                        const std::size_t idx = static_cast<std::size_t>(i);
                        const auto kind = qc.items[idx].itemKind;
                        if (kind != TargetInfo::ItemKind::None && !rowFallSlots[idx].valid) {
                            rowIconUtf8[idx] = FontAwesome::UnicodeToUtf8(OverlayLayout::GetItemIconCodepoint(kind));
                            rowIconSzs[idx] = MeasureTextScaled(faFont, rowIconSize, rowIconUtf8[idx].c_str());
                        }
                    }
                    FontAwesome::Pop();
                }
                if (hasRowIcons) {
                    iconColW = std::max(rowIconSize * 1.15f, hasAnyFallRow ? fallIconPx : 0.0f);
                    for (int i = 0; i < visibleCount; ++i) {
                        const std::size_t idx = static_cast<std::size_t>(i);
                        if (rowFallSlots[idx].valid || !rowIconUtf8[idx].empty()) {
                            iconColW = std::max(iconColW, rowIconSzs[idx].x);
                        }
                    }
                }
            }

            const float iconGap = 5.0f * qcScale;
            const float iconColSpan = hasRowIcons ? (iconColW + iconGap) : 0.0f;
            const float availRowW = fixedInnerW - rowTextIndent - iconColSpan - 4.0f * qcScale;
            const float rowTextMaxW = fixedWidth ? std::max(12.0f, availRowW) : 0.0f;
            std::vector<std::string> rowTexts;
            std::vector<ImGuiMCP::ImVec2> rowSzs;
            rowTexts.reserve(qc.items.size());
            rowSzs.reserve(qc.items.size());

            if (showTopEllipsis) {
                rowsH += ellipsisRowH + rowGap;
            }
            for (int i = 0; i < visibleCount; ++i) {
                const std::size_t idx = static_cast<std::size_t>(i);
                const auto& item = qc.items[idx];
                std::string label = FormatQuickLootRowLabel(a_baseFont, rowFontSize, item, a_cfg.cleanSorterTags, rowTextMaxW);
                const auto sz = MeasureTextScaled(a_baseFont, rowFontSize, label.c_str());
                maxRowW = std::max(maxRowW, sz.x + iconColSpan + 14.0f * qcScale);
                const bool rowHasIcon = hasRowIcons && (rowFallSlots[idx].valid || !rowIconUtf8[idx].empty());
                const float iconH = rowHasIcon ? std::max(rowIconSize, rowIconSzs[idx].y) : 0.0f;
                const float contentH = std::max(sz.y, iconH);
                rowsH += contentH + rowPadY;
                if (i + 1 < visibleCount) {
                    rowsH += rowGap;
                }
                rowTexts.push_back(std::move(label));
                rowSzs.push_back(sz);
            }
            if (showBottomEllipsis) {
                rowsH += rowGap + ellipsisRowH;
            }

            // Button bar sizing (supports wrapping cleanly onto extra rows if buttons overflow fixedWidth)
            const float btnGap = 12.0f * qcScale;
            const float btnRowInnerGap = 5.0f * qcScale;
            float singleBtnRowH = std::max(btnBadgeSize, btnLabelFontSize);
            std::vector<std::string> btnKeyTxts(buttons.size());
            std::vector<float> keySpans(buttons.size(), btnBadgeSize);
            std::vector<ImGuiMCP::ImVec2> btnLblSzs(buttons.size());
            std::vector<float> btnItemWidths(buttons.size(), 0.0f);
            float unWrappedBtnW = 0.0f;

            for (std::size_t i = 0; i < buttons.size(); ++i) {
                btnKeyTxts[i] = buttons[i].key.empty() ? FallbackKeyForIndex(i) : buttons[i].key;
                keySpans[i] = MeasureKeySpanW(a_baseFont, btnBadgeSize, btnKeyTextScale, a_cfg.buttonIconStyle, btnKeyTxts[i]);
                singleBtnRowH = std::max(
                    singleBtnRowH,
                    MeasureKeyBadgeH(a_baseFont, btnBadgeSize, btnKeyTextScale, a_cfg.buttonIconStyle, btnKeyTxts[i]));
                btnLblSzs[i] = MeasureTextScaled(a_baseFont, btnLabelFontSize, buttons[i].label.c_str());
                btnItemWidths[i] = keySpans[i] + itemGap * 0.85f + btnLblSzs[i].x;
                unWrappedBtnW += btnItemWidths[i];
                if (i + 1 < buttons.size()) {
                    unWrappedBtnW += btnGap;
                }
            }

            const float innerW = fixedWidth
                ? fixedInnerW
                : std::max({
                    headerSz.x + (rSz.x > 0.0f ? (rSz.x + 14.0f * qcScale) : 0.0f),
                    maxRowW,
                    unWrappedBtnW });

            // Pack buttons into rows bounded by innerW
            std::vector<std::vector<std::size_t>> footerRows;
            {
                std::vector<std::size_t> curRow;
                float curW = 0.0f;
                for (std::size_t i = 0; i < buttons.size(); ++i) {
                    const float addW = curRow.empty() ? btnItemWidths[i] : (btnGap + btnItemWidths[i]);
                    if (!curRow.empty() && (curW + addW) > innerW + 0.5f) {
                        footerRows.push_back(std::move(curRow));
                        curRow.clear();
                        curRow.push_back(i);
                        curW = btnItemWidths[i];
                    } else {
                        curRow.push_back(i);
                        curW += addW;
                    }
                }
                if (!curRow.empty()) {
                    footerRows.push_back(std::move(curRow));
                }
            }

            const float footerTotalH = footerRows.empty()
                ? singleBtnRowH
                : (static_cast<float>(footerRows.size()) * singleBtnRowH +
                   (footerRows.size() > 1 ? btnRowInnerGap * static_cast<float>(footerRows.size() - 1) : 0.0f));

            const float divGap = 7.0f * qcScale;
            const float panelW = pad * 2.0f + innerW;
            const float panelH = pad * 2.0f + headerSz.y + divGap * 2.0f + rowsH + divGap * 2.0f + footerTotalH;

            float x = qc.normScreenX * a_screenW;
            float y = qc.normScreenY * a_screenH - panelH * 0.15f;
            if (a_cfg.enableClamping) {
                x = std::clamp(x, margin, std::max(margin, a_screenW - margin - panelW));
                y = std::clamp(y, margin, std::max(margin, a_screenH - margin - panelH));
            }

            if (bgAlpha > 0.005f) {
                if (a_cfg.modernTextShadow) {
                    ImGuiMCP::ImDrawListManager::AddRectFilled(
                        a_dl, { x + 2.0f * qcScale, y + 3.0f * qcScale }, { x + panelW + 2.0f * qcScale, y + panelH + 3.0f * qcScale },
                        MakeCol(0, 0, 0, 0.45f * bgAlpha), panelRounding, 0);
                }
                ImGuiMCP::ImDrawListManager::AddRectFilled(
                    a_dl, { x, y }, { x + panelW, y + panelH }, MakeRgbCol(a_cfg.infoCardBgColor, bgAlpha), panelRounding, 0);
            }
            if (a_cfg.infoCardBorder) {
                ImGuiMCP::ImDrawListManager::AddRect(
                    a_dl, { x, y }, { x + panelW, y + panelH },
                    MakeRgbCol(a_cfg.infoCardBorderColor, 0.22f * overallAlpha), panelRounding, 0, 1.0f);
            }
            if (a_cfg.infoCardAccentBar) {
                ImGuiMCP::ImDrawListManager::AddLine(
                    a_dl, { x + panelRounding, y + 1.0f }, { x + panelW - panelRounding, y + 1.0f },
                    MakeRgbCol(a_cfg.modernAccentColor, 0.90f * overallAlpha), 2.0f);
            }

            const bool useCaseBShadow = a_cfg.modernTextShadow || bgAlpha < 0.2f;
            if (fixedWidth) {
                const float clipInset = std::max(0.0f, pad - (4.0f * qcScale + 2.0f));
                ImGuiMCP::ImDrawListManager::PushClipRect(
                    a_dl, { x + clipInset, y }, { x + panelW - clipInset, y + panelH }, true);
            }

            float curY = y + pad;
            DrawTextShadowed(
                a_dl, a_baseFont, headerFontSize, { x + pad, curY },
                MakeRgbCol(a_cfg.infoCardTitleColor, overallAlpha),
                overallAlpha, useCaseBShadow, qcScale, displayHeader.c_str());

            if (!rangeStr.empty()) {
                DrawTextShadowed(
                    a_dl, a_baseFont, rowFontSize * 0.9f,
                    { x + panelW - pad - rSz.x, curY + (headerSz.y - rSz.y) * 0.5f },
                    MakeRgbCol(a_cfg.infoCardLabelColor, overallAlpha),
                    overallAlpha, useCaseBShadow, qcScale, rangeStr.c_str());
            }

            curY += headerSz.y + divGap;
            ImGuiMCP::ImDrawListManager::AddLine(
                a_dl, { x + pad, curY }, { x + panelW - pad, curY },
                MakeRgbCol(a_cfg.infoCardBorderColor, 0.16f * overallAlpha), 1.0f);
            curY += divGap;

            const ImGuiMCP::ImU32 ellipsisCol = MakeRgbCol(a_cfg.infoCardLabelColor, 0.78f * overallAlpha);
            if (showTopEllipsis) {
                DrawTextShadowed(
                    a_dl, a_baseFont, ellipsisFontSize,
                    { x + pad + rowTextIndent + iconColSpan, curY + (ellipsisRowH - ellipsisSz.y) * 0.5f },
                    ellipsisCol, 0.78f * overallAlpha, useCaseBShadow, qcScale, "...");
                curY += ellipsisRowH + rowGap;
            }

            const float rowsStartY = curY;
            std::vector<float> rowHeights(rowTexts.size(), 0.0f);
            for (int idx = 0; idx < static_cast<int>(rowTexts.size()); ++idx) {
                const int globalIdx = qc.windowStartIndex + idx;
                const bool isSelected = (globalIdx == sel);
                const std::size_t uIdx = static_cast<std::size_t>(idx);
                const bool rowHasIcon =
                    hasRowIcons &&
                    ((uIdx < rowFallSlots.size() && rowFallSlots[uIdx].valid) || !rowIconUtf8[uIdx].empty());
                const float iconH = rowHasIcon ? std::max(rowIconSize, rowIconSzs[uIdx].y) : 0.0f;
                const float rowH = std::max(rowSzs[uIdx].y, iconH) + rowPadY;
                rowHeights[uIdx] = rowH;

                if (isSelected) {
                    ImGuiMCP::ImDrawListManager::AddRectFilled(
                        a_dl, { x + pad - 3.0f * qcScale, curY }, { x + panelW - pad + 3.0f * qcScale, curY + rowH },
                        MakeRgbCol(a_cfg.modernAccentColor, 0.24f * overallAlpha), 3.0f * qcScale, 0);
                    ImGuiMCP::ImDrawListManager::AddLine(
                        a_dl, { x + pad - 3.0f * qcScale, curY + 2.0f }, { x + pad - 3.0f * qcScale, curY + rowH - 2.0f },
                        MakeRgbCol(a_cfg.modernAccentColor, overallAlpha), 2.5f);
                }

                const float rowAlpha = isSelected ? overallAlpha : (0.88f * overallAlpha);
                const ImGuiMCP::ImU32 rowCol = isSelected
                    ? MakeRgbCol(a_cfg.modernTextColor, rowAlpha)
                    : MakeRgbCol(a_cfg.modernNameColor, rowAlpha);
                DrawTextShadowed(
                    a_dl, a_baseFont, rowFontSize,
                    { x + pad + rowTextIndent + iconColSpan, curY + (rowH - rowSzs[uIdx].y) * 0.5f },
                    rowCol, rowAlpha, useCaseBShadow, qcScale,
                    rowTexts[uIdx].c_str());

                curY += rowH + rowGap;
            }
            if (showBottomEllipsis) {
                DrawTextShadowed(
                    a_dl, a_baseFont, ellipsisFontSize,
                    { x + pad + rowTextIndent + iconColSpan, curY + (ellipsisRowH - ellipsisSz.y) * 0.5f },
                    ellipsisCol, 0.78f * overallAlpha, useCaseBShadow, qcScale, "...");
                curY += ellipsisRowH + rowGap;
            }

            if (hasRowIcons && !rowTexts.empty()) {
                if (hasAnyFallRow && rowFallTex) {
                    float iconCurY = rowsStartY;
                    for (int idx = 0; idx < static_cast<int>(rowTexts.size()); ++idx) {
                        const std::size_t uIdx = static_cast<std::size_t>(idx);
                        const float rowH = rowHeights[uIdx];
                        if (uIdx < rowFallSlots.size() && rowFallSlots[uIdx].valid) {
                            const int globalIdx = qc.windowStartIndex + idx;
                            const bool isSelected = (globalIdx == sel);
                            const float rowAlpha = isSelected ? overallAlpha : (0.88f * overallAlpha);
                            const std::uint32_t fallbackRgb = isSelected ? a_cfg.modernAccentColor : a_cfg.modernIconColor;
                            const std::uint32_t catRgb = OverlayLayout::GetItemIconColorRgb(
                                qc.items[uIdx].itemKind, fallbackRgb, a_cfg.coloredCategoryIcons);
                            const std::uint32_t iconRgb = (a_cfg.coloredCategoryIcons && rowFallHasColor[uIdx])
                                ? rowFallRgb[uIdx]
                                : catRgb;
                            const float iconCenterX = x + pad + rowTextIndent + iconColW * 0.5f;
                            const float iconPosX = iconCenterX - rowIconSzs[uIdx].x * 0.5f;
                            const float iconPosY = iconCurY + (rowH - rowIconSzs[uIdx].y) * 0.5f;
                            DrawFallUIQuad(
                                a_dl, rowFallTex, rowFallSlots[uIdx],
                                { iconPosX, iconPosY }, rowIconSzs[uIdx].x,
                                iconRgb, rowAlpha, useCaseBShadow, qcScale);
                        }
                        iconCurY += rowH + rowGap;
                    }
                }
                if (hasAnyFaRow) {
                    FontAwesome::PushSolid();
                    ImGuiMCP::ImFont* faFont = ImGuiMCP::GetFont();
                    float iconCurY = rowsStartY;
                    for (int idx = 0; idx < static_cast<int>(rowTexts.size()); ++idx) {
                        const std::size_t uIdx = static_cast<std::size_t>(idx);
                        const float rowH = rowHeights[uIdx];
                        if (!rowIconUtf8[uIdx].empty()) {
                            const int globalIdx = qc.windowStartIndex + idx;
                            const bool isSelected = (globalIdx == sel);
                            const float rowAlpha = isSelected ? overallAlpha : (0.88f * overallAlpha);
                            const std::uint32_t fallbackRgb = isSelected ? a_cfg.modernAccentColor : a_cfg.modernIconColor;
                            const std::uint32_t iconRgb = OverlayLayout::GetItemIconColorRgb(
                                qc.items[uIdx].itemKind, fallbackRgb, a_cfg.coloredCategoryIcons);
                            const float iconCenterX = x + pad + rowTextIndent + iconColW * 0.5f;
                            const float iconPosX = iconCenterX - rowIconSzs[uIdx].x * 0.5f;
                            const float iconPosY = iconCurY + (rowH - rowIconSzs[uIdx].y) * 0.5f;
                            DrawTextShadowed(
                                a_dl, faFont, rowIconSize, { iconPosX, iconPosY },
                                MakeRgbCol(iconRgb, rowAlpha), rowAlpha, useCaseBShadow, qcScale,
                                rowIconUtf8[uIdx].c_str());
                        }
                        iconCurY += rowH + rowGap;
                    }
                    FontAwesome::Pop();
                }
            }

            curY += divGap - rowGap;
            ImGuiMCP::ImDrawListManager::AddLine(
                a_dl, { x + pad, curY }, { x + panelW - pad, curY },
                MakeRgbCol(a_cfg.infoCardBorderColor, 0.16f * overallAlpha), 1.0f);
            curY += divGap;

            for (const auto& fRow : footerRows) {
                float curX = x + pad;
                const float btnCenterY = curY + singleBtnRowH * 0.5f;
                for (const std::size_t i : fRow) {
                    const float alphaMult = buttons[i].enabled ? 1.0f : 0.45f;
                    DrawKeycapOrGamepadBadge(
                        a_dl, a_baseFont, { curX + keySpans[i] * 0.5f, btnCenterY },
                        btnBadgeSize, btnKeyTextScale, a_cfg.buttonIconStyle, btnKeyTxts[i],
                        i, rounding, useCaseBShadow, overallAlpha * alphaMult);
                    curX += keySpans[i] + itemGap * 0.85f;
                    DrawTextShadowed(
                        a_dl, a_baseFont, btnLabelFontSize, { curX, btnCenterY - btnLblSzs[i].y * 0.5f },
                        MakeRgbCol(a_cfg.modernTextColor, overallAlpha * alphaMult),
                        overallAlpha * alphaMult, useCaseBShadow, qcScale, buttons[i].label.c_str());
                    curX += btnLblSzs[i].x + btnGap;
                }
                curY += singleBtnRowH + btnRowInnerGap;
            }

            if (fixedWidth) {
                ImGuiMCP::ImDrawListManager::PopClipRect(a_dl);
            }

            // Render Mini Info Card for the currently selected QuickLoot item
            if (a_cfg.infoCardInQuickContainer && a_cfg.infoCardPosition != 0 && qc.hasSelectedItemMeta) {
                HUDController::OverlaySnapshot cardSnap{};
                cardSnap.visible = true;
                cardSnap.edgeFade = qc.edgeFade;
                cardSnap.targetFirstSeenMs = qc.selectedItemFirstSeenMs;
                cardSnap.target = qc.selectedItemMeta;
                DrawMiniInfoCard(
                    a_dl, a_infoCardFont, a_baseFontSize,
                    x + panelW * 0.5f, y + panelH * 0.5f,
                    panelW * 0.5f, panelH * 0.5f,
                    a_screenW, a_screenH, a_cfg, cardSnap);
            }
        }

        void DrawMiniInfoCard(
            ImGuiMCP::ImDrawList* a_dl,
            ImGuiMCP::ImFont* a_baseFont,
            float a_baseFontSize,
            float a_px,
            float a_py,
            float a_promptHalfW,
            float a_promptHalfH,
            float a_screenW,
            float a_screenH,
            const ConfigData& a_cfg,
            const HUDController::OverlaySnapshot& a_snap)
        {
            if (a_cfg.infoCardPosition == 0) {
                return;
            }

            float delayAlpha = 1.0f;
            if (a_cfg.infoCardDelaySeconds > 0.001f) {
                if (a_snap.targetFirstSeenMs <= 0) {
                    return;
                }
                delayAlpha = OverlayLayout::ComputeHoverDelayAlpha(
                    SteadyNowMs() - a_snap.targetFirstSeenMs,
                    a_cfg.infoCardDelaySeconds);
            }
            if (delayAlpha <= 0.005f) {
                return;
            }

            const auto rows = TargetInfo::BuildInfoCardRows(a_snap.target, a_cfg);
            if (rows.empty()) {
                return;
            }

            const float scale = std::clamp(a_cfg.infoCardScale, 0.5f, 2.5f);
            const float fade = std::clamp(a_snap.edgeFade, 0.0f, 1.0f);
            const float overallAlpha = std::clamp(a_cfg.infoCardOpacity, 0.1f, 1.0f) * fade * delayAlpha;
            const float bgAlpha = std::clamp(a_cfg.infoCardBgOpacity, 0.0f, 1.0f) * overallAlpha;
            const bool textShadow = a_cfg.modernTextShadow || (bgAlpha < 0.25f);

            const float titleFontSize = a_baseFontSize * 0.94f * scale;
            const float typeFontSize = a_baseFontSize * 0.74f * scale;
            const float rowFontSize = a_baseFontSize * 0.84f * scale;
            const float pad = std::clamp(a_cfg.infoCardPad, 2.0f, 28.0f) * scale;
            const float colGap = 18.0f * scale;
            const float rowGap = std::clamp(a_cfg.infoCardRowGap, 0.0f, 16.0f) * scale;
            const float rounding = std::clamp(a_cfg.infoCardRounding, 0.0f, 24.0f) * scale;

            const std::string cleanName = TargetInfo::CleanDisplayName(a_snap.target.displayName, a_cfg.cleanSorterTags);
            const std::string headerTitle = cleanName.empty()
                ? std::string("INSPECT")
                : TargetInfo::ToUpperAscii(cleanName);
            const std::string& typeLabel = a_snap.target.typeLabel;

            bool hasFallCardIcon = false;
            ImGuiMCP::ImTextureID fallCardTex = nullptr;
            FallUIAtlasSlot fallCardSlot{};
            std::uint32_t fallCardRgb = 0xFFFFFFu;
            bool fallCardHasColor = false;
            std::string iconUtf8;
            ImGuiMCP::ImVec2 iconSz{ 0.0f, 0.0f };
            if (a_cfg.infoCardShowIcon) {
                const std::string& rawForCard = !a_snap.target.rawDisplayName.empty()
                    ? a_snap.target.rawDisplayName
                    : a_snap.target.displayName;
                hasFallCardIcon = TryResolveFallUIAtlasTile(
                    a_cfg, rawForCard, a_snap.target.itemKind,
                    fallCardTex, fallCardSlot, fallCardRgb, fallCardHasColor);
                if (hasFallCardIcon) {
                    const float szPx = std::max(8.0f, titleFontSize * 0.98f);
                    iconSz = { szPx, szPx };
                } else {
                    const unsigned int cp = OverlayLayout::GetItemIconCodepoint(a_snap.target.itemKind);
                    iconUtf8 = FontAwesome::UnicodeToUtf8(cp);
                    FontAwesome::PushSolid();
                    ImGuiMCP::ImFont* faFont = ImGuiMCP::GetFont();
                    iconSz = MeasureTextScaled(faFont, titleFontSize * 0.92f, iconUtf8.c_str());
                    FontAwesome::Pop();
                }
            }

            const auto titleSz = MeasureTextScaled(a_baseFont, titleFontSize, headerTitle.c_str());
            const auto typeSz = typeLabel.empty()
                ? ImGuiMCP::ImVec2{ 0.0f, 0.0f }
                : MeasureTextScaled(a_baseFont, typeFontSize, typeLabel.c_str());

            float maxLabelW = 0.0f;
            float maxValW = 0.0f;
            float rowsTotalH = 0.0f;
            std::vector<ImGuiMCP::ImVec2> labelSizes(rows.size());
            std::vector<ImGuiMCP::ImVec2> valueSizes(rows.size());
            for (std::size_t i = 0; i < rows.size(); ++i) {
                labelSizes[i] = MeasureTextScaled(a_baseFont, rowFontSize, rows[i].label.c_str());
                valueSizes[i] = MeasureTextScaled(a_baseFont, rowFontSize, rows[i].value.c_str());
                maxLabelW = std::max(maxLabelW, labelSizes[i].x);
                maxValW = std::max(maxValW, valueSizes[i].x);
                rowsTotalH += std::max(labelSizes[i].y, valueSizes[i].y);
                if (i + 1 < rows.size()) {
                    rowsTotalH += rowGap;
                }
            }

            const float iconSpan = (iconSz.x > 0.0f) ? (iconSz.x + 8.0f * scale) : 0.0f;
            const float headerTextH = titleSz.y + (typeLabel.empty() ? 0.0f : (2.0f * scale + typeSz.y));
            const float headerH = std::max(iconSz.y, headerTextH);
            const float innerW = std::max({ 170.0f * scale, iconSpan + titleSz.x, iconSpan + typeSz.x, maxLabelW + colGap + maxValW });
            const float dividerGap = 8.0f * scale;

            const float cardW = innerW + pad * 2.0f;
            const float cardH = pad * 2.0f + headerH + dividerGap * 2.0f + rowsTotalH;
            const float margin = EffectiveEdgeMargin(a_cfg, a_screenW, a_screenH);

            const auto rect = OverlayLayout::ComputeInfoCardRect(
                a_px, a_py, a_promptHalfW, a_promptHalfH,
                cardW, cardH, a_screenW, a_screenH, margin,
                a_cfg.infoCardPosition, a_cfg.infoCardOffsetX, a_cfg.infoCardOffsetY,
                a_cfg.enableClamping);

            const ImGuiMCP::ImVec2 pMin{ rect.x, rect.y };
            const ImGuiMCP::ImVec2 pMax{ rect.x + rect.w, rect.y + rect.h };

            if (bgAlpha > 0.005f) {
                ImGuiMCP::ImDrawListManager::AddRectFilled(
                    a_dl, { rect.x + 2.0f * scale, rect.y + 3.0f * scale }, { rect.x + rect.w + 2.0f * scale, rect.y + rect.h + 3.0f * scale },
                    MakeCol(0, 0, 0, 0.45f * bgAlpha), rounding, 0);
                ImGuiMCP::ImDrawListManager::AddRectFilled(
                    a_dl, pMin, pMax, MakeRgbCol(a_cfg.infoCardBgColor, bgAlpha), rounding, 0);
            }
            if (a_cfg.infoCardBorder) {
                ImGuiMCP::ImDrawListManager::AddRect(
                    a_dl, pMin, pMax, MakeRgbCol(a_cfg.infoCardBorderColor, 0.20f * overallAlpha), rounding, 0, 1.0f);
            }
            if (a_cfg.infoCardAccentBar) {
                ImGuiMCP::ImDrawListManager::AddLine(
                    a_dl, { rect.x + rounding, rect.y + 1.0f }, { rect.x + rect.w - rounding, rect.y + 1.0f },
                    MakeRgbCol(a_cfg.infoCardAccentColor, 0.85f * overallAlpha), 2.0f);
            }

            float cursorY = rect.y + pad;

            if (iconSz.x > 0.0f) {
                const std::uint32_t baseCardRgb = OverlayLayout::GetItemIconColorRgb(
                    a_snap.target.itemKind, a_cfg.infoCardAccentColor, a_cfg.coloredCategoryIcons);
                const std::uint32_t cardIconRgb = (hasFallCardIcon && a_cfg.coloredCategoryIcons && fallCardHasColor)
                    ? fallCardRgb
                    : baseCardRgb;
                if (hasFallCardIcon) {
                    DrawFallUIQuad(
                        a_dl, fallCardTex, fallCardSlot,
                        { rect.x + pad, cursorY + (headerH - iconSz.y) * 0.5f },
                        iconSz.x, cardIconRgb, overallAlpha, textShadow, scale);
                } else {
                    FontAwesome::PushSolid();
                    ImGuiMCP::ImFont* faFont = ImGuiMCP::GetFont();
                    DrawTextShadowed(
                        a_dl, faFont, titleFontSize * 0.92f,
                        { rect.x + pad, cursorY + (headerH - iconSz.y) * 0.5f },
                        MakeRgbCol(cardIconRgb, overallAlpha),
                        overallAlpha, textShadow, scale, iconUtf8.c_str());
                    FontAwesome::Pop();
                }
            }

            DrawTextShadowed(
                a_dl, a_baseFont, titleFontSize,
                { rect.x + pad + iconSpan, cursorY },
                MakeRgbCol(a_cfg.infoCardTitleColor, overallAlpha),
                overallAlpha, textShadow, scale, headerTitle.c_str());
            if (!typeLabel.empty()) {
                DrawTextShadowed(
                    a_dl, a_baseFont, typeFontSize,
                    { rect.x + pad + iconSpan, cursorY + titleSz.y + 2.0f * scale },
                    MakeRgbCol(a_cfg.infoCardLabelColor, overallAlpha),
                    overallAlpha, textShadow, scale, typeLabel.c_str());
            }

            cursorY += headerH + dividerGap;
            ImGuiMCP::ImDrawListManager::AddLine(
                a_dl, { rect.x + pad, cursorY }, { rect.x + rect.w - pad, cursorY },
                MakeRgbCol(a_cfg.infoCardBorderColor, 0.15f * overallAlpha), 1.0f);
            cursorY += dividerGap;

            for (std::size_t i = 0; i < rows.size(); ++i) {
                DrawTextShadowed(
                    a_dl, a_baseFont, rowFontSize,
                    { rect.x + pad, cursorY },
                    MakeRgbCol(a_cfg.infoCardLabelColor, overallAlpha),
                    overallAlpha, textShadow, scale, rows[i].label.c_str());

                const float valX = rect.x + rect.w - pad - valueSizes[i].x;
                DrawTextShadowed(
                    a_dl, a_baseFont, rowFontSize,
                    { valX, cursorY },
                    MakeRgbCol(a_cfg.infoCardValueColor, overallAlpha),
                    overallAlpha, textShadow, scale, rows[i].value.c_str());

                cursorY += std::max(labelSizes[i].y, valueSizes[i].y) + rowGap;
            }
        }

        void __stdcall RenderHudOverlay()
        {
            FlushDeferredFontFrees(false);

            if (F4SEMenuFramework::IsAnyBlockingWindowOpened()) {
                return;
            }

            auto* ui = RE::UI::GetSingleton();
            if (!ui || !ui->GetRuntimeData().menuSystemVisible || ui->GetMenuModeCount() > 0) {
                return;
            }

            static const RE::BSFixedString kHudMenu("HUDMenu");
            static const RE::BSFixedString kDialogueMenu("DialogueMenu");
            static const RE::BSFixedString kVATSMenu("VATSMenu");
            static const RE::BSFixedString kWorkshopMenu("WorkshopMenu");
            static const RE::BSFixedString kLockpickingMenu("LockpickingMenu");
            static const RE::BSFixedString kTerminalMenu("TerminalMenu");
            static const RE::BSFixedString kLoadingMenu("LoadingMenu");
            static const RE::BSFixedString kMainMenu("MainMenu");

            if (!ui->GetMenuOpen(kHudMenu) ||
                ui->GetMenuOpen(kDialogueMenu) ||
                ui->GetMenuOpen(kVATSMenu) ||
                ui->GetMenuOpen(kWorkshopMenu) ||
                ui->GetMenuOpen(kLockpickingMenu) ||
                ui->GetMenuOpen(kTerminalMenu) ||
                ui->GetMenuOpen(kLoadingMenu) ||
                ui->GetMenuOpen(kMainMenu)) {
                return;
            }

            const ConfigData cfg = Config::GetSingleton().GetSnapshot();
            if (!cfg.enabled) {
                return;
            }

            const HUDController::OverlaySnapshot snap = HUDController::Controller::GetSingleton().GetOverlaySnapshot();
            if (!snap.visible) {
                return;
            }

            if (snap.updateTimeMs == 0 || (SteadyNowMs() - snap.updateTimeMs) > 250) {
                return;
            }

            auto* io = ImGuiMCP::GetIO();
            auto* dl = ImGuiMCP::GetForegroundDrawList();
            if (!io || !dl || io->DisplaySize.x <= 1.0f || io->DisplaySize.y <= 1.0f) {
                return;
            }

            ImGuiMCP::ImFont* baseFont = ImGuiMCP::GetFont();
            const float baseFontSize = std::max(14.0f, ImGuiMCP::GetFontSize());
            const float screenW = io->DisplaySize.x;
            const float screenH = io->DisplaySize.y;

            if (snap.quickContainerVisible && snap.quickContainer.active &&
                cfg.promptStyle == 1 && cfg.modernQuickContainer) {
                if (snap.quickContainer.edgeFade > 0.01f) {
                    ImGuiMCP::ImFont* qcFont = ResolveCustomFont(cfg.qcFont, baseFont, cfg);
                    ImGuiMCP::ImFont* infoCardFont = ResolveCustomFont(cfg.infoCardFont, baseFont, cfg);
                    DrawModernQuickContainer(dl, qcFont, infoCardFont, baseFontSize, screenW, screenH, cfg, snap);
                }
                return;
            }

            if (snap.quickContainerVisible || snap.edgeFade <= 0.01f) {
                return;
            }

            ImGuiMCP::ImFont* promptFont = ResolveCustomFont(cfg.promptFont, baseFont, cfg);
            ImGuiMCP::ImFont* infoCardFont = ResolveCustomFont(cfg.infoCardFont, baseFont, cfg);

            float px = snap.normScreenX * screenW;
            float py = snap.normScreenY * screenH;

            float promptHalfW = 55.0f;
            float promptHalfH = 18.0f;

            if (cfg.promptStyle == 1) {
                DrawModernPrompt(
                    dl, promptFont, baseFontSize,
                    px, py, screenW, screenH,
                    cfg, snap, promptHalfW, promptHalfH);
            } else if (cfg.inlineItemInfo) {
                DrawVanillaInlineSummaryPill(
                    dl, promptFont, baseFontSize,
                    px, py, screenW, screenH,
                    snap.edgeFade,
                    cfg, snap.target, promptHalfW, promptHalfH);
            }

            DrawMiniInfoCard(
                dl, infoCardFont, baseFontSize,
                px, py, promptHalfW, promptHalfH,
                screenW, screenH, cfg, snap);
        }

        bool DrawColorPickerRGB(const char* a_label, std::uint32_t& a_rgb)
        {
            float col[4]{
                static_cast<float>((a_rgb >> 16) & 0xFFu) / 255.0f,
                static_cast<float>((a_rgb >> 8) & 0xFFu) / 255.0f,
                static_cast<float>(a_rgb & 0xFFu) / 255.0f,
                1.0f
            };
            if (ImGuiMCP::ColorEdit4(a_label, col, ImGuiMCP::ImGuiColorEditFlags_NoAlpha)) {
                const auto r = static_cast<std::uint32_t>(std::clamp(static_cast<int>(col[0] * 255.0f + 0.5f), 0, 255));
                const auto g = static_cast<std::uint32_t>(std::clamp(static_cast<int>(col[1] * 255.0f + 0.5f), 0, 255));
                const auto b = static_cast<std::uint32_t>(std::clamp(static_cast<int>(col[2] * 255.0f + 0.5f), 0, 255));
                a_rgb = (r << 16) | (g << 8) | b;
                return true;
            }
            return false;
        }

        bool DrawVec3Sliders(const char* a_label, Vector3& a_vec, float a_min, float a_max)
        {
            float tmp[3]{ a_vec.x, a_vec.y, a_vec.z };
            if (ImGuiMCP::SliderFloat3(a_label, tmp, a_min, a_max, "%.1f")) {
                a_vec.x = tmp[0];
                a_vec.y = tmp[1];
                a_vec.z = tmp[2];
                return true;
            }
            return false;
        }

        void DrawActionButtons(Config& a_cfg)
        {
            ImGuiMCP::Separator();

            if (a_cfg.HasUnsavedChanges()) {
                ImGuiMCP::Text("* Unsaved changes (click Save Settings to persist)");
            }

            if (ImGuiMCP::Button("Save Settings")) {
                if (a_cfg.SaveUser()) {
                    g_statusMessage = "Settings saved to MCM user INI.";
                } else {
                    g_statusMessage = "Failed to save settings.";
                }
            }

            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Reload from INI")) {
                a_cfg.LoadAll();
                g_statusMessage = "Reloaded from INI.";
            }

            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Reset Defaults")) {
                a_cfg.ResetDefaults();
                if (a_cfg.SaveUser()) {
                    g_statusMessage = "Defaults restored and saved.";
                } else {
                    g_statusMessage = "Defaults restored (save failed).";
                }
            }

            if (!g_statusMessage.empty()) {
                ImGuiMCP::Text("%s", g_statusMessage.c_str());
            }
        }

        void __stdcall RenderGeneral()
        {
            auto& cfg = Config::GetSingleton();
            ConfigData data = cfg.GetSnapshot();

            bool changed = false;
            changed |= ImGuiMCP::Checkbox("Enable Floating Prompts", &data.enabled);
            changed |= ImGuiMCP::Checkbox("Bypass in Power Armor", &data.powerArmorBypass);
            changed |= ImGuiMCP::Checkbox("Clamp to Screen Edges", &data.enableClamping);
            changed |= ImGuiMCP::SliderFloat("Smoothing Speed", &data.smoothingSpeed, 0.05f, 1.0f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Edge Margin (px)", &data.edgeMargin, 0.0f, 400.0f, "%.0f");

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Companion Prompts");
            changed |= ImGuiMCP::Checkbox("Limit Companion Prompt Distance", &data.limitCompanionPromptDistance);
            if (data.limitCompanionPromptDistance) {
                changed |= ImGuiMCP::SliderFloat("Max Distance (units)", &data.companionMaxDistance, 50.0f, 500.0f, "%.0f");
                changed |= ImGuiMCP::SliderFloat("Combat Max Distance (0 = Hide)", &data.companionCombatMaxDistance, 0.0f, 500.0f, "%.0f");
                changed |= ImGuiMCP::Checkbox("Prevent Companion Stealing Loot Focus", &data.companionDeprioritizeLoot);
            }

            if (changed) {
                cfg.ApplySnapshot(data, true);
            }

            DrawActionButtons(cfg);
        }

        void __stdcall RenderVisualStyle()
        {
            auto& cfg = Config::GetSingleton();
            ConfigData data = cfg.GetSnapshot();

            static constexpr const char* kStyles[] = { "Vanilla (Native HUD)", "Modern (GTA 6 Style)" };
            static constexpr const char* kButtonIcons[] = {
                "Keyboard (E / R)",
                "PlayStation (Colored)",
                "Xbox (Colored)",
                "PlayStation (Monochrome)",
                "Xbox (Monochrome)"
            };
            static constexpr const char* kTextModes[] = {
                "Action Only (GTA 6)",
                "Action + Target Name",
                "Name on World Objects Only"
            };
            static constexpr const char* kMultiActionLayouts[] = {
                "Stacked Vertical (Modern)",
                "Inline Horizontal"
            };

            bool changed = false;
            changed |= ImGuiMCP::Combo("Prompt Style", &data.promptStyle, kStyles, 2, -1);
            changed |= ImGuiMCP::Combo("Modern Text Mode", &data.modernTextMode, kTextModes, 3, -1);
            changed |= ImGuiMCP::Checkbox("Show Destination Map Above Load Doors", &data.showDoorDestination);
            changed |= ImGuiMCP::Combo("Multi-Action Layout", &data.multiActionLayout, kMultiActionLayouts, 2, -1);
            changed |= ImGuiMCP::SliderFloat("Multi-Action Row Spacing (px)", &data.modernActionGap, 0.0f, 24.0f, "%.0f");
            changed |= ImGuiMCP::Checkbox("Clean FIS / Sorter Tags ([Scrap], {{...}})", &data.cleanSorterTags);
            changed |= ImGuiMCP::Combo("Button Icon Style", &data.buttonIconStyle, kButtonIcons, 5, -1);
            changed |= ImGuiMCP::SliderFloat("Modern Prompt Opacity", &data.modernOpacity, 0.1f, 1.0f, "%.2f");
            changed |= ImGuiMCP::Checkbox("Compact Inline Info Line (Scrap / Val / Wt)", &data.inlineItemInfo);

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Prompt Sizes, Font & Screen Offset");
            changed |= DrawFontCombo("Prompt Font", data.promptFont);
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Refresh Fonts##Prompt")) {
                (void)DiscoverAvailableFonts(true);
            }
            changed |= ImGuiMCP::SliderFloat("Prompt Overall Scale", &data.modernScale, 0.5f, 2.5f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Button / Keycap Size", &data.modernButtonSize, 0.5f, 2.5f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Inner Key Text Size (e.g. 'E')", &data.modernKeyTextScale, 0.40f, 1.20f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Action & Name Text Size", &data.modernActionTextScale, 0.5f, 2.5f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Category Icon Size", &data.modernIconScale, 0.5f, 2.5f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Screen Offset X (px)", &data.modernScreenOffsetX, -200.0f, 200.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Screen Offset Y (px)", &data.modernScreenOffsetY, -200.0f, 200.0f, "%.0f");

            ImGuiMCP::Separator();
            static constexpr const char* kIconLibraries[] = {
                "FontAwesome (Built-in)",
                "FallUI Icon Library + Expansion"
            };
            changed |= ImGuiMCP::Combo("Icon Library", &data.iconLibrary, kIconLibraries, 2, -1);
            if (data.iconLibrary == 1) {
                const std::string statusTxt = GetFallUIRegistryStatusText();
                ImGuiMCP::Text("%s", statusTxt.c_str());
            }
            changed |= ImGuiMCP::Checkbox("Show Category Icons (Master Toggle)", &data.modernShowItemIcon);
            changed |= ImGuiMCP::Checkbox("Colored Category Icons (FallUI Style)", &data.coloredCategoryIcons);
            if (data.modernShowItemIcon) {
                changed |= ImGuiMCP::Checkbox("Weapon", &data.iconWeapon); ImGuiMCP::SameLine();
                changed |= ImGuiMCP::Checkbox("Armor", &data.iconArmor); ImGuiMCP::SameLine();
                changed |= ImGuiMCP::Checkbox("Aid / Chem", &data.iconAid); ImGuiMCP::SameLine();
                changed |= ImGuiMCP::Checkbox("Ammo", &data.iconAmmo);

                changed |= ImGuiMCP::Checkbox("Misc / Junk", &data.iconMiscJunk); ImGuiMCP::SameLine();
                changed |= ImGuiMCP::Checkbox("Key", &data.iconKey); ImGuiMCP::SameLine();
                changed |= ImGuiMCP::Checkbox("Book / Note", &data.iconBookNote); ImGuiMCP::SameLine();
                changed |= ImGuiMCP::Checkbox("Holotape", &data.iconHolotape);

                changed |= ImGuiMCP::Checkbox("Container", &data.iconContainer); ImGuiMCP::SameLine();
                changed |= ImGuiMCP::Checkbox("Door", &data.iconDoor); ImGuiMCP::SameLine();
                changed |= ImGuiMCP::Checkbox("Furniture / Bed", &data.iconFurniture); ImGuiMCP::SameLine();
                changed |= ImGuiMCP::Checkbox("Terminal", &data.iconTerminal);

                changed |= ImGuiMCP::Checkbox("NPC / Companion", &data.iconNpc);
            }

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Box, Padding & Shadows");
            changed |= ImGuiMCP::SliderFloat("Background Opacity (0 = Clean GTA 6)", &data.modernBgOpacity, 0.0f, 1.0f, "%.2f");
            changed |= ImGuiMCP::Checkbox("Show Prompt Border", &data.modernBorder);
            changed |= ImGuiMCP::Checkbox("Enable Text Drop Shadow", &data.modernTextShadow);
            changed |= ImGuiMCP::SliderFloat("Corner Rounding", &data.modernRounding, 0.0f, 24.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Horizontal Padding (px)", &data.modernPadX, 0.0f, 32.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Vertical Padding (px)", &data.modernPadY, 0.0f, 24.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Element Spacing (px)", &data.modernItemGap, 2.0f, 24.0f, "%.0f");

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Colors");
            changed |= DrawColorPickerRGB("Action Text Color", data.modernTextColor);
            changed |= DrawColorPickerRGB("Target Name Color", data.modernNameColor);
            changed |= DrawColorPickerRGB("Category Icon Color", data.modernIconColor);
            changed |= DrawColorPickerRGB("Accent / Selection Color", data.modernAccentColor);
            changed |= DrawColorPickerRGB("Background Color", data.modernBgColor);
            changed |= DrawColorPickerRGB("Border Color", data.modernBorderColor);

            if (changed) {
                cfg.ApplySnapshot(data, true);
            }

            DrawActionButtons(cfg);
        }

        void __stdcall RenderInfoCard()
        {
            auto& cfg = Config::GetSingleton();
            ConfigData data = cfg.GetSnapshot();

            static constexpr const char* kPositions[] = {
                "Off",
                "Below Prompt",
                "Above Prompt",
                "Right of Prompt",
                "Left of Prompt",
                "Fixed Top-Right",
                "Fixed Bottom-Right"
            };

            bool changed = false;
            changed |= ImGuiMCP::Combo("Mini Info Card Position", &data.infoCardPosition, kPositions, 7, -1);
            changed |= ImGuiMCP::SliderFloat("Hover Reveal Delay (sec)", &data.infoCardDelaySeconds, 0.0f, 5.0f, "%.1f");
            changed |= ImGuiMCP::SliderFloat("Card Offset X (px)", &data.infoCardOffsetX, -400.0f, 400.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Card Offset Y (px)", &data.infoCardOffsetY, -400.0f, 400.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Card Scale", &data.infoCardScale, 0.5f, 2.5f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Card Overall Opacity", &data.infoCardOpacity, 0.1f, 1.0f, "%.2f");

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Style, Geometry, Font & Colors");
            changed |= DrawFontCombo("Info Card Font", data.infoCardFont);
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Refresh Fonts##InfoCard")) {
                (void)DiscoverAvailableFonts(true);
            }
            changed |= ImGuiMCP::SliderFloat("Card Background Opacity", &data.infoCardBgOpacity, 0.0f, 1.0f, "%.2f");
            changed |= ImGuiMCP::Checkbox("Show Card Border", &data.infoCardBorder);
            changed |= ImGuiMCP::Checkbox("Show Top Accent Bar", &data.infoCardAccentBar);
            changed |= ImGuiMCP::Checkbox("Show Header Icon", &data.infoCardShowIcon);
            changed |= ImGuiMCP::SliderFloat("Card Rounding", &data.infoCardRounding, 0.0f, 24.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Card Padding (px)", &data.infoCardPad, 2.0f, 28.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Row Spacing (px)", &data.infoCardRowGap, 0.0f, 16.0f, "%.0f");
            changed |= DrawColorPickerRGB("Card Title Color", data.infoCardTitleColor);
            changed |= DrawColorPickerRGB("Card Label Color", data.infoCardLabelColor);
            changed |= DrawColorPickerRGB("Card Value Color", data.infoCardValueColor);
            changed |= DrawColorPickerRGB("Card Accent Color", data.infoCardAccentColor);
            changed |= DrawColorPickerRGB("Card Background Color", data.infoCardBgColor);
            changed |= DrawColorPickerRGB("Card Border Color", data.infoCardBorderColor);

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Card Contents");
            changed |= ImGuiMCP::Checkbox("Show Scrap Components", &data.showScrapComponents);
            changed |= ImGuiMCP::Checkbox("Show Value & Weight", &data.showItemValueWeight);
            changed |= ImGuiMCP::Checkbox("Show Weapon & Armor Stats", &data.showWeaponArmorStats);
            changed |= ImGuiMCP::Checkbox("Show NPC Stats (Weapon / HP / AP / DR / ER)", &data.showNpcStats);
            changed |= ImGuiMCP::Checkbox("Show Mini Info Card inside Modern QuickLoot", &data.infoCardInQuickContainer);

            if (changed) {
                cfg.ApplySnapshot(data, true);
            }

            DrawActionButtons(cfg);
        }

        void __stdcall RenderQuickContainer()
        {
            auto& cfg = Config::GetSingleton();
            ConfigData data = cfg.GetSnapshot();

            bool changed = false;
            changed |= ImGuiMCP::Checkbox("Float QuickContainer in 3D", &data.floatQuickContainer);
            changed |= ImGuiMCP::Checkbox("Use Modern Style for QuickContainer & Workbenches", &data.modernQuickContainer);
            changed |= ImGuiMCP::SliderFloat("QuickContainer Smoothing", &data.quickContainerSmoothing, 0.05f, 1.0f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Scroll Stability Deadzone (px)", &data.quickContainerDeadzone, 0.0f, 30.0f, "%.1f");
            changed |= DrawVec3Sliders("QuickContainer 3D Offset (X / Y / Z)", data.quickContainerOffset, -100.0f, 100.0f);

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Modern QuickLoot Window, Icons, Font & Sizes");
            static constexpr const char* kQcIconLibraries[] = {
                "FontAwesome (Built-in)",
                "FallUI Icon Library + Expansion"
            };
            changed |= ImGuiMCP::Combo("Icon Library##QuickLoot", &data.iconLibrary, kQcIconLibraries, 2, -1);
            if (data.iconLibrary == 1) {
                const std::string statusTxt = GetFallUIRegistryStatusText();
                ImGuiMCP::Text("%s", statusTxt.c_str());
            }
            changed |= ImGuiMCP::Checkbox("Show Item Category Icons", &data.qcShowItemIcons);
            ImGuiMCP::SameLine();
            changed |= ImGuiMCP::Checkbox("Colored Icons##QuickLoot", &data.coloredCategoryIcons);
            changed |= DrawFontCombo("QuickLoot Font", data.qcFont);
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Refresh Fonts##QuickLoot")) {
                (void)DiscoverAvailableFonts(true);
            }
            changed |= ImGuiMCP::SliderFloat("QuickLoot Overall Scale", &data.qcScale, 0.5f, 2.5f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Fixed Window Width (0 = Auto, px)", &data.qcWidth, 0.0f, 600.0f, "%.0f");
            changed |= ImGuiMCP::Checkbox("Expand List Beyond 5 Items (Off = Native 5-Slot)", &data.qcExpandInventoryList);
            changed |= ImGuiMCP::SliderInt("Max Visible Rows", &data.qcMaxVisibleRows, 3, 12);
            changed |= ImGuiMCP::SliderFloat("Row Height Padding (px)", &data.qcRowHeightPad, 0.0f, 20.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Header Title Size", &data.qcHeaderTextScale, 0.5f, 2.5f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("Item List Text Size", &data.qcItemTextScale, 0.5f, 2.5f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("QuickLoot Button Size", &data.qcButtonSize, 0.5f, 2.5f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("QuickLoot Inner Key Size", &data.qcKeyTextScale, 0.40f, 1.20f, "%.2f");
            changed |= ImGuiMCP::SliderFloat("QuickLoot Action Label Size", &data.qcButtonTextScale, 0.5f, 2.5f, "%.2f");

            if (changed) {
                cfg.ApplySnapshot(data, true);
            }

            DrawActionButtons(cfg);
        }

        void __stdcall RenderOffsets()
        {
            auto& cfg = Config::GetSingleton();
            ConfigData data = cfg.GetSnapshot();

            ImGuiMCP::Text("Live 3D world-unit anchor offsets. Negative Z lowers prompts closer to items.");
            ImGuiMCP::Separator();

            bool changed = false;
            ImGuiMCP::Text("Loose Items (Beaker, Oil Canister, etc.)");
            changed |= ImGuiMCP::SliderFloat("Items: Vertical Height (Z)", &data.itemOffset.z, -100.0f, 100.0f, "%.1f");
            changed |= ImGuiMCP::SliderFloat("Items: Horizontal (X)", &data.itemOffset.x, -100.0f, 100.0f, "%.1f");
            changed |= ImGuiMCP::SliderFloat("Items: Depth (Y)", &data.itemOffset.y, -100.0f, 100.0f, "%.1f");

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Doors");
            changed |= ImGuiMCP::SliderFloat("Doors: Vertical Height (Z)", &data.doorOffset.z, -100.0f, 100.0f, "%.1f");
            changed |= DrawVec3Sliders("Doors (X / Y / Z)", data.doorOffset, -100.0f, 100.0f);

            ImGuiMCP::Separator();
            ImGuiMCP::Text("NPCs & Companions");
            changed |= ImGuiMCP::SliderFloat("NPCs: Vertical Height (Z)", &data.npcOffset.z, -100.0f, 100.0f, "%.1f");
            changed |= DrawVec3Sliders("NPCs (X / Y / Z)", data.npcOffset, -100.0f, 100.0f);

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Containers & Workbenches");
            changed |= ImGuiMCP::SliderFloat("Containers: Vertical Height (Z)", &data.containerOffset.z, -100.0f, 100.0f, "%.1f");
            changed |= DrawVec3Sliders("Containers (X / Y / Z)", data.containerOffset, -100.0f, 100.0f);

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Terminals & Furniture (Workbenches, Chairs, Beds)");
            changed |= ImGuiMCP::SliderFloat("Terminals / Furniture: Vertical Height (Z)", &data.furnitureOffset.z, -100.0f, 100.0f, "%.1f");
            changed |= DrawVec3Sliders("Terminals / Furniture (X / Y / Z)", data.furnitureOffset, -100.0f, 100.0f);

            if (changed) {
                cfg.ApplySnapshot(data, true);
            }

            DrawActionButtons(cfg);
        }
    }

    bool IsHudOverlayActive() noexcept
    {
        return g_hudOverlayActive.load(std::memory_order_acquire);
    }

    void Register()
    {
        HMODULE fwMod = GetModuleHandleW(L"F4SEMenuFramework");
        if (!F4SEMenuFramework::IsInstalled() || fwMod == nullptr) {
            logger::info("F4SE Menu Framework not detected; skipping ImGui registration");
            return;
        }

        F4SEMenuFramework::SetSection("FAP - Floating Action Prompts");
        F4SEMenuFramework::AddSectionItem("General", RenderGeneral);
        F4SEMenuFramework::AddSectionItem("Visual Style", RenderVisualStyle);
        F4SEMenuFramework::AddSectionItem("Info Card", RenderInfoCard);
        F4SEMenuFramework::AddSectionItem("QuickContainer", RenderQuickContainer);
        F4SEMenuFramework::AddSectionItem("3D Offsets", RenderOffsets);

        const bool hasFontExports =
            GetProcAddress(fwMod, "ImFontAtlas_ImFontAtlas") != nullptr &&
            GetProcAddress(fwMod, "ImFontAtlas_destroy") != nullptr &&
            GetProcAddress(fwMod, "ImFontAtlas_AddFontFromFileTTF") != nullptr &&
            GetProcAddress(fwMod, "ImFontAtlas_Build") != nullptr &&
            GetProcAddress(fwMod, "ImFontAtlas_GetTexDataAsRGBA32") != nullptr &&
            GetProcAddress(fwMod, "ImFontAtlas_SetTexID") != nullptr &&
            GetProcAddress(fwMod, "ImFontAtlas_ClearTexData") != nullptr &&
            GetProcAddress(fwMod, "ImDrawList_PushTextureID") != nullptr &&
            GetProcAddress(fwMod, "ImDrawList_PopTextureID") != nullptr;
        g_customFontsSupported.store(hasFontExports, std::memory_order_release);
        if (!hasFontExports) {
            logger::warn("F4SE Menu Framework lacks ImFontAtlas or PushTextureID exports; custom fonts disabled");
        }

        const bool hasAddImage = GetProcAddress(fwMod, "ImDrawList_AddImage") != nullptr;
        g_drawImageSupported.store(hasAddImage, std::memory_order_release);
        if (!hasAddImage) {
            logger::warn("F4SE Menu Framework lacks ImDrawList_AddImage export; FallUI icon atlas and gamepad PNGs disabled");
        }

        const bool hasCirclePrimitives =
            GetProcAddress(fwMod, "ImDrawList_AddCircle") != nullptr &&
            GetProcAddress(fwMod, "ImDrawList_AddCircleFilled") != nullptr &&
            GetProcAddress(fwMod, "ImDrawList_AddTriangle") != nullptr &&
            GetProcAddress(fwMod, "ImDrawList_AddRect") != nullptr &&
            GetProcAddress(fwMod, "ImDrawList_AddLine") != nullptr;
        g_circlePrimitivesSupported.store(hasCirclePrimitives, std::memory_order_release);
        if (!hasCirclePrimitives) {
            logger::warn("F4SE Menu Framework lacks circle/triangle draw exports; gamepad badges will use keycap fallback");
        }

        const bool hasFindGlyph = GetProcAddress(fwMod, "ImFont_FindGlyph") != nullptr;
        g_findGlyphSupported.store(hasFindGlyph, std::memory_order_release);
        if (!hasFindGlyph) {
            logger::warn("F4SE Menu Framework lacks ImFont_FindGlyph export; gamepad glyph centering will use line bounds");
        }

        if (GetProcAddress(fwMod, "RegisterHudElement") != nullptr) {
            g_hudElement = F4SEMenuFramework::AddHudElement(RenderHudOverlay);
            g_hudOverlayActive.store(g_hudElement != nullptr, std::memory_order_release);
            logger::info("Registered FAP - Floating Action Prompts HUD overlay and 5 F1 settings tabs");
        } else {
            g_hudOverlayActive.store(false, std::memory_order_release);
            logger::warn("F4SE Menu Framework lacks RegisterHudElement; Modern HUD overlay inactive");
        }
    }
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "TargetInfo.h"

namespace FallUIIcons
{
    inline constexpr int kIconTileSize = 32;

    struct ResolvedFallUIIcon
    {
        std::string swfRelPath;
        std::string symbolName;
        std::uint32_t rgbColor{ 0xFFFFFFu };
        bool hasColor{ false };

        bool hasFallback{ false };
        std::string fallbackSwfRelPath;
        std::string fallbackSymbolName;
        std::uint32_t fallbackRgbColor{ 0xFFFFFFu };
        bool fallbackHasColor{ false };
    };

    struct SwfEdge
    {
        float x0{ 0.0f };
        float y0{ 0.0f };
        float cx{ 0.0f };
        float cy{ 0.0f };
        float x1{ 0.0f };
        float y1{ 0.0f };
        bool curved{ false };
    };

    struct SwfShape
    {
        std::vector<SwfEdge> edges;
    };

    struct SwfMatrix
    {
        float sx{ 1.0f };
        float r0{ 0.0f };
        float r1{ 0.0f };
        float sy{ 1.0f };
        float tx{ 0.0f };
        float ty{ 0.0f };
    };

    struct SwfPlacement
    {
        std::uint16_t characterId{ 0 };
        SwfMatrix matrix{};
    };

    struct SwfSprite
    {
        std::vector<SwfPlacement> placements;
    };

    [[nodiscard]] bool DecompressZlib(
        const std::uint8_t* a_src,
        std::size_t a_srcSize,
        std::vector<std::uint8_t>& a_out,
        std::size_t a_maxOutBytes = 16u * 1024u * 1024u);

    [[nodiscard]] std::string ExtractLeadingSorterTag(std::string_view a_rawName);

    class SwfIconFile
    {
    public:
        void Clear() noexcept;
        bool LoadFromMemory(const std::uint8_t* a_data, std::size_t a_size);
        bool LoadFromFile(const std::filesystem::path& a_path);

        [[nodiscard]] bool IsLoaded() const noexcept { return _loaded; }
        [[nodiscard]] std::size_t SymbolCount() const noexcept { return _symbols.size(); }
        [[nodiscard]] std::size_t ShapeCount() const noexcept { return _shapes.size(); }
        [[nodiscard]] bool HasSymbol(std::string_view a_symbolName) const;

        bool RasterizeSymbol(
            std::string_view a_symbolName,
            int a_tileSize,
            std::vector<std::uint8_t>& a_outRgba) const;

    private:
        struct Segment
        {
            float x0{ 0.0f };
            float y0{ 0.0f };
            float x1{ 0.0f };
            float y1{ 0.0f };
        };

        void CollectCharacterSegments(
            std::uint16_t a_characterId,
            const SwfMatrix& a_parentMatrix,
            int a_depth,
            std::vector<Segment>& a_outSegs) const;

        bool _loaded{ false };
        std::unordered_map<std::uint16_t, SwfShape> _shapes;
        std::unordered_map<std::uint16_t, SwfSprite> _sprites;
        std::unordered_map<std::string, std::uint16_t> _symbols;
    };

    class FallUIIconRegistry
    {
    public:
        void Clear();
        bool InitializeFromInterfaceDir(const std::filesystem::path& a_interfaceDir);

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] std::size_t TagCount() const noexcept;
        [[nodiscard]] std::size_t ColorCount() const noexcept;
        [[nodiscard]] std::size_t LoadedSwfCount() const noexcept;

        [[nodiscard]] bool ResolveIcon(
            std::string_view a_rawName,
            TargetInfo::ItemKind a_kind,
            ResolvedFallUIIcon& a_out) const;

        bool RasterizeResolvedIcon(
            const ResolvedFallUIIcon& a_resolved,
            int a_tileSize,
            std::vector<std::uint8_t>& a_outRgba,
            bool* a_outUsedFallback = nullptr);

    private:
        struct SwfCandidate
        {
            std::string swfRelPath;
            std::string iconSymbol;
            std::uint32_t rgbColor{ 0xFFFFFFu };
            bool hasColor{ false };
        };

        struct TagDefinition
        {
            SwfCandidate primary;
            SwfCandidate fallback;
            bool hasFallback{ false };
        };

        struct ColorDefinition
        {
            std::uint32_t rgb{ 0xFFFFFFu };
            bool isAlias{ false };
            std::string aliasTarget;
        };

        struct AutoTagRule
        {
            std::string needleLower;
            std::string tagLower;
        };

        struct SwfCacheEntry
        {
            bool attempted{ false };
            bool valid{ false };
            SwfIconFile swf{};
        };

        [[nodiscard]] bool ResolveColorName(
            const std::unordered_map<std::string, ColorDefinition>& a_colors,
            std::string_view a_colorName,
            std::uint32_t& a_outRgb,
            int a_depth = 0) const;

        void ParseColorXml(
            const std::string& a_cleanedXml,
            std::unordered_map<std::string, ColorDefinition>& a_ioColors) const;

        void ParseTagXml(
            const std::string& a_rawXml,
            const std::string& a_defaultSwfRelPath,
            bool a_verifySwfOnDisk,
            std::unordered_map<std::string, ColorDefinition>& a_ioColors);

        void ParseIndexedTagsFile(const std::filesystem::path& a_path);
        void ParseAutoTagsIni(const std::filesystem::path& a_path);

        mutable std::mutex _mutex;
        bool _initialized{ false };
        std::filesystem::path _interfaceDir;
        std::unordered_map<std::string, TagDefinition> _tags;
        std::unordered_map<std::string, std::string> _indexedBySection;
        std::unordered_map<std::string, std::string> _indexedGlobal;
        std::unordered_map<std::string, std::vector<AutoTagRule>> _autoRules;
        std::unordered_map<std::string, std::string> _autoWildcards;
        std::unordered_map<std::string, SwfCacheEntry> _swfCache;
        std::size_t _colorCount{ 0 };
    };
}

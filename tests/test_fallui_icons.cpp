#include "check.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "../src/Config.h"
#include "../src/FallUIIcons.h"
#include "../src/TargetInfo.h"

namespace
{
    void PushU16(std::vector<std::uint8_t>& a_buf, std::uint16_t a_v)
    {
        a_buf.push_back(static_cast<std::uint8_t>(a_v & 0xFFu));
        a_buf.push_back(static_cast<std::uint8_t>((a_v >> 8) & 0xFFu));
    }

    void PushU32(std::vector<std::uint8_t>& a_buf, std::uint32_t a_v)
    {
        a_buf.push_back(static_cast<std::uint8_t>(a_v & 0xFFu));
        a_buf.push_back(static_cast<std::uint8_t>((a_v >> 8) & 0xFFu));
        a_buf.push_back(static_cast<std::uint8_t>((a_v >> 16) & 0xFFu));
        a_buf.push_back(static_cast<std::uint8_t>((a_v >> 24) & 0xFFu));
    }

    struct BitWriter
    {
        std::vector<std::uint8_t> bytes;
        std::uint8_t cur{ 0 };
        int bits{ 0 };

        void WriteUBits(std::uint32_t a_val, int a_count)
        {
            for (int i = a_count - 1; i >= 0; --i) {
                cur = static_cast<std::uint8_t>((cur << 1) | ((a_val >> i) & 1u));
                if (++bits == 8) {
                    bytes.push_back(cur);
                    cur = 0;
                    bits = 0;
                }
            }
        }

        void WriteSBits(std::int32_t a_val, int a_count)
        {
            const std::uint32_t mask = (a_count >= 32) ? 0xFFFFFFFFu : ((1u << a_count) - 1u);
            WriteUBits(static_cast<std::uint32_t>(a_val) & mask, a_count);
        }

        void Align()
        {
            if (bits != 0) {
                cur = static_cast<std::uint8_t>(cur << (8 - bits));
                bytes.push_back(cur);
                cur = 0;
                bits = 0;
            }
        }
    };

    void PushSwfTag(std::vector<std::uint8_t>& a_out, std::uint16_t a_tagId, const std::vector<std::uint8_t>& a_payload)
    {
        if (a_payload.size() < 0x3Fu) {
            PushU16(a_out, static_cast<std::uint16_t>((a_tagId << 6) | a_payload.size()));
        } else {
            PushU16(a_out, static_cast<std::uint16_t>((a_tagId << 6) | 0x3Fu));
            PushU32(a_out, static_cast<std::uint32_t>(a_payload.size()));
        }
        a_out.insert(a_out.end(), a_payload.begin(), a_payload.end());
    }

    // Minimal valid uncompressed FWS with a 100x100 filled square symbol "Test.Square"
    std::vector<std::uint8_t> BuildSyntheticSquareSwf()
    {
        std::vector<std::uint8_t> body;
        // RECT (nbits=1: 0,0,0,0 -> 5 + 4*1 = 9 bits = 2 bytes) + Rate(2) + Count(2)
        BitWriter stageRect;
        stageRect.WriteUBits(1, 5);
        stageRect.WriteSBits(0, 1);
        stageRect.WriteSBits(0, 1);
        stageRect.WriteSBits(0, 1);
        stageRect.WriteSBits(0, 1);
        stageRect.Align();
        body.insert(body.end(), stageRect.bytes.begin(), stageRect.bytes.end());
        PushU16(body, 0x0100);
        PushU16(body, 1);

        // DefineShape3 (tag 32): shapeId=1
        std::vector<std::uint8_t> shape;
        PushU16(shape, 1);
        BitWriter b;
        b.WriteUBits(9, 5); // RECT 0..100
        b.WriteSBits(0, 9);
        b.WriteSBits(100, 9);
        b.WriteSBits(0, 9);
        b.WriteSBits(100, 9);
        b.Align();
        shape.insert(shape.end(), b.bytes.begin(), b.bytes.end());
        // 1 solid fill style
        shape.push_back(1);
        shape.push_back(0x00);
        shape.push_back(255); shape.push_back(255); shape.push_back(255); shape.push_back(255);
        // 0 line styles
        shape.push_back(0);
        // NumFillBits=1, NumLineBits=1
        BitWriter edges;
        edges.WriteUBits(1, 4);
        edges.WriteUBits(1, 4);
        // StyleChange: move(0,0), fill1=1 -> flags = 0x05 (StateMoveTo | StateFillStyle1)
        edges.WriteUBits(0, 1);
        edges.WriteUBits(0x05, 5);
        edges.WriteUBits(1, 5);
        edges.WriteSBits(0, 1);
        edges.WriteSBits(0, 1);
        edges.WriteUBits(1, 1); // fill1 = 1
        // 4 straight edges (nbits = 9 -> encoded as 7 = 9 - 2)
        auto writeH = [&](std::int32_t dx) {
            edges.WriteUBits(1, 1); edges.WriteUBits(1, 1); edges.WriteUBits(7, 4);
            edges.WriteUBits(0, 1); edges.WriteUBits(0, 1); edges.WriteSBits(dx, 9);
        };
        auto writeV = [&](std::int32_t dy) {
            edges.WriteUBits(1, 1); edges.WriteUBits(1, 1); edges.WriteUBits(7, 4);
            edges.WriteUBits(0, 1); edges.WriteUBits(1, 1); edges.WriteSBits(dy, 9);
        };
        writeH(100);
        writeV(100);
        writeH(-100);
        writeV(-100);
        // EndShape
        edges.WriteUBits(0, 1);
        edges.WriteUBits(0, 5);
        edges.Align();
        shape.insert(shape.end(), edges.bytes.begin(), edges.bytes.end());
        PushSwfTag(body, 32, shape);

        // DefineSprite (tag 39): spriteId=2, frameCount=1, PlaceObject2 (tag 26) -> shape 1 with Matrix translate(20, 10)
        std::vector<std::uint8_t> sprite;
        PushU16(sprite, 2);
        PushU16(sprite, 1);
        std::vector<std::uint8_t> place;
        place.push_back(0x06); // HasCharacter | HasMatrix
        PushU16(place, 1);     // Depth
        PushU16(place, 1);     // CharacterId
        BitWriter mat;
        mat.WriteUBits(0, 1); // HasScale = 0
        mat.WriteUBits(0, 1); // HasRotate = 0
        mat.WriteUBits(6, 5); // NTranslateBits = 6
        mat.WriteSBits(20, 6);
        mat.WriteSBits(10, 6);
        mat.Align();
        place.insert(place.end(), mat.bytes.begin(), mat.bytes.end());
        PushSwfTag(sprite, 26, place);
        PushU16(sprite, 0); // End inside sprite
        PushSwfTag(body, 39, sprite);

        // SymbolClass (tag 76): "m_Test.Square" -> 1, "m_Test.SpriteSquare" -> 2
        std::vector<std::uint8_t> sym;
        PushU16(sym, 2);
        PushU16(sym, 1);
        const std::string name1 = "m_Test.Square";
        sym.insert(sym.end(), name1.begin(), name1.end());
        sym.push_back(0);
        PushU16(sym, 2);
        const std::string name2 = "m_Test.SpriteSquare";
        sym.insert(sym.end(), name2.begin(), name2.end());
        sym.push_back(0);
        PushSwfTag(body, 76, sym);

        // End tag (0)
        PushU16(body, 0);

        std::vector<std::uint8_t> swf;
        swf.push_back('F'); swf.push_back('W'); swf.push_back('S'); swf.push_back(10);
        PushU32(swf, static_cast<std::uint32_t>(8 + body.size()));
        swf.insert(swf.end(), body.begin(), body.end());
        return swf;
    }
}

int main()
{
    std::cout << "[TEST] Running test_fallui_icons..." << std::endl;

    // 1. Zlib validation, Adler-32 check, stored / fixed / dynamic block inflate + fuzz guards
    {
        // Valid zlib header + uncompressed block "HELLO" + Adler-32 (0x044C0175)
        const std::vector<std::uint8_t> stored{
            0x78, 0x9C,
            0x01, 0x05, 0x00, 0xFA, 0xFF,
            'H', 'E', 'L', 'L', 'O',
            0x04, 0x4C, 0x01, 0x75
        };
        std::vector<std::uint8_t> out;
        CHECK(FallUIIcons::DecompressZlib(stored.data(), stored.size(), out));
        CHECK(std::string(out.begin(), out.end()) == "HELLO");

        // Fixed Huffman block (btype=1)
        const std::vector<std::uint8_t> fixedZlib{
            0x78, 0x01, 0x73, 0x74, 0x72, 0x44, 0x81, 0x00, 0x22, 0xD8, 0x04, 0x19
        };
        CHECK(FallUIIcons::DecompressZlib(fixedZlib.data(), fixedZlib.size(), out));
        CHECK(std::string(out.begin(), out.end()) == "ABABABABABABABAB");

        // Dynamic Huffman block (btype=2)
        const std::vector<std::uint8_t> dynZlib{
            0x78, 0xDA, 0x35, 0xCC, 0x47, 0x01, 0x00, 0x20, 0x00, 0xC4, 0x30, 0x6D, 0xEC, 0x3D, 0xFD, 0x8B,
            0xA1, 0xF7, 0x20, 0x02, 0x62, 0x0C, 0xAC, 0x38, 0xF1, 0x12, 0x24, 0x4A, 0x42, 0x46, 0x41, 0x45,
            0x43, 0xC7, 0xC0, 0xC4, 0xC2, 0xC6, 0xC1, 0xC5, 0xCF, 0x7E, 0xF5, 0x00, 0xD2, 0xC0, 0x1A, 0x6A
        };
        CHECK(FallUIIcons::DecompressZlib(dynZlib.data(), dynZlib.size(), out));
        CHECK(out.size() == 94u);
        CHECK(std::string(out.begin(), out.begin() + 16) == "AAAAABBBBBCCCCCD");

        // Bad Adler-32 / bad header / truncated / preset dict / max cap
        std::vector<std::uint8_t> badAdler = stored;
        badAdler.back() ^= 0xFFu;
        CHECK(!FallUIIcons::DecompressZlib(badAdler.data(), badAdler.size(), out));
        const std::vector<std::uint8_t> badHdr{ 0x78, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01 };
        CHECK(!FallUIIcons::DecompressZlib(badHdr.data(), badHdr.size(), out));
        CHECK(!FallUIIcons::DecompressZlib(stored.data(), 4, out));
        CHECK(!FallUIIcons::DecompressZlib(stored.data(), stored.size(), out, 3));
        CHECK(out.empty());
    }

    // 2. Leading FIS/sorter tag extraction
    {
        CHECK(FallUIIcons::ExtractLeadingSorterTag("[Pistol] .44 Pistol") == "pistol");
        CHECK(FallUIIcons::ExtractLeadingSorterTag("[[Scrap]] Industrial Oil Canister {{Aluminum}}") == "scrap");
        CHECK(FallUIIcons::ExtractLeadingSorterTag("<font color='#FFFFFF'>[Aid] Stimpak</font>") == "aid");
        CHECK(FallUIIcons::ExtractLeadingSorterTag("|Cigarette| Pack") == "cigarette");
        CHECK(FallUIIcons::ExtractLeadingSorterTag("(Mk II) Combat Armor").empty());
        CHECK(FallUIIcons::ExtractLeadingSorterTag("Plain Item").empty());
    }

    // 3. Synthetic FWS SWF parsing + vector silhouette rasterization
    {
        const auto swfBytes = BuildSyntheticSquareSwf();
        FallUIIcons::SwfIconFile swf;
        CHECK(swf.LoadFromMemory(swfBytes.data(), swfBytes.size()));
        CHECK(swf.IsLoaded());
        CHECK(swf.ShapeCount() == 1);
        CHECK(swf.SymbolCount() >= 1);
        CHECK(swf.HasSymbol("Test.Square"));
        CHECK(swf.HasSymbol("m_Test.Square"));
        CHECK(!swf.HasSymbol("Missing.Symbol"));

        std::vector<std::uint8_t> rgba;
        CHECK(swf.RasterizeSymbol("Test.Square", FallUIIcons::kIconTileSize, rgba));
        CHECK(rgba.size() == 32u * 32u * 4u);
        // Center pixel should be opaque white, corner pixel should be transparent padding
        const std::size_t centerIdx = (16u * 32u + 16u) * 4u;
        const std::size_t cornerIdx = 0u;
        CHECK(rgba[centerIdx + 3] == 255u);
        CHECK(rgba[cornerIdx + 3] == 0u);

        // Sprite placement + matrix rasterization
        std::vector<std::uint8_t> spriteRgba;
        CHECK(swf.RasterizeSymbol("Test.SpriteSquare", FallUIIcons::kIconTileSize, spriteRgba));
        CHECK(spriteRgba[centerIdx + 3] == 255u);
    }

    // 4. Temp Interface directory XML + IndexedTags + AutoTags integration
    {
        const auto tmp = std::filesystem::temp_directory_path() / "fap_test_fallui_icons";
        std::filesystem::remove_all(tmp);
        std::filesystem::create_directories(tmp / "ItemSorter" / "ColorSets");
        std::filesystem::create_directories(tmp / "ItemSorter" / "FIS" / "TagConfiguration.Addons");
        std::filesystem::create_directories(tmp / "ItemSorter" / "FIS" / "AutoTags");

        // Write synthetic SWF as FallUI_IconLib.swf
        {
            const auto swfBytes = BuildSyntheticSquareSwf();
            std::ofstream ofs(tmp / "FallUI_IconLib.swf", std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(swfBytes.data()), static_cast<std::streamsize>(swfBytes.size()));
        }

        // Write Default.xml with hex, rgb, and alias colors + cycle guard
        {
            std::ofstream ofs(tmp / "ItemSorter" / "ColorSets" / "Default.xml");
            ofs << "<TagConfiguration><colors>\n";
            ofs << "  <color name='gold' rgb='233,184,63' />\n";
            ofs << "  <color name='WeaponRanged' hex='81b5cb' />\n";
            ofs << "  <color name='Ammo' alias='gold' />\n";
            ofs << "  <color name='CycleA' alias='CycleB' />\n";
            ofs << "  <color name='CycleB' alias='CycleA' />\n";
            ofs << "</colors></TagConfiguration>\n";
        }

        // Write main FIS XML (includes '>' inside quoted attribute + comma color fallback)
        {
            std::ofstream ofs(tmp / "ItemSorter" / "FIS (FallUI Item Sorter).xml");
            ofs << "<TagConfiguration><tags iconLibraryFile=\"FallUI_IconLib.swf\">\n";
            ofs << "  <tag keyword=\"Pistol\" icon=\"Test.Square\" colorname=\"WeaponRanged\" />\n";
            ofs << "  <tag keyword=\"Scrap\" icon=\"Test.Square\" colorname=\"Ammo,gold\" />\n";
            ofs << "  <tag keyword=\"Bottle\" icon=\"Test.Square\" colorname=\"gold\" />\n";
            ofs << "  <tag keyword=\"CustomWild\" icon=\"Test.Square\" colorname=\"pink,gold\" />\n";
            ofs << "  <tag keyword=\"CycleTest\" icon=\"Test.Square\" colorname=\"CycleA\" />\n";
            ofs << "  <tag keyword='AttrQuote' note='A > B' icon='Test.SpriteSquare' colorname='WeaponRanged' />\n";
            ofs << "  <tag keyword='SubTest' icon='Test.Square' subicon='Test.SpriteSquare' colorname='WeaponRanged,white' />\n";
            ofs << "</tags></TagConfiguration>\n";
        }

        // Write valid addon XML override + missing-SWF addon XML that must NOT clobber base Bottle tag
        {
            std::ofstream ofs(tmp / "ItemSorter" / "FIS" / "TagConfiguration.Addons" / "CustomAddon.xml");
            ofs << "<TagConfiguration><colors><color name='CigCol' rgb='74,65,42' /></colors>\n";
            ofs << "<tags iconLibraryFile=\"FallUI_IconLib.swf\">\n";
            ofs << "  <tag keyword='Cigarette' icon='Test.Square' colorname='CigCol,gold' />\n";
            ofs << "</tags></TagConfiguration>\n";
        }
        {
            std::ofstream ofs(tmp / "ItemSorter" / "FIS" / "TagConfiguration.Addons" / "z_MissingAddon.xml");
            ofs << "<TagConfiguration><tags iconLibraryFile=\"DoesNotExist.swf\">\n";
            ofs << "  <tag keyword='Bottle' icon='Broken.Icon' colorname='WeaponRanged' />\n";
            ofs << "</tags></TagConfiguration>\n";
        }

        // Write IndexedTags_en.txt with 0x1F delimiter + JUNK {{{...}}} suffix format
        {
            std::ofstream ofs(tmp / "ItemSorter" / "FIS" / "AutoTags" / "IndexedTags_en.txt", std::ios::binary);
            ofs << "MISC\n[Cigarette]\x1f" "Cigarette Pack\n[Scrap]\x1f" "Desk Fan\n";
            ofs << "JUNK\n[Bottle]{{{Glass}}}\x1f" "Milk Bottle\n";
        }

        // Write AutoTags_en.ini + extra locale AutoTags_ptbr.ini with specific rule + wildcard '*' catch-all
        {
            std::ofstream ofs(tmp / "ItemSorter" / "FIS" / "AutoTags" / "AutoTags_en.ini");
            ofs << "[WEAP]\nRevolver=[Pistol]\n";
            ofs << "[MISC]\n*=[CustomWild]\n";
        }
        {
            std::ofstream ofs(tmp / "ItemSorter" / "FIS" / "AutoTags" / "AutoTags_ptbr.ini");
            ofs << "[ALCH]\nMedicina={SubTest}\n";
        }

        FallUIIcons::FallUIIconRegistry reg;
        CHECK(reg.InitializeFromInterfaceDir(tmp));
        CHECK(reg.TagCount() == 8);
        CHECK(reg.ColorCount() >= 6);
        {
            const auto addons = reg.LoadedAddonNames();
            CHECK(addons.size() == 1);
            CHECK(addons[0] == "CustomAddon");
        }

        FallUIIcons::ResolvedFallUIIcon res;
        // 4a. World-only category returns false
        CHECK(!reg.ResolveIcon("[Pistol] Door", TargetInfo::ItemKind::Door, res));
        CHECK(!reg.ResolveIcon("Terminal", TargetInfo::ItemKind::Terminal, res));

        // 4b. Explicit [Tag] match & quote-aware XML tag parsing
        CHECK(reg.ResolveIcon("[Pistol] Custom Gun", TargetInfo::ItemKind::Weapon, res));
        CHECK(res.symbolName == "test.square");
        CHECK(res.hasColor && res.rgbColor == 0x81B5CBu);
        CHECK(reg.ResolveIcon("[AttrQuote] Test", TargetInfo::ItemKind::MiscJunk, res));
        CHECK(res.symbolName == "test.spritesquare");

        // 4c. IndexedTags exact lookup on untagged name + JUNK {{{Glass}}} + missing-SWF addon guard
        CHECK(reg.ResolveIcon("Cigarette Pack", TargetInfo::ItemKind::MiscJunk, res));
        CHECK(res.hasColor && res.rgbColor == ((74u << 16) | (65u << 8) | 42u));
        CHECK(reg.ResolveIcon("Milk Bottle", TargetInfo::ItemKind::MiscJunk, res));
        CHECK(res.symbolName == "test.square");
        CHECK(res.hasColor && res.rgbColor == ((233u << 16) | (184u << 8) | 63u));

        // 4d. AutoTags substring lookup on untagged modified weapon
        CHECK(reg.ResolveIcon("Tuned snubnosed Revolver", TargetInfo::ItemKind::Weapon, res));
        CHECK(res.hasColor && res.rgbColor == 0x81B5CBu);

        // 4e. AutoTags '*' catch-all + comma color fallback + cycle guard
        CHECK(reg.ResolveIcon("Mystery Widget", TargetInfo::ItemKind::MiscJunk, res));
        CHECK(res.symbolName == "test.square");
        CHECK(res.hasColor && res.rgbColor == ((233u << 16) | (184u << 8) | 63u));
        CHECK(reg.ResolveIcon("[CycleTest] Item", TargetInfo::ItemKind::MiscJunk, res));
        CHECK(!res.hasColor);

        // 4f. Rasterize via registry lazy SWF load
        std::vector<std::uint8_t> rgba;
        CHECK(reg.RasterizeResolvedIcon(res, 32, rgba));
        CHECK(reg.LoadedSwfCount() == 1);
        CHECK(rgba.size() == 32u * 32u * 4u);

        // 4g. Offline Modern Age checks: |+{Tag} wrapper, built-in white colorname, dual-color subicon baking, and non-English AutoTags
        FallUIIcons::ResolvedFallUIIcon resSub;
        CHECK(reg.ResolveIcon("|+{SubTest} Prototype", TargetInfo::ItemKind::MiscJunk, resSub));
        CHECK(resSub.subSymbolName == "test.spritesquare");
        CHECK(resSub.hasColor && resSub.rgbColor == 0x81B5CBu);
        CHECK(resSub.hasSubColor && resSub.subRgbColor == 0xFFFFFFu);
        std::vector<std::uint8_t> subRgba;
        bool usedFb = false;
        bool baked = false;
        CHECK(reg.RasterizeResolvedIcon(resSub, 32, subRgba, &usedFb, true, &baked));
        CHECK(baked);
        CHECK(subRgba.size() == 32u * 32u * 4u);

        std::vector<std::uint8_t> monoSubRgba;
        bool bakedMono = true;
        CHECK(reg.RasterizeResolvedIcon(resSub, 32, monoSubRgba, &usedFb, false, &bakedMono));
        CHECK(!bakedMono);

        FallUIIcons::ResolvedFallUIIcon resPt;
        CHECK(reg.ResolveIcon("Medicina Especial", TargetInfo::ItemKind::Aid, resPt));
        CHECK(resPt.subSymbolName == "test.spritesquare");
        CHECK(TargetInfo::CleanDisplayName("|+{SubTest} Prototype", true) == "Prototype");

        std::filesystem::remove_all(tmp);
    }

    // 5. Real installed Fallout 4 FallUI_IconLib.swf + XeniuIconEditLib.swf verification (when present locally)
    {
        const std::filesystem::path gameInterface = "E:/SteamLibrary/steamapps/common/Fallout 4/Data/Interface";
        const std::filesystem::path falluiSwf = gameInterface / "FallUI_IconLib.swf";
        if (std::filesystem::exists(falluiSwf)) {
            FallUIIcons::SwfIconFile swf;
            CHECK(swf.LoadFromFile(falluiSwf));
            CHECK(swf.SymbolCount() >= 200);
            std::vector<std::uint8_t> rgba;
            CHECK(swf.RasterizeSymbol("M8r.Fo4Wpn.Pistol", 32, rgba));
            int opaquePixels = 0;
            for (std::size_t i = 3; i < rgba.size(); i += 4) {
                if (rgba[i] > 128u) {
                    ++opaquePixels;
                }
            }
            CHECK(opaquePixels > 20);
        }

        const std::filesystem::path expSwf =
            "E:/Mods/Vortex Mods/fallout4/Icon Expansion Pack-88022-2-1-2b-1740492100/Interface/ItemSorter/FIS/TagConfiguration.Addons/XeniuIconEditLib.swf";
        if (std::filesystem::exists(expSwf)) {
            FallUIIcons::SwfIconFile swf;
            CHECK(swf.LoadFromFile(expSwf));
            CHECK(swf.SymbolCount() >= 200);
            std::vector<std::uint8_t> rgba;
            CHECK(swf.RasterizeSymbol("XeniuIconEditLib.Icon01", 32, rgba));
            int opaquePixels = 0;
            for (std::size_t i = 3; i < rgba.size(); i += 4) {
                if (rgba[i] > 128u) {
                    ++opaquePixels;
                }
            }
            CHECK(opaquePixels > 20);
        }

        if (std::filesystem::exists(gameInterface / "ItemSorter" / "FIS (FallUI Item Sorter).xml")) {
            FallUIIcons::FallUIIconRegistry reg;
            CHECK(reg.InitializeFromInterfaceDir(gameInterface));
            CHECK(reg.TagCount() >= 150);
            FallUIIcons::ResolvedFallUIIcon res;
            CHECK(reg.ResolveIcon("[Pistol] 10mm", TargetInfo::ItemKind::Weapon, res));
            CHECK(reg.ResolveIcon("Milk Bottle", TargetInfo::ItemKind::MiscJunk, res));
            std::vector<std::uint8_t> rgba;
            CHECK(reg.RasterizeResolvedIcon(res, 32, rgba));
            CHECK(rgba.size() == 32u * 32u * 4u);
        }

        const std::filesystem::path modernAgeDir =
            "E:/Mods/Vortex Mods/fallout4/FIS - Modern Age Icons Addon-66093-1-2-1HF-1690222011/Interface";
        const std::filesystem::path modernAgeXml =
            modernAgeDir / "ItemSorter" / "FIS" / "TagConfiguration.Addons" / "ModernAgeAddonTags.xml";
        const std::filesystem::path modernAgeSwf =
            modernAgeDir / "ItemSorter" / "FIS" / "TagConfiguration.Addons" / "ModernAgeAddonIconLib.swf";
        if (std::filesystem::exists(modernAgeXml) && std::filesystem::exists(modernAgeSwf)) {
            FallUIIcons::SwfIconFile swf;
            CHECK(swf.LoadFromFile(modernAgeSwf));
            CHECK(swf.SymbolCount() >= 150);

            if (std::filesystem::exists(gameInterface / "ItemSorter" / "ColorSets" / "Default.xml")) {
                const auto tmpMod = std::filesystem::temp_directory_path() / "fap_modern_age_test";
                std::filesystem::remove_all(tmpMod);
                std::filesystem::create_directories(tmpMod / "ItemSorter" / "ColorSets");
                std::filesystem::create_directories(tmpMod / "ItemSorter" / "FIS" / "TagConfiguration.Addons");
                std::filesystem::create_directories(tmpMod / "ItemSorter" / "FIS" / "AutoTags");
                std::filesystem::copy_file(
                    gameInterface / "ItemSorter" / "ColorSets" / "Default.xml",
                    tmpMod / "ItemSorter" / "ColorSets" / "Default.xml");
                std::filesystem::copy_file(
                    modernAgeXml,
                    tmpMod / "ItemSorter" / "FIS" / "TagConfiguration.Addons" / "ModernAgeAddonTags.xml");
                std::filesystem::copy_file(
                    modernAgeSwf,
                    tmpMod / "ItemSorter" / "FIS" / "TagConfiguration.Addons" / "ModernAgeAddonIconLib.swf");
                {
                    std::ofstream ptbr(tmpMod / "ItemSorter" / "FIS" / "AutoTags" / "AutoTags_ptbr.ini");
                    ptbr << "[alch]\nNuka=RGD\n";
                }

                FallUIIcons::FallUIIconRegistry modReg;
                CHECK(modReg.InitializeFromInterfaceDir(tmpMod));
                FallUIIcons::ResolvedFallUIIcon rSub;
                // ModernAgeAddonTags maps ARS to icon + subicon with dual colorname="red,white"
                CHECK(modReg.ResolveIcon("|+{ARS} M4A1 Carbine", TargetInfo::ItemKind::Weapon, rSub));
                CHECK(rSub.symbolName == "ha_ar15");
                CHECK(rSub.subSymbolName == "ha_muzzle_2");
                CHECK(rSub.hasColor);
                CHECK(rSub.hasSubColor);
                CHECK(rSub.rgbColor != rSub.subRgbColor);
                std::vector<std::uint8_t> compRgba;
                bool usedFb = false;
                bool baked = false;
                CHECK(modReg.RasterizeResolvedIcon(rSub, 32, compRgba, &usedFb, true, &baked));
                CHECK(baked);
                CHECK(compRgba.size() == 32u * 32u * 4u);

                std::vector<std::uint8_t> monoRgba;
                bool bakedMono = true;
                CHECK(modReg.RasterizeResolvedIcon(rSub, 32, monoRgba, &usedFb, false, &bakedMono));
                CHECK(!bakedMono);
                CHECK(monoRgba.size() == 32u * 32u * 4u);

                // Verify CleanDisplayName strips {Tag} and |+ prefix
                CHECK(TargetInfo::CleanDisplayName("|+{ARS} M4A1 Carbine", true) == "M4A1 Carbine");

                // Verify non-English AutoTags_*.ini rule works
                FallUIIcons::ResolvedFallUIIcon rPt;
                CHECK(modReg.ResolveIcon("Nuka-Cola Quantum", TargetInfo::ItemKind::Aid, rPt));
                CHECK(rPt.subSymbolName == "bb_blast");

                std::filesystem::remove_all(tmpMod);
            }
        }
    }

    // 6. Unconditional FallUIIconLibrary.ini & FallUI.ini parity tests (ColorSet, bIconsColored, bIconsEffect, Brightness/Saturation, Shading)
    {
        const auto tmpRoot = std::filesystem::temp_directory_path() / "fap_fallui_colorset_test";
        std::filesystem::remove_all(tmpRoot);
        const auto tmpIface = tmpRoot / "Interface";
        const auto tmpMcm = tmpRoot / "MCM" / "Settings";
        std::filesystem::create_directories(tmpIface / "ItemSorter" / "ColorSets");
        std::filesystem::create_directories(tmpMcm);

        {
            std::ofstream ini(tmpMcm / "FallUIIconLibrary.ini");
            ini << "[MainSettings]\n";
            ini << "sIconsColorSet=CustomSet.xml\n";
            ini << "bIconsColored=1\n";
            ini << "bIconsEffect=1\n";
            ini << "iIconsBrightness=50\n";
            ini << "iIconsSaturation=100\n";
        }
        {
            std::ofstream falluiIni(tmpMcm / "FallUI.ini");
            falluiIni << "[ItemSorter]\n";
            falluiIni << "sItemSorterTagConfig=Auto-detect.xml\n";
        }
        {
            std::ofstream autoDetectXml(tmpIface / "ItemSorter" / "Auto-detect.xml");
            autoDetectXml << "<TagConfiguration></TagConfiguration>\n";
        }
        {
            std::ofstream defXml(tmpIface / "ItemSorter" / "ColorSets" / "Default.xml");
            defXml << "<colors>\n  <color name=\"c_only_default\" hex=\"#FF0000\" />\n</colors>\n";
        }
        {
            std::ofstream customXml(tmpIface / "ItemSorter" / "ColorSets" / "CustomSet.xml");
            customXml << "<colors>\n  <color name=\"c_custom\" hex=\"#00FF80\" />\n</colors>\n";
            customXml << "<tags>\n  <tag keyword=\"Note\" colorname=\"c_custom\" />\n</tags>\n";
        }
        {
            std::ofstream fisXml(tmpIface / "ItemSorter" / "FIS (FallUI Item Sorter).xml");
            fisXml << "<tags iconlibraryfile=\"FallUI_IconLib.swf\">\n";
            fisXml << "  <tag keyword=\"Ammo\" icon=\"ammo\" colorname=\"c_custom\" />\n";
            fisXml << "  <tag keyword=\"Note\" icon=\"note\" colorname=\"c_only_default\" />\n";
            fisXml << "  <tag keyword=\"Junk\" icon=\"junk\" subicon=\"sub\" colorname=\"c_only_default,c_custom\" />\n";
            fisXml << "</tags>\n";
        }

        FallUIIcons::FallUIIconRegistry csReg;
        CHECK(csReg.InitializeFromInterfaceDir(tmpIface));
        CHECK(csReg.ActiveColorSetName() == "CustomSet.xml");
        CHECK(csReg.IconsEffectEnabled());

        FallUIIcons::ResolvedFallUIIcon rcs;
        CHECK(csReg.ResolveIcon("{Ammo} 10mm Round", TargetInfo::ItemKind::Ammo, rcs));
        CHECK(rcs.symbolName == "ammo");
        CHECK(rcs.hasColor);
        // 50% brightness on #00FF80 -> #008040 (allow +/-1 rounding)
        const int cr = static_cast<int>((rcs.rgbColor >> 16) & 0xFFu);
        const int cg = static_cast<int>((rcs.rgbColor >> 8) & 0xFFu);
        const int cb = static_cast<int>(rcs.rgbColor & 0xFFu);
        CHECK(cr == 0);
        CHECK(std::abs(cg - 128) <= 1);
        CHECK(std::abs(cb - 64) <= 1);

        // Custom ColorSet does NOT preload Default.xml or apply color-only <tag> overrides (1:1 FallUI Inventory)
        FallUIIcons::ResolvedFallUIIcon rNote;
        CHECK(csReg.ResolveIcon("{Note} Scrap Note", TargetInfo::ItemKind::BookNote, rNote));
        CHECK(!rNote.hasColor);

        // Undefined primary in dual "primary,sub" never leaks sub color into primary
        FallUIIcons::ResolvedFallUIIcon rJunk;
        CHECK(csReg.ResolveIcon("{Junk} Tin Can", TargetInfo::ItemKind::MiscJunk, rJunk));
        CHECK(!rJunk.hasColor);
        CHECK(rJunk.hasSubColor);

        // Saturation=0 removes chroma cleanly
        {
            std::ofstream ini(tmpMcm / "FallUIIconLibrary.ini");
            ini << "[MainSettings]\n";
            ini << "sIconsColorSet=CustomSet.xml\n";
            ini << "bIconsColored=1\n";
            ini << "bIconsEffect=1\n";
            ini << "iIconsBrightness=100\n";
            ini << "iIconsSaturation=0\n";
        }
        CHECK(csReg.InitializeFromInterfaceDir(tmpIface));
        CHECK(csReg.ResolveIcon("{Ammo} 10mm Round", TargetInfo::ItemKind::Ammo, rcs));
        CHECK(rcs.hasColor && rcs.rgbColor == 0xFFFFFFu);

        // bIconsColored=0 strips all tag colors and sets ColorCount() == 0
        {
            std::ofstream ini(tmpMcm / "FallUIIconLibrary.ini");
            ini << "[MainSettings]\n";
            ini << "sIconsColorSet=CustomSet.xml\n";
            ini << "bIconsColored=0\n";
            ini << "bIconsEffect=0\n";
        }
        CHECK(csReg.InitializeFromInterfaceDir(tmpIface));
        CHECK(!csReg.IconsEffectEnabled());
        CHECK(csReg.ColorCount() == 0u);
        CHECK(csReg.ResolveIcon("{Ammo} 10mm Round", TargetInfo::ItemKind::Ammo, rcs));
        CHECK(!rcs.hasColor);

        csReg.Clear();
        CHECK(csReg.IconsEffectEnabled());
        CHECK(csReg.ActiveColorSetName() == "Default.xml");

        // Direct ApplyIconRadialShading verification & bounds guards
        std::vector<std::uint8_t> shadeBuf(16u * 16u * 4u, 255u);
        shadeBuf[0] = 255u; shadeBuf[1] = 255u; shadeBuf[2] = 255u; shadeBuf[3] = 0u; // alpha=0 top-left
        FallUIIcons::ApplyIconRadialShading(shadeBuf, 16);
        CHECK(shadeBuf[0] == 255u && shadeBuf[3] == 0u);
        const std::size_t midIdx = (8u * 16u + 8u) * 4u;
        const std::size_t edgeIdx = (0u * 16u + 15u) * 4u;
        CHECK(shadeBuf[midIdx] >= 245u);
        CHECK(shadeBuf[edgeIdx] <= 185u && shadeBuf[edgeIdx] >= 165u);
        std::vector<std::uint8_t> badBuf(12, 200u);
        FallUIIcons::ApplyIconRadialShading(badBuf, 0);
        FallUIIcons::ApplyIconRadialShading(badBuf, 16);
        CHECK(badBuf[0] == 200u);

        // Real-disk Rescaled and Fixed Colors.xml test if present
        const std::filesystem::path gameInterface = "E:/SteamLibrary/steamapps/common/Fallout 4/Data/Interface";
        const auto realRescaled = gameInterface / "ItemSorter" / "ColorSets" / "Rescaled and Fixed Colors.xml";
        const auto realFisXml = gameInterface / "ItemSorter" / "FIS (FallUI Item Sorter).xml";
        const auto realSwf = gameInterface / "FallUI_IconLib.swf";
        if (std::filesystem::exists(realRescaled) && std::filesystem::exists(realFisXml) && std::filesystem::exists(realSwf)) {
            std::filesystem::copy_file(
                realRescaled,
                tmpIface / "ItemSorter" / "ColorSets" / "Rescaled and Fixed Colors.xml",
                std::filesystem::copy_options::overwrite_existing);
            std::filesystem::copy_file(
                realSwf,
                tmpIface / "FallUI_IconLib.swf",
                std::filesystem::copy_options::overwrite_existing);
            {
                std::ofstream ini(tmpMcm / "FallUIIconLibrary.ini");
                ini << "[MainSettings]\n";
                ini << "sIconsColorSet=Rescaled and Fixed Colors.xml\n";
                ini << "bIconsColored=1\n";
                ini << "bIconsEffect=1\n";
                ini << "iIconsBrightness=100\n";
                ini << "iIconsSaturation=100\n";
            }
            std::filesystem::copy_file(
                realFisXml,
                tmpIface / "ItemSorter" / "FIS (FallUI Item Sorter).xml",
                std::filesystem::copy_options::overwrite_existing);
            std::filesystem::copy_file(
                gameInterface / "ItemSorter" / "ColorSets" / "Default.xml",
                tmpIface / "ItemSorter" / "ColorSets" / "Default.xml",
                std::filesystem::copy_options::overwrite_existing);

            FallUIIcons::FallUIIconRegistry realCsReg;
            CHECK(realCsReg.InitializeFromInterfaceDir(tmpIface));
            CHECK(realCsReg.ActiveColorSetName() == "Rescaled and Fixed Colors.xml");
            CHECK(realCsReg.IconsEffectEnabled());

            FallUIIcons::ResolvedFallUIIcon rFood, rClothes, rGrenade, rShells, rCorn, rRaider;
            CHECK(realCsReg.ResolveIcon("[Food] Pop's Dog Food", TargetInfo::ItemKind::Aid, rFood));
            CHECK(rFood.hasColor && rFood.rgbColor == 0xE9B49Bu);
            CHECK(realCsReg.ResolveIcon("[Clothes] Steel Plate Arm Left", TargetInfo::ItemKind::Armor, rClothes));
            CHECK(rClothes.hasColor && rClothes.rgbColor == 0xE9DA93u);
            CHECK(realCsReg.ResolveIcon("[Grenade] Fragmentation Grenade", TargetInfo::ItemKind::Weapon, rGrenade));
            CHECK(rGrenade.hasColor && rGrenade.rgbColor == 0xACC1A3u);
            CHECK(realCsReg.ResolveIcon("[AmmoShells] Shotgun Shell", TargetInfo::ItemKind::Ammo, rShells));
            CHECK(!rShells.hasColor);
            CHECK(realCsReg.ResolveIcon("[Vegetables] Corn", TargetInfo::ItemKind::Aid, rCorn));
            CHECK(!rCorn.hasColor);
            CHECK(realCsReg.ResolveIcon("[SetRaider] Raider Right Arm", TargetInfo::ItemKind::Armor, rRaider));
            CHECK(!rRaider.hasColor);

            std::vector<std::uint8_t> shadedRgba;
            CHECK(realCsReg.RasterizeResolvedIcon(rFood, 32, shadedRgba));
            int minLit = 255;
            int maxLit = 0;
            for (std::size_t i = 0; i + 3 < shadedRgba.size(); i += 4) {
                if (shadedRgba[i + 3] > 200u) {
                    minLit = std::min(minLit, static_cast<int>(shadedRgba[i]));
                    maxLit = std::max(maxLit, static_cast<int>(shadedRgba[i]));
                }
            }
            CHECK(maxLit >= 230 && minLit <= 220);

            // Toggle bIconsEffect=0 -> flat white pixels (255) on rasterized monochrome tile
            {
                std::ofstream ini(tmpMcm / "FallUIIconLibrary.ini");
                ini << "[MainSettings]\n";
                ini << "sIconsColorSet=Rescaled and Fixed Colors.xml\n";
                ini << "bIconsColored=1\n";
                ini << "bIconsEffect=0\n";
            }
            CHECK(realCsReg.InitializeFromInterfaceDir(tmpIface));
            CHECK(!realCsReg.IconsEffectEnabled());
            std::vector<std::uint8_t> flatRgba;
            CHECK(realCsReg.RasterizeResolvedIcon(rFood, 32, flatRgba));
            int minFlat = 255;
            for (std::size_t i = 0; i + 3 < flatRgba.size(); i += 4) {
                if (flatRgba[i + 3] > 200u) {
                    minFlat = std::min(minFlat, static_cast<int>(flatRgba[i]));
                }
            }
            CHECK(minFlat == 255);
        }

        std::filesystem::remove_all(tmpRoot);
    }

    std::cout << "[TEST] test_fallui_icons PASSED" << std::endl;
    return 0;
}

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

        // Write AutoTags_en.ini with specific rule + wildcard '*' catch-all
        {
            std::ofstream ofs(tmp / "ItemSorter" / "FIS" / "AutoTags" / "AutoTags_en.ini");
            ofs << "[WEAP]\nRevolver=[Pistol]\n";
            ofs << "[MISC]\n*=[CustomWild]\n";
        }

        FallUIIcons::FallUIIconRegistry reg;
        CHECK(reg.InitializeFromInterfaceDir(tmp));
        CHECK(reg.TagCount() == 7);
        CHECK(reg.ColorCount() >= 6);

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
    }

    std::cout << "[TEST] test_fallui_icons PASSED" << std::endl;
    return 0;
}

#include "check.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include "../src/Config.h"

int main()
{
    std::cout << "[TEST] Running test_config..." << std::endl;

    auto& cfg = Config::GetSingleton();
    cfg.ResetDefaults();

    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "fap_test_config";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    // 1. Test defaults via thread-safe snapshot
    {
        const ConfigData snap = cfg.GetSnapshot();
        CHECK(snap.enabled == true);
        CHECK_NEAR(snap.smoothingSpeed, 0.25f, 0.0001f);
        CHECK(snap.powerArmorBypass == true);
        CHECK(snap.enableClamping == true);
        CHECK_NEAR(snap.edgeMargin, 40.0f, 0.0001f);

        CHECK_NEAR(snap.doorOffset.x, 0.0f, 0.0001f);
        CHECK_NEAR(snap.doorOffset.y, 0.0f, 0.0001f);
        CHECK_NEAR(snap.doorOffset.z, -20.0f, 0.0001f);
        CHECK_NEAR(snap.npcOffset.z, 10.0f, 0.0001f);
        CHECK_NEAR(snap.containerOffset.z, 15.0f, 0.0001f);
        CHECK_NEAR(snap.itemOffset.z, -8.0f, 0.0001f);
        CHECK_NEAR(snap.furnitureOffset.x, 0.0f, 0.0001f);
        CHECK_NEAR(snap.furnitureOffset.y, 0.0f, 0.0001f);
        CHECK_NEAR(snap.furnitureOffset.z, 10.0f, 0.0001f);

        // v1.0.0 release defaults
        CHECK(snap.promptStyle == 1);
        CHECK(snap.buttonIconStyle == 0);
        CHECK(snap.iconLibrary == 1);
        CHECK(snap.modernShowItemIcon == true);
        CHECK(snap.coloredCategoryIcons == false);
        CHECK_NEAR(snap.modernScale, 0.85f, 0.0001f);
        CHECK_NEAR(snap.modernOpacity, 0.85f, 0.0001f);
        CHECK(snap.inlineItemInfo == false);
        CHECK(snap.modernTextMode == 0);
        CHECK(snap.showDoorDestination == true);
        CHECK(snap.cleanSorterTags == true);
        CHECK_NEAR(snap.modernBgOpacity, 0.0f, 0.0001f);
        CHECK(snap.modernBorder == false);
        CHECK(snap.modernTextShadow == true);
        CHECK_NEAR(snap.modernRounding, 6.0f, 0.0001f);
        CHECK_NEAR(snap.modernPadX, 8.0f, 0.0001f);
        CHECK_NEAR(snap.modernPadY, 4.0f, 0.0001f);
        CHECK_NEAR(snap.modernItemGap, 7.0f, 0.0001f);
        CHECK(snap.multiActionLayout == 0);
        CHECK_NEAR(snap.modernActionGap, 4.0f, 0.0001f);
        CHECK_NEAR(snap.modernButtonSize, 0.70f, 0.0001f);
        CHECK_NEAR(snap.modernKeyTextScale, 0.80f, 0.0001f);
        CHECK_NEAR(snap.modernActionTextScale, 0.85f, 0.0001f);
        CHECK_NEAR(snap.modernIconScale, 0.70f, 0.0001f);
        CHECK_NEAR(snap.modernScreenOffsetX, 10.0f, 0.0001f);
        CHECK_NEAR(snap.modernScreenOffsetY, 0.0f, 0.0001f);
        CHECK(snap.iconWeapon && snap.iconArmor && snap.iconAid && snap.iconAmmo);
        CHECK(snap.iconMiscJunk && snap.iconKey && snap.iconBookNote && snap.iconHolotape);
        CHECK(snap.iconContainer && snap.iconTerminal);
        CHECK(!snap.iconDoor && !snap.iconFurniture && !snap.iconNpc);
        CHECK(snap.promptFont.empty());
        CHECK(snap.modernBgColor == 0x0E1116u);
        CHECK(snap.modernBorderColor == 0xFFFFFFu);
        CHECK(snap.modernTextColor == 0xFFFFFFu);
        CHECK(snap.modernNameColor == 0xE0E8F0u);
        CHECK(snap.modernIconColor == 0xFFFFFFu);
        CHECK(snap.modernAccentColor == 0xF6C86Eu);

        CHECK(snap.infoCardPosition == 3);
        CHECK_NEAR(snap.infoCardOffsetX, 16.0f, 0.0001f);
        CHECK_NEAR(snap.infoCardOffsetY, 0.0f, 0.0001f);
        CHECK_NEAR(snap.infoCardScale, 0.75f, 0.0001f);
        CHECK_NEAR(snap.infoCardOpacity, 0.90f, 0.0001f);
        CHECK_NEAR(snap.infoCardDelaySeconds, 1.0f, 0.0001f);
        CHECK_NEAR(snap.infoCardBgOpacity, 0.70f, 0.0001f);
        CHECK(snap.infoCardBorder == true);
        CHECK(snap.infoCardAccentBar == false);
        CHECK(snap.infoCardShowIcon == true);
        CHECK_NEAR(snap.infoCardRounding, 0.0f, 0.0001f);
        CHECK_NEAR(snap.infoCardPad, 12.0f, 0.0001f);
        CHECK_NEAR(snap.infoCardRowGap, 2.0f, 0.0001f);
        CHECK(snap.infoCardFont.empty());
        CHECK(snap.infoCardBgColor == 0x0E1116u);
        CHECK(snap.infoCardBorderColor == 0xFFFFFFu);
        CHECK(snap.infoCardAccentColor == 0xF6C86Eu);
        CHECK(snap.infoCardTitleColor == 0xFFFFFFu);
        CHECK(snap.infoCardLabelColor == 0xA8B2C0u);
        CHECK(snap.infoCardValueColor == 0xF0F5FAu);
        CHECK(snap.showScrapComponents == true);
        CHECK(snap.showItemValueWeight == true);
        CHECK(snap.showWeaponArmorStats == true);
        CHECK(snap.showNpcStats == true);
        CHECK(snap.infoCardInQuickContainer == true);

        CHECK(snap.floatQuickContainer == true);
        CHECK(snap.modernQuickContainer == true);
        CHECK_NEAR(snap.quickContainerSmoothing, 0.30f, 0.0001f);
        CHECK_NEAR(snap.quickContainerDeadzone, 6.0f, 0.0001f);
        CHECK_NEAR(snap.quickContainerOffset.x, 0.0f, 0.0001f);
        CHECK_NEAR(snap.quickContainerOffset.y, 0.0f, 0.0001f);
        CHECK_NEAR(snap.quickContainerOffset.z, 15.0f, 0.0001f);
        CHECK_NEAR(snap.qcScale, 0.80f, 0.0001f);
        CHECK_NEAR(snap.qcWidth, 300.0f, 0.0001f);
        CHECK(snap.qcExpandInventoryList == true);
        CHECK(snap.qcMaxVisibleRows == 12);
        CHECK_NEAR(snap.qcRowHeightPad, 4.0f, 0.0001f);
        CHECK_NEAR(snap.qcHeaderTextScale, 1.0f, 0.0001f);
        CHECK_NEAR(snap.qcItemTextScale, 1.0f, 0.0001f);
        CHECK_NEAR(snap.qcButtonSize, 0.85f, 0.0001f);
        CHECK_NEAR(snap.qcKeyTextScale, 0.75f, 0.0001f);
        CHECK_NEAR(snap.qcButtonTextScale, 0.95f, 0.0001f);
        CHECK(snap.qcShowItemIcons == true);
        CHECK(snap.qcFont.empty());

        CHECK(!cfg.HasUnsavedChanges());
    }

    // 2. ApplySnapshot clamps invalid values and marks unsaved changes
    {
        ConfigData edit = cfg.GetSnapshot();
        edit.smoothingSpeed = 0.0f;
        edit.edgeMargin = -10.0f;
        edit.doorOffset.z = -35.0f;
        edit.furnitureOffset = { -250.0f, 300.0f, 999.0f };
        edit.promptStyle = 99;
        edit.buttonIconStyle = -5;
        edit.iconLibrary = 7;
        edit.modernScale = 10.0f;
        edit.modernOpacity = 0.01f;
        edit.modernTextMode = 9;
        edit.modernBgOpacity = -0.5f;
        edit.modernRounding = 99.0f;
        edit.modernPadX = -5.0f;
        edit.modernPadY = 99.0f;
        edit.modernItemGap = 0.0f;
        edit.modernTextColor = 0xFF112233u;
        edit.infoCardPosition = 42;
        edit.infoCardOffsetX = -999.0f;
        edit.infoCardScale = 0.1f;
        edit.infoCardOpacity = 2.0f;
        edit.infoCardDelaySeconds = 99.0f;
        edit.infoCardBgOpacity = 5.0f;
        edit.infoCardRounding = -3.0f;
        edit.infoCardPad = 1.0f;
        edit.infoCardRowGap = 99.0f;
        edit.quickContainerSmoothing = 0.0f;
        edit.quickContainerDeadzone = 99.0f;
        edit.multiActionLayout = 7;
        edit.modernButtonSize = 10.0f;
        edit.modernKeyTextScale = 0.05f;
        edit.modernScreenOffsetX = 999.0f;
        edit.qcWidth = 900.0f;
        edit.qcMaxVisibleRows = 50;
        edit.qcKeyTextScale = 9.0f;
        cfg.ApplySnapshot(edit, true);

        CHECK(cfg.HasUnsavedChanges());
        const ConfigData clamped = cfg.GetSnapshot();
        CHECK_NEAR(clamped.smoothingSpeed, 0.05f, 0.0001f);
        CHECK_NEAR(clamped.edgeMargin, 0.0f, 0.0001f);
        CHECK_NEAR(clamped.doorOffset.z, -35.0f, 0.0001f);
        CHECK_NEAR(clamped.furnitureOffset.x, -200.0f, 0.0001f);
        CHECK_NEAR(clamped.furnitureOffset.y, 200.0f, 0.0001f);
        CHECK_NEAR(clamped.furnitureOffset.z, 200.0f, 0.0001f);
        CHECK(clamped.promptStyle == 1);
        CHECK(clamped.buttonIconStyle == 0);
        CHECK(clamped.iconLibrary == 1);
        CHECK_NEAR(clamped.modernScale, 2.5f, 0.0001f);
        CHECK_NEAR(clamped.modernOpacity, 0.1f, 0.0001f);
        CHECK(clamped.modernTextMode == 2);
        CHECK_NEAR(clamped.modernBgOpacity, 0.0f, 0.0001f);
        CHECK_NEAR(clamped.modernRounding, 24.0f, 0.0001f);
        CHECK_NEAR(clamped.modernPadX, 0.0f, 0.0001f);
        CHECK_NEAR(clamped.modernPadY, 24.0f, 0.0001f);
        CHECK_NEAR(clamped.modernItemGap, 2.0f, 0.0001f);
        CHECK(clamped.modernTextColor == 0x112233u);
        CHECK(clamped.multiActionLayout == 1);
        CHECK_NEAR(clamped.modernButtonSize, 2.5f, 0.0001f);
        CHECK_NEAR(clamped.modernKeyTextScale, 0.40f, 0.0001f);
        CHECK_NEAR(clamped.modernScreenOffsetX, 200.0f, 0.0001f);
        CHECK_NEAR(clamped.qcWidth, 600.0f, 0.0001f);
        CHECK(clamped.qcMaxVisibleRows == 12);
        CHECK_NEAR(clamped.qcKeyTextScale, 1.20f, 0.0001f);
        CHECK(clamped.infoCardPosition == 6);
        CHECK_NEAR(clamped.infoCardOffsetX, -400.0f, 0.0001f);
        CHECK_NEAR(clamped.infoCardScale, 0.5f, 0.0001f);
        CHECK_NEAR(clamped.infoCardOpacity, 1.0f, 0.0001f);
        CHECK_NEAR(clamped.infoCardDelaySeconds, 5.0f, 0.0001f);
        CHECK_NEAR(clamped.infoCardBgOpacity, 1.0f, 0.0001f);
        CHECK_NEAR(clamped.infoCardRounding, 0.0f, 0.0001f);
        CHECK_NEAR(clamped.infoCardPad, 2.0f, 0.0001f);
        CHECK_NEAR(clamped.infoCardRowGap, 16.0f, 0.0001f);
        CHECK_NEAR(clamped.quickContainerSmoothing, 0.05f, 0.0001f);
        CHECK_NEAR(clamped.quickContainerDeadzone, 30.0f, 0.0001f);

        cfg.ClearUnsavedChanges();
        CHECK(!cfg.HasUnsavedChanges());

        edit.buttonIconStyle = 99;
        cfg.ApplySnapshot(edit, false);
        CHECK(cfg.GetSnapshot().buttonIconStyle == 4);
    }

    // 3. Save / Load round-trip & value clamping in tempDir
    {
        const auto tempPath = tempDir / "roundtrip.ini";
        ConfigData edit = cfg.GetSnapshot();
        edit.smoothingSpeed = 0.5f;
        edit.doorOffset.z = -35.0f;
        edit.furnitureOffset = { 1.5f, -3.5f, 12.0f };
        edit.promptStyle = 1;
        edit.buttonIconStyle = 2;
        edit.iconLibrary = 1;
        edit.modernShowItemIcon = false;
        edit.coloredCategoryIcons = true;
        edit.inlineItemInfo = true;
        edit.modernTextMode = 2;
        edit.showDoorDestination = false;
        edit.cleanSorterTags = false;
        edit.modernBgOpacity = 0.45f;
        edit.modernBorder = true;
        edit.modernTextShadow = false;
        edit.modernTextColor = 0x12AB34u;
        edit.modernAccentColor = 0xFF8800u;
        edit.infoCardPosition = 5;
        edit.infoCardOffsetX = -24.0f;
        edit.infoCardDelaySeconds = 1.5f;
        edit.infoCardBgOpacity = 0.0f;
        edit.infoCardBorder = false;
        edit.infoCardAccentBar = false;
        edit.infoCardShowIcon = false;
        edit.infoCardTitleColor = 0xAA55CCu;
        edit.showNpcStats = false;
        edit.floatQuickContainer = false;
        edit.modernQuickContainer = false;
        edit.quickContainerDeadzone = 12.5f;
        edit.quickContainerOffset.z = 22.0f;
        edit.multiActionLayout = 1;
        edit.modernActionGap = 9.0f;
        edit.modernButtonSize = 1.35f;
        edit.modernKeyTextScale = 0.78f;
        edit.modernActionTextScale = 1.25f;
        edit.modernIconScale = 0.85f;
        edit.modernScreenOffsetX = 12.0f;
        edit.modernScreenOffsetY = -18.0f;
        edit.iconDoor = false;
        edit.iconFurniture = false;
        edit.infoCardInQuickContainer = false;
        edit.qcScale = 1.15f;
        edit.qcWidth = 340.0f;
        edit.qcExpandInventoryList = false;
        edit.qcMaxVisibleRows = 8;
        edit.qcRowHeightPad = 9.0f;
        edit.qcHeaderTextScale = 1.20f;
        edit.qcItemTextScale = 1.10f;
        edit.qcButtonSize = 1.25f;
        edit.qcKeyTextScale = 0.74f;
        edit.qcButtonTextScale = 1.15f;
        edit.qcShowItemIcons = false;
        edit.promptFont = "  segoeui.ttf\r\n";
        edit.qcFont = std::string(300, 'a') + ".ttf";
        edit.infoCardFont = "consola.ttf";
        cfg.ApplySnapshot(edit, false);
        CHECK(cfg.Save(tempPath));

        cfg.ResetDefaults();
        CHECK(cfg.Load(tempPath));
        const auto loaded = cfg.GetSnapshot();
        CHECK_NEAR(loaded.smoothingSpeed, 0.5f, 0.0001f);
        CHECK_NEAR(loaded.doorOffset.z, -35.0f, 0.0001f);
        CHECK_NEAR(loaded.furnitureOffset.x, 1.5f, 0.0001f);
        CHECK_NEAR(loaded.furnitureOffset.y, -3.5f, 0.0001f);
        CHECK_NEAR(loaded.furnitureOffset.z, 12.0f, 0.0001f);
        CHECK(loaded.promptStyle == 1);
        CHECK(loaded.buttonIconStyle == 2);
        CHECK(loaded.iconLibrary == 1);
        CHECK(loaded.modernShowItemIcon == false);
        CHECK(loaded.coloredCategoryIcons == true);
        CHECK(loaded.inlineItemInfo == true);
        CHECK(loaded.modernTextMode == 2);
        CHECK(loaded.showDoorDestination == false);
        CHECK(loaded.cleanSorterTags == false);
        CHECK_NEAR(loaded.modernBgOpacity, 0.45f, 0.0001f);
        CHECK(loaded.modernBorder == true);
        CHECK(loaded.modernTextShadow == false);
        CHECK(loaded.modernTextColor == 0x12AB34u);
        CHECK(loaded.modernAccentColor == 0xFF8800u);
        CHECK(loaded.infoCardPosition == 5);
        CHECK_NEAR(loaded.infoCardOffsetX, -24.0f, 0.0001f);
        CHECK_NEAR(loaded.infoCardDelaySeconds, 1.5f, 0.0001f);
        CHECK_NEAR(loaded.infoCardBgOpacity, 0.0f, 0.0001f);
        CHECK(loaded.infoCardBorder == false);
        CHECK(loaded.infoCardAccentBar == false);
        CHECK(loaded.infoCardShowIcon == false);
        CHECK(loaded.infoCardTitleColor == 0xAA55CCu);
        CHECK(loaded.showNpcStats == false);
        CHECK(loaded.floatQuickContainer == false);
        CHECK(loaded.modernQuickContainer == false);
        CHECK_NEAR(loaded.quickContainerDeadzone, 12.5f, 0.0001f);
        CHECK_NEAR(loaded.quickContainerOffset.z, 22.0f, 0.0001f);
        CHECK(loaded.multiActionLayout == 1);
        CHECK_NEAR(loaded.modernActionGap, 9.0f, 0.0001f);
        CHECK_NEAR(loaded.modernButtonSize, 1.35f, 0.0001f);
        CHECK_NEAR(loaded.modernKeyTextScale, 0.78f, 0.0001f);
        CHECK_NEAR(loaded.modernActionTextScale, 1.25f, 0.0001f);
        CHECK_NEAR(loaded.modernIconScale, 0.85f, 0.0001f);
        CHECK_NEAR(loaded.modernScreenOffsetX, 12.0f, 0.0001f);
        CHECK_NEAR(loaded.modernScreenOffsetY, -18.0f, 0.0001f);
        CHECK(loaded.iconDoor == false);
        CHECK(loaded.iconFurniture == false);
        CHECK(loaded.iconWeapon == true);
        CHECK(loaded.infoCardInQuickContainer == false);
        CHECK_NEAR(loaded.qcScale, 1.15f, 0.0001f);
        CHECK_NEAR(loaded.qcWidth, 340.0f, 0.0001f);
        CHECK(loaded.qcExpandInventoryList == false);
        CHECK(loaded.qcMaxVisibleRows == 8);

        cfg.ResetDefaults();
        CHECK(cfg.GetSnapshot().qcExpandInventoryList == true);
        CHECK_NEAR(loaded.qcRowHeightPad, 9.0f, 0.0001f);
        CHECK_NEAR(loaded.qcHeaderTextScale, 1.20f, 0.0001f);
        CHECK_NEAR(loaded.qcItemTextScale, 1.10f, 0.0001f);
        CHECK_NEAR(loaded.qcButtonSize, 1.25f, 0.0001f);
        CHECK_NEAR(loaded.qcKeyTextScale, 0.74f, 0.0001f);
        CHECK_NEAR(loaded.qcButtonTextScale, 1.15f, 0.0001f);
        CHECK(loaded.qcShowItemIcons == false);
        CHECK(loaded.promptFont == "segoeui.ttf");
        CHECK(loaded.qcFont.size() == 260);
        CHECK(loaded.infoCardFont == "consola.ttf");

        {
            std::ofstream bad(tempDir / "bad.ini");
            bad << "[General]\nfSmoothingSpeed=5\nfEdgeMargin=999\n";
        }
        CHECK(cfg.Load(tempDir / "bad.ini"));
        const auto clampedBad = cfg.GetSnapshot();
        CHECK_NEAR(clampedBad.smoothingSpeed, 1.0f, 0.0001f);
        CHECK_NEAR(clampedBad.edgeMargin, 400.0f, 0.0001f);

        CHECK(!cfg.Load(tempDir / "does_not_exist.ini"));

        // Save/Load round-trip for Monochrome PlayStation (3) and Monochrome Xbox (4)
        for (int monoStyle : { 3, 4 }) {
            ConfigData monoEdit = cfg.GetSnapshot();
            monoEdit.buttonIconStyle = monoStyle;
            cfg.ApplySnapshot(monoEdit, false);
            const auto monoPath = tempDir / ("mono_" + std::to_string(monoStyle) + ".ini");
            CHECK(cfg.Save(monoPath));
            cfg.ResetDefaults();
            CHECK(cfg.Load(monoPath));
            CHECK(cfg.GetSnapshot().buttonIconStyle == monoStyle);
        }
    }

    // 4. LoadFromPaths precedence: mcmUser > plugin > default, and SaveUserTo leaves plugin INI intact
    {
        const auto defIni = tempDir / "default.ini";
        const auto plugIni = tempDir / "plugin.ini";
        const auto userIni = tempDir / "user.ini";

        {
            std::ofstream f(defIni);
            f << "[General]\nfSmoothingSpeed=0.15\nbPowerArmorBypass=1\n";
            f << "[Offsets]\nfDoorOffsetZ=-10.0\nfItemOffsetZ=5.0\n";
        }
        {
            std::ofstream f(plugIni);
            f << "[General]\nfSmoothingSpeed=0.35\n";
            f << "[Offsets]\nfDoorOffsetZ=-22.0\n";
        }
        {
            std::ofstream f(userIni);
            f << "[Offsets]\nfDoorOffsetZ=-40.0\n";
        }

        cfg.LoadFromPaths(defIni, plugIni, userIni);
        const auto merged = cfg.GetSnapshot();
        CHECK_NEAR(merged.itemOffset.z, 5.0f, 0.0001f);      // from default
        CHECK_NEAR(merged.smoothingSpeed, 0.35f, 0.0001f); // overridden by plugin
        CHECK_NEAR(merged.doorOffset.z, -40.0f, 0.0001f);  // overridden by user
        CHECK(!cfg.HasUnsavedChanges());

        // Modify and save only to userIni; pluginIni must NOT be modified
        ConfigData mod = merged;
        mod.doorOffset.z = -55.0f;
        cfg.ApplySnapshot(mod, true);
        CHECK(cfg.SaveUserTo(userIni));
        CHECK(!cfg.HasUnsavedChanges());

        // Reloading pluginIni alone must still have -22.0
        cfg.LoadFromPaths(defIni, plugIni, tempDir / "no_user.ini");
        CHECK_NEAR(cfg.GetSnapshot().doorOffset.z, -22.0f, 0.0001f);

        // 5. Unprefixed hex colors & NaN float sanitization
        {
            std::ofstream f(tempDir / "edge_values.ini");
            f << "[Visual]\nuModernBgColor=0E1116\nuModernAccentColor=#F6C86E\nfModernOpacity=nan\n";
            f << "[InfoCard]\nfInfoCardDelaySeconds=nan\n";
            f << "[Offsets]\nfItemOffsetZ=nan\n";
        }
        CHECK(cfg.Load(tempDir / "edge_values.ini"));
        const auto edgeCfg = cfg.GetSnapshot();
        CHECK(edgeCfg.modernBgColor == 0x0E1116u);
        CHECK(edgeCfg.modernAccentColor == 0xF6C86Eu);
        CHECK_NEAR(edgeCfg.modernOpacity, 0.85f, 0.0001f);
        CHECK_NEAR(edgeCfg.infoCardDelaySeconds, 1.0f, 0.0001f);
        CHECK_NEAR(edgeCfg.itemOffset.z, -8.0f, 0.0001f);
    }

    std::filesystem::remove_all(tempDir);

    std::cout << "[TEST] test_config PASSED" << std::endl;
    return 0;
}

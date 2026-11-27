#include "Config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>

namespace
{
    constexpr std::array<const char*, kQuickLootCategoryCount> kQuickLootCategoryKeys = {
        "Key",
        "Medicine",
        "Chem",
        "Drink",
        "Food",
        "Ammo",
        "Explosive",
        "Pistol",
        "Rifle",
        "HeavyWeapon",
        "Melee",
        "PowerArmor",
        "Armor",
        "Clothing",
        "Mod",
        "BookNote",
        "Holotape",
        "Misc",
        "Junk"
    };

    constexpr std::array<const char*, kQuickLootCategoryCount> kQuickLootCategoryLabels = {
        "Keys",
        "Aid: Medicine",
        "Aid: Chems",
        "Aid: Drinks",
        "Aid: Food",
        "Ammo",
        "Weapons: Explosives",
        "Weapons: Pistols",
        "Weapons: Rifles & Shotguns",
        "Weapons: Heavy",
        "Weapons: Melee",
        "Armor: Power Armor",
        "Armor: Pieces",
        "Armor: Clothing",
        "Mods",
        "Books & Notes",
        "Holotapes",
        "Misc",
        "Junk"
    };

    [[nodiscard]] bool EqualsIgnoreCaseAscii(std::string_view a_lhs, std::string_view a_rhs) noexcept
    {
        if (a_lhs.size() != a_rhs.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a_lhs.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a_lhs[i])) !=
                std::tolower(static_cast<unsigned char>(a_rhs[i]))) {
                return false;
            }
        }
        return true;
    }
    std::string Trim(const std::string& str)
    {
        const auto first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return "";
        }
        const auto last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    std::string SanitizeFontSetting(std::string_view a_raw)
    {
        std::string cleaned;
        cleaned.reserve(std::min<std::size_t>(a_raw.size(), 260));
        for (const char ch : a_raw) {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (uch < 0x20 || uch == 0x7F) {
                continue;
            }
            cleaned.push_back(ch);
            if (cleaned.size() >= 260) {
                break;
            }
        }
        return Trim(cleaned);
    }

    bool ParseBool(const std::string& valStr)
    {
        if (valStr == "true" || valStr == "True" || valStr == "TRUE") {
            return true;
        }
        if (valStr == "false" || valStr == "False" || valStr == "FALSE") {
            return false;
        }
        return std::stoi(valStr) != 0;
    }

    std::uint32_t ParseColorHex(const std::string& a_valStr)
    {
        std::string clean = Trim(a_valStr);
        if (clean.empty()) {
            return 0xFFFFFFu;
        }
        if (clean.front() == '#') {
            clean = clean.substr(1);
        } else if (clean.size() >= 2 && clean[0] == '0' && (clean[1] == 'x' || clean[1] == 'X')) {
            clean = clean.substr(2);
        }
        if (clean.empty() || clean.size() > 8) {
            return 0xFFFFFFu;
        }
        for (const char ch : clean) {
            if (!std::isxdigit(static_cast<unsigned char>(ch))) {
                return 0xFFFFFFu;
            }
        }
        try {
            return static_cast<std::uint32_t>(std::stoul(clean, nullptr, 16)) & 0x00FFFFFFu;
        } catch (...) {
            return 0xFFFFFFu;
        }
    }

    std::string FormatColorHex(std::uint32_t a_rgb)
    {
        char buf[16]{};
        std::snprintf(buf, sizeof(buf), "0x%06X", a_rgb & 0x00FFFFFFu);
        return std::string(buf);
    }

    void ClampFinite(float& a_val, float a_lo, float a_hi, float a_default) noexcept
    {
        if (!std::isfinite(a_val)) {
            a_val = a_default;
        }
        a_val = std::clamp(a_val, a_lo, a_hi);
    }

    void ClampOffset(Vector3& a_offset, const Vector3& a_default) noexcept
    {
        ClampFinite(a_offset.x, -500.0f, 500.0f, a_default.x);
        ClampFinite(a_offset.y, -500.0f, 500.0f, a_default.y);
        ClampFinite(a_offset.z, -500.0f, 500.0f, a_default.z);
    }

    void ClampData(ConfigData& a_data)
    {
        const ConfigData defaults{};

        ClampFinite(a_data.smoothingSpeed, 0.05f, 1.0f, defaults.smoothingSpeed);
        ClampFinite(a_data.edgeMargin, 0.0f, 400.0f, defaults.edgeMargin);
        ClampFinite(a_data.companionMaxDistance, 50.0f, 500.0f, defaults.companionMaxDistance);
        ClampFinite(a_data.companionCombatMaxDistance, 0.0f, 500.0f, defaults.companionCombatMaxDistance);
        ClampFinite(a_data.grabHoldDelay, 0.30f, 2.00f, defaults.grabHoldDelay);
        a_data.featuredItemGraceMs = std::clamp(a_data.featuredItemGraceMs, 0, 2000);
        a_data.aimingPromptMode = std::clamp(a_data.aimingPromptMode, 0, 2);
        ClampFinite(a_data.npcMaxDistance, 60.0f, 500.0f, defaults.npcMaxDistance);
        ClampFinite(a_data.npcRangedMaxDistance, 0.0f, 2000.0f, defaults.npcRangedMaxDistance);

        a_data.tpAssistMode = std::clamp(a_data.tpAssistMode, 0, 1);
        ClampFinite(a_data.tpAssistMaxDistance, 60.0f, 250.0f, defaults.tpAssistMaxDistance);
        ClampFinite(a_data.tpAssistConeDegrees, 20.0f, 90.0f, defaults.tpAssistConeDegrees);
        a_data.tpAssistLockMs = std::clamp(a_data.tpAssistLockMs, 0, 1000);

        ClampFinite(a_data.holdToActivateDuration, 0.20f, 2.00f, defaults.holdToActivateDuration);
        a_data.holdRingStyle = std::clamp(a_data.holdRingStyle, 0, 1);
        a_data.holdCustomVerbs = SanitizeFontSetting(a_data.holdCustomVerbs);

        ClampOffset(a_data.doorOffset, defaults.doorOffset);
        ClampOffset(a_data.npcOffset, defaults.npcOffset);
        ClampOffset(a_data.containerOffset, defaults.containerOffset);
        ClampOffset(a_data.itemOffset, defaults.itemOffset);
        ClampFinite(a_data.furnitureOffset.x, -200.0f, 200.0f, defaults.furnitureOffset.x);
        ClampFinite(a_data.furnitureOffset.y, -200.0f, 200.0f, defaults.furnitureOffset.y);
        ClampFinite(a_data.furnitureOffset.z, -200.0f, 200.0f, defaults.furnitureOffset.z);
        ClampOffset(a_data.quickContainerOffset, defaults.quickContainerOffset);

        a_data.promptStyle = std::clamp(a_data.promptStyle, 0, 1);
        a_data.buttonIconStyle = std::clamp(a_data.buttonIconStyle, 0, 4);
        a_data.iconLibrary = std::clamp(a_data.iconLibrary, 0, 1);
        ClampFinite(a_data.modernScale, 0.5f, 2.5f, defaults.modernScale);
        ClampFinite(a_data.modernOpacity, 0.1f, 1.0f, defaults.modernOpacity);
        a_data.modernTextMode = std::clamp(a_data.modernTextMode, 0, 2);
        ClampFinite(a_data.modernBgOpacity, 0.0f, 1.0f, defaults.modernBgOpacity);
        ClampFinite(a_data.modernRounding, 0.0f, 24.0f, defaults.modernRounding);
        ClampFinite(a_data.modernPadX, 0.0f, 32.0f, defaults.modernPadX);
        ClampFinite(a_data.modernPadY, 0.0f, 24.0f, defaults.modernPadY);
        ClampFinite(a_data.modernItemGap, 2.0f, 24.0f, defaults.modernItemGap);
        a_data.multiActionLayout = std::clamp(a_data.multiActionLayout, 0, 1);
        ClampFinite(a_data.modernActionGap, 0.0f, 24.0f, defaults.modernActionGap);
        ClampFinite(a_data.modernButtonSize, 0.5f, 2.5f, defaults.modernButtonSize);
        ClampFinite(a_data.modernKeyTextScale, 0.40f, 1.20f, defaults.modernKeyTextScale);
        ClampFinite(a_data.modernActionTextScale, 0.5f, 2.5f, defaults.modernActionTextScale);
        ClampFinite(a_data.modernIconScale, 0.5f, 2.5f, defaults.modernIconScale);
        ClampFinite(a_data.modernScreenOffsetX, -200.0f, 200.0f, defaults.modernScreenOffsetX);
        ClampFinite(a_data.modernScreenOffsetY, -200.0f, 200.0f, defaults.modernScreenOffsetY);
        a_data.modernBgColor &= 0x00FFFFFFu;
        a_data.modernBorderColor &= 0x00FFFFFFu;
        a_data.modernTextColor &= 0x00FFFFFFu;
        a_data.modernNameColor &= 0x00FFFFFFu;
        a_data.modernIconColor &= 0x00FFFFFFu;
        a_data.modernAccentColor &= 0x00FFFFFFu;
        a_data.crimeTextColor &= 0x00FFFFFFu;
        a_data.promptFont = SanitizeFontSetting(a_data.promptFont);

        a_data.infoCardPosition = std::clamp(a_data.infoCardPosition, 0, 6);
        ClampFinite(a_data.infoCardOffsetX, -400.0f, 400.0f, defaults.infoCardOffsetX);
        ClampFinite(a_data.infoCardOffsetY, -400.0f, 400.0f, defaults.infoCardOffsetY);
        ClampFinite(a_data.infoCardScale, 0.5f, 2.5f, defaults.infoCardScale);
        ClampFinite(a_data.infoCardOpacity, 0.1f, 1.0f, defaults.infoCardOpacity);
        ClampFinite(a_data.infoCardDelaySeconds, 0.0f, 5.0f, defaults.infoCardDelaySeconds);
        ClampFinite(a_data.infoCardBgOpacity, 0.0f, 1.0f, defaults.infoCardBgOpacity);
        ClampFinite(a_data.infoCardRounding, 0.0f, 24.0f, defaults.infoCardRounding);
        ClampFinite(a_data.infoCardPad, 2.0f, 28.0f, defaults.infoCardPad);
        ClampFinite(a_data.infoCardRowGap, 0.0f, 16.0f, defaults.infoCardRowGap);
        a_data.infoCardBgColor &= 0x00FFFFFFu;
        a_data.infoCardBorderColor &= 0x00FFFFFFu;
        a_data.infoCardAccentColor &= 0x00FFFFFFu;
        a_data.infoCardTitleColor &= 0x00FFFFFFu;
        a_data.infoCardLabelColor &= 0x00FFFFFFu;
        a_data.infoCardValueColor &= 0x00FFFFFFu;
        a_data.infoCardFont = SanitizeFontSetting(a_data.infoCardFont);
        a_data.infoCardConditionDisplay = std::clamp(a_data.infoCardConditionDisplay, 0, 3);

        ClampFinite(a_data.quickContainerSmoothing, 0.05f, 1.0f, defaults.quickContainerSmoothing);
        ClampFinite(a_data.quickContainerDeadzone, 0.0f, 30.0f, defaults.quickContainerDeadzone);
        ClampFinite(a_data.qcScale, 0.5f, 2.5f, defaults.qcScale);
        ClampFinite(a_data.qcWidth, 0.0f, 600.0f, defaults.qcWidth);
        a_data.qcMaxVisibleRows = std::clamp(a_data.qcMaxVisibleRows, 3, 12);
        ClampFinite(a_data.qcRowHeightPad, 0.0f, 20.0f, defaults.qcRowHeightPad);
        ClampFinite(a_data.qcHeaderTextScale, 0.5f, 2.5f, defaults.qcHeaderTextScale);
        ClampFinite(a_data.qcItemTextScale, 0.5f, 2.5f, defaults.qcItemTextScale);
        ClampFinite(a_data.qcButtonSize, 0.5f, 2.5f, defaults.qcButtonSize);
        ClampFinite(a_data.qcKeyTextScale, 0.40f, 1.20f, defaults.qcKeyTextScale);
        ClampFinite(a_data.qcButtonTextScale, 0.5f, 2.5f, defaults.qcButtonTextScale);
        a_data.qcFont = SanitizeFontSetting(a_data.qcFont);
        a_data.qcSortMode = std::clamp(a_data.qcSortMode, 0, 6);
        a_data.qcCategorySecondarySort = std::clamp(a_data.qcCategorySecondarySort, 0, 3);
        a_data.qcTakeDebounceMs = std::clamp(a_data.qcTakeDebounceMs, 0, 300);
        a_data.itemConditionDisplay = std::clamp(a_data.itemConditionDisplay, 0, 3);
        SanitizeQuickLootCategoryOrder(a_data.qcCategoryOrder);
    }

    bool ParseFileInto(const std::filesystem::path& a_path, ConfigData& a_target)
    {
        if (!std::filesystem::exists(a_path)) {
            return false;
        }

        std::ifstream file(a_path);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        std::string currentSection;

        while (std::getline(file, line)) {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
                continue;
            }

            if (trimmed.front() == '[' && trimmed.back() == ']') {
                currentSection = Trim(trimmed.substr(1, trimmed.size() - 2));
                continue;
            }

            const auto eqPos = trimmed.find('=');
            if (eqPos == std::string::npos) {
                continue;
            }

            std::string key = Trim(trimmed.substr(0, eqPos));
            std::string valStr = Trim(trimmed.substr(eqPos + 1));

            try {
                if (currentSection == "General") {
                    if (key == "bEnabled") a_target.enabled = ParseBool(valStr);
                    else if (key == "fSmoothingSpeed") a_target.smoothingSpeed = std::stof(valStr);
                    else if (key == "bPowerArmorBypass") a_target.powerArmorBypass = ParseBool(valStr);
                    else if (key == "bEnableClamping") a_target.enableClamping = ParseBool(valStr);
                    else if (key == "fEdgeMargin") a_target.edgeMargin = std::stof(valStr);
                    else if (key == "bLimitCompanionPromptDistance") a_target.limitCompanionPromptDistance = ParseBool(valStr);
                    else if (key == "fCompanionMaxDistance") a_target.companionMaxDistance = std::stof(valStr);
                    else if (key == "fCompanionCombatMaxDistance") a_target.companionCombatMaxDistance = std::stof(valStr);
                    else if (key == "bCompanionDeprioritizeLoot") a_target.companionDeprioritizeLoot = ParseBool(valStr);
                    else if (key == "bBlockHiddenCompanionInteractions") a_target.blockHiddenCompanionInteractions = ParseBool(valStr);
                    else if (key == "fGrabHoldDelay") a_target.grabHoldDelay = std::stof(valStr);
                    else if (key == "iFeaturedItemGraceMs") a_target.featuredItemGraceMs = std::stoi(valStr);
                    else if (key == "iAimingPromptMode") a_target.aimingPromptMode = std::stoi(valStr);
                    else if (key == "bLimitNpcPromptDistance") a_target.limitNpcPromptDistance = ParseBool(valStr);
                    else if (key == "fNpcMaxDistance") a_target.npcMaxDistance = std::stof(valStr);
                    else if (key == "bNpcKeepRangedActions") a_target.npcKeepRangedActions = ParseBool(valStr);
                    else if (key == "fNpcRangedMaxDistance") a_target.npcRangedMaxDistance = std::stof(valStr);
                    else if (key == "bBlockPromptsThroughWalls") a_target.blockPromptsThroughWalls = ParseBool(valStr);
                } else if (currentSection == "ThirdPersonAssist") {
                    if (key == "bTpAssistEnabled") a_target.tpAssistEnabled = ParseBool(valStr);
                    else if (key == "iTpAssistMode") a_target.tpAssistMode = std::stoi(valStr);
                    else if (key == "fTpAssistMaxDistance") a_target.tpAssistMaxDistance = std::stof(valStr);
                    else if (key == "fTpAssistConeDegrees") a_target.tpAssistConeDegrees = std::stof(valStr);
                    else if (key == "iTpAssistLockMs") a_target.tpAssistLockMs = std::stoi(valStr);
                    else if (key == "bTpAssistItems") a_target.tpAssistItems = ParseBool(valStr);
                    else if (key == "bTpAssistDoors") a_target.tpAssistDoors = ParseBool(valStr);
                    else if (key == "bTpAssistFurniture") a_target.tpAssistFurniture = ParseBool(valStr);
                    else if (key == "bTpAssistContainers") a_target.tpAssistContainers = ParseBool(valStr);
                } else if (currentSection == "HoldToActivate") {
                    if (key == "bHoldToActivateEnabled") a_target.holdToActivateEnabled = ParseBool(valStr);
                    else if (key == "fHoldToActivateDuration") a_target.holdToActivateDuration = std::stof(valStr);
                    else if (key == "iHoldRingStyle") a_target.holdRingStyle = std::stoi(valStr);
                    else if (key == "bHoldDrink") a_target.holdDrink = ParseBool(valStr);
                    else if (key == "bHoldSitSleep") a_target.holdSitSleep = ParseBool(valStr);
                    else if (key == "bHoldStealPickpocket") a_target.holdStealPickpocket = ParseBool(valStr);
                    else if (key == "bHoldEatCannibal") a_target.holdEatCannibal = ParseBool(valStr);
                    else if (key == "bHoldCraftPowerArmor") a_target.holdCraftPowerArmor = ParseBool(valStr);
                    else if (key == "sHoldCustomVerbs") a_target.holdCustomVerbs = SanitizeFontSetting(valStr);
                } else if (currentSection == "Offsets") {
                    if (key == "fDoorOffsetX") a_target.doorOffset.x = std::stof(valStr);
                    else if (key == "fDoorOffsetY") a_target.doorOffset.y = std::stof(valStr);
                    else if (key == "fDoorOffsetZ") a_target.doorOffset.z = std::stof(valStr);
                    else if (key == "fNpcOffsetX") a_target.npcOffset.x = std::stof(valStr);
                    else if (key == "fNpcOffsetY") a_target.npcOffset.y = std::stof(valStr);
                    else if (key == "fNpcOffsetZ") a_target.npcOffset.z = std::stof(valStr);
                    else if (key == "fContainerOffsetX") a_target.containerOffset.x = std::stof(valStr);
                    else if (key == "fContainerOffsetY") a_target.containerOffset.y = std::stof(valStr);
                    else if (key == "fContainerOffsetZ") a_target.containerOffset.z = std::stof(valStr);
                    else if (key == "fItemOffsetX") a_target.itemOffset.x = std::stof(valStr);
                    else if (key == "fItemOffsetY") a_target.itemOffset.y = std::stof(valStr);
                    else if (key == "fItemOffsetZ") a_target.itemOffset.z = std::stof(valStr);
                    else if (key == "fFurnitureOffsetX") a_target.furnitureOffset.x = std::stof(valStr);
                    else if (key == "fFurnitureOffsetY") a_target.furnitureOffset.y = std::stof(valStr);
                    else if (key == "fFurnitureOffsetZ") a_target.furnitureOffset.z = std::stof(valStr);
                } else if (currentSection == "Visual") {
                    if (key == "iPromptStyle") a_target.promptStyle = std::stoi(valStr);
                    else if (key == "iButtonIconStyle") a_target.buttonIconStyle = std::stoi(valStr);
                    else if (key == "iIconLibrary") a_target.iconLibrary = std::stoi(valStr);
                    else if (key == "bModernShowItemIcon") a_target.modernShowItemIcon = ParseBool(valStr);
                    else if (key == "bColoredCategoryIcons") a_target.coloredCategoryIcons = ParseBool(valStr);
                    else if (key == "fModernScale") a_target.modernScale = std::stof(valStr);
                    else if (key == "fModernOpacity") a_target.modernOpacity = std::stof(valStr);
                    else if (key == "bInlineItemInfo") a_target.inlineItemInfo = ParseBool(valStr);
                    else if (key == "iModernTextMode") a_target.modernTextMode = std::stoi(valStr);
                    else if (key == "bShowDoorDestination") a_target.showDoorDestination = ParseBool(valStr);
                    else if (key == "bShowInteractableHeader") a_target.showInteractableHeader = ParseBool(valStr);
                    else if (key == "bCleanSorterTags") a_target.cleanSorterTags = ParseBool(valStr);
                    else if (key == "fModernBgOpacity") a_target.modernBgOpacity = std::stof(valStr);
                    else if (key == "bModernBorder") a_target.modernBorder = ParseBool(valStr);
                    else if (key == "bModernTextShadow") a_target.modernTextShadow = ParseBool(valStr);
                    else if (key == "fModernRounding") a_target.modernRounding = std::stof(valStr);
                    else if (key == "fModernPadX") a_target.modernPadX = std::stof(valStr);
                    else if (key == "fModernPadY") a_target.modernPadY = std::stof(valStr);
                    else if (key == "fModernItemGap") a_target.modernItemGap = std::stof(valStr);
                    else if (key == "iMultiActionLayout") a_target.multiActionLayout = std::stoi(valStr);
                    else if (key == "fModernActionGap") a_target.modernActionGap = std::stof(valStr);
                    else if (key == "fModernButtonSize") a_target.modernButtonSize = std::stof(valStr);
                    else if (key == "fModernKeyTextScale") a_target.modernKeyTextScale = std::stof(valStr);
                    else if (key == "fModernActionTextScale") a_target.modernActionTextScale = std::stof(valStr);
                    else if (key == "fModernIconScale") a_target.modernIconScale = std::stof(valStr);
                    else if (key == "fModernScreenOffsetX") a_target.modernScreenOffsetX = std::stof(valStr);
                    else if (key == "fModernScreenOffsetY") a_target.modernScreenOffsetY = std::stof(valStr);
                    else if (key == "bIconWeapon") a_target.iconWeapon = ParseBool(valStr);
                    else if (key == "bIconArmor") a_target.iconArmor = ParseBool(valStr);
                    else if (key == "bIconAid") a_target.iconAid = ParseBool(valStr);
                    else if (key == "bIconAmmo") a_target.iconAmmo = ParseBool(valStr);
                    else if (key == "bIconMiscJunk") a_target.iconMiscJunk = ParseBool(valStr);
                    else if (key == "bIconKey") a_target.iconKey = ParseBool(valStr);
                    else if (key == "bIconBookNote") a_target.iconBookNote = ParseBool(valStr);
                    else if (key == "bIconHolotape") a_target.iconHolotape = ParseBool(valStr);
                    else if (key == "bIconContainer") a_target.iconContainer = ParseBool(valStr);
                    else if (key == "bIconDoor") a_target.iconDoor = ParseBool(valStr);
                    else if (key == "bIconFurniture") a_target.iconFurniture = ParseBool(valStr);
                    else if (key == "bIconTerminal") a_target.iconTerminal = ParseBool(valStr);
                    else if (key == "bIconNpc") a_target.iconNpc = ParseBool(valStr);
                    else if (key == "sPromptFont") a_target.promptFont = SanitizeFontSetting(valStr);
                    else if (key == "uModernBgColor") a_target.modernBgColor = ParseColorHex(valStr);
                    else if (key == "uModernBorderColor") a_target.modernBorderColor = ParseColorHex(valStr);
                    else if (key == "uModernTextColor") a_target.modernTextColor = ParseColorHex(valStr);
                    else if (key == "uModernNameColor") a_target.modernNameColor = ParseColorHex(valStr);
                    else if (key == "uModernIconColor") a_target.modernIconColor = ParseColorHex(valStr);
                    else if (key == "uModernAccentColor") a_target.modernAccentColor = ParseColorHex(valStr);
                    else if (key == "bHighlightCrimePrompts") a_target.highlightCrimePrompts = ParseBool(valStr);
                    else if (key == "uCrimeTextColor") a_target.crimeTextColor = ParseColorHex(valStr);
                } else if (currentSection == "InfoCard") {
                    if (key == "iInfoCardPosition") a_target.infoCardPosition = std::stoi(valStr);
                    else if (key == "fInfoCardOffsetX") a_target.infoCardOffsetX = std::stof(valStr);
                    else if (key == "fInfoCardOffsetY") a_target.infoCardOffsetY = std::stof(valStr);
                    else if (key == "fInfoCardScale") a_target.infoCardScale = std::stof(valStr);
                    else if (key == "fInfoCardOpacity") a_target.infoCardOpacity = std::stof(valStr);
                    else if (key == "fInfoCardDelaySeconds") a_target.infoCardDelaySeconds = std::stof(valStr);
                    else if (key == "fInfoCardBgOpacity") a_target.infoCardBgOpacity = std::stof(valStr);
                    else if (key == "bInfoCardBorder") a_target.infoCardBorder = ParseBool(valStr);
                    else if (key == "bInfoCardAccentBar") a_target.infoCardAccentBar = ParseBool(valStr);
                    else if (key == "bInfoCardShowIcon") a_target.infoCardShowIcon = ParseBool(valStr);
                    else if (key == "fInfoCardRounding") a_target.infoCardRounding = std::stof(valStr);
                    else if (key == "fInfoCardPad") a_target.infoCardPad = std::stof(valStr);
                    else if (key == "fInfoCardRowGap") a_target.infoCardRowGap = std::stof(valStr);
                    else if (key == "uInfoCardBgColor") a_target.infoCardBgColor = ParseColorHex(valStr);
                    else if (key == "uInfoCardBorderColor") a_target.infoCardBorderColor = ParseColorHex(valStr);
                    else if (key == "uInfoCardAccentColor") a_target.infoCardAccentColor = ParseColorHex(valStr);
                    else if (key == "uInfoCardTitleColor") a_target.infoCardTitleColor = ParseColorHex(valStr);
                    else if (key == "uInfoCardLabelColor") a_target.infoCardLabelColor = ParseColorHex(valStr);
                    else if (key == "uInfoCardValueColor") a_target.infoCardValueColor = ParseColorHex(valStr);
                    else if (key == "bShowScrapComponents") a_target.showScrapComponents = ParseBool(valStr);
                    else if (key == "bShowItemValueWeight") a_target.showItemValueWeight = ParseBool(valStr);
                    else if (key == "bShowWeaponArmorStats") a_target.showWeaponArmorStats = ParseBool(valStr);
                    else if (key == "bShowNpcStats") a_target.showNpcStats = ParseBool(valStr);
                    else if (key == "bInfoCardInQuickContainer") a_target.infoCardInQuickContainer = ParseBool(valStr);
                    else if (key == "iInfoCardConditionDisplay") a_target.infoCardConditionDisplay = std::stoi(valStr);
                    else if (key == "sInfoCardFont") a_target.infoCardFont = SanitizeFontSetting(valStr);
                } else if (currentSection == "QuickContainer") {
                    if (key == "bFloatQuickContainer") a_target.floatQuickContainer = ParseBool(valStr);
                    else if (key == "bModernQuickContainer") a_target.modernQuickContainer = ParseBool(valStr);
                    else if (key == "fQuickContainerSmoothing") a_target.quickContainerSmoothing = std::stof(valStr);
                    else if (key == "fQuickContainerDeadzone") a_target.quickContainerDeadzone = std::stof(valStr);
                    else if (key == "fQuickContainerOffsetX") a_target.quickContainerOffset.x = std::stof(valStr);
                    else if (key == "fQuickContainerOffsetY") a_target.quickContainerOffset.y = std::stof(valStr);
                    else if (key == "fQuickContainerOffsetZ") a_target.quickContainerOffset.z = std::stof(valStr);
                    else if (key == "fQcScale") a_target.qcScale = std::stof(valStr);
                    else if (key == "fQcWidth") a_target.qcWidth = std::stof(valStr);
                    else if (key == "bQcExpandInventoryList") a_target.qcExpandInventoryList = ParseBool(valStr);
                    else if (key == "iQcMaxVisibleRows") a_target.qcMaxVisibleRows = std::stoi(valStr);
                    else if (key == "fQcRowHeightPad") a_target.qcRowHeightPad = std::stof(valStr);
                    else if (key == "fQcHeaderTextScale") a_target.qcHeaderTextScale = std::stof(valStr);
                    else if (key == "fQcItemTextScale") a_target.qcItemTextScale = std::stof(valStr);
                    else if (key == "fQcButtonSize") a_target.qcButtonSize = std::stof(valStr);
                    else if (key == "fQcKeyTextScale") a_target.qcKeyTextScale = std::stof(valStr);
                    else if (key == "fQcButtonTextScale") a_target.qcButtonTextScale = std::stof(valStr);
                    else if (key == "bQcShowItemIcons") a_target.qcShowItemIcons = ParseBool(valStr);
                    else if (key == "sQcFont") a_target.qcFont = SanitizeFontSetting(valStr);
                    else if (key == "iQcSortMode") a_target.qcSortMode = std::stoi(valStr);
                    else if (key == "iQcCategorySecondarySort") a_target.qcCategorySecondarySort = std::stoi(valStr);
                    else if (key == "bQcPinQuestItems") a_target.qcPinQuestItems = ParseBool(valStr);
                    else if (key == "bQcPinLegendary") a_target.qcPinLegendary = ParseBool(valStr);
                    else if (key == "bQcPinTaggedForSearch") a_target.qcPinTaggedForSearch = ParseBool(valStr);
                    else if (key == "bQcPinCaps") a_target.qcPinCaps = ParseBool(valStr);
                    else if (key == "iQcTakeDebounceMs") a_target.qcTakeDebounceMs = std::stoi(valStr);
                    else if (key == "iItemConditionDisplay") a_target.itemConditionDisplay = std::stoi(valStr);
                    else if (key == "sQcCategoryOrder") a_target.qcCategoryOrder = ParseQuickLootCategoryOrder(valStr);
                }
            } catch (...) {
                // Ignore malformed numeric values
            }
        }

        ClampData(a_target);
        return true;
    }

    bool WriteDataToFile(const ConfigData& a_data, const std::filesystem::path& a_path)
    {
        if (a_path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(a_path.parent_path(), ec);
        }

        std::ofstream file(a_path);
        if (!file.is_open()) {
            return false;
        }

        file << "[General]\n";
        file << "bEnabled=" << (a_data.enabled ? 1 : 0) << "\n";
        file << "fSmoothingSpeed=" << a_data.smoothingSpeed << "\n";
        file << "bPowerArmorBypass=" << (a_data.powerArmorBypass ? 1 : 0) << "\n";
        file << "bEnableClamping=" << (a_data.enableClamping ? 1 : 0) << "\n";
        file << "fEdgeMargin=" << a_data.edgeMargin << "\n";
        file << "bLimitCompanionPromptDistance=" << (a_data.limitCompanionPromptDistance ? 1 : 0) << "\n";
        file << "fCompanionMaxDistance=" << a_data.companionMaxDistance << "\n";
        file << "fCompanionCombatMaxDistance=" << a_data.companionCombatMaxDistance << "\n";
        file << "bCompanionDeprioritizeLoot=" << (a_data.companionDeprioritizeLoot ? 1 : 0) << "\n";
        file << "bBlockHiddenCompanionInteractions=" << (a_data.blockHiddenCompanionInteractions ? 1 : 0) << "\n";
        file << "fGrabHoldDelay=" << a_data.grabHoldDelay << "\n";
        file << "iFeaturedItemGraceMs=" << a_data.featuredItemGraceMs << "\n";
        file << "iAimingPromptMode=" << a_data.aimingPromptMode << "\n";
        file << "bLimitNpcPromptDistance=" << (a_data.limitNpcPromptDistance ? 1 : 0) << "\n";
        file << "fNpcMaxDistance=" << a_data.npcMaxDistance << "\n";
        file << "bNpcKeepRangedActions=" << (a_data.npcKeepRangedActions ? 1 : 0) << "\n";
        file << "fNpcRangedMaxDistance=" << a_data.npcRangedMaxDistance << "\n";
        file << "bBlockPromptsThroughWalls=" << (a_data.blockPromptsThroughWalls ? 1 : 0) << "\n\n";

        file << "[ThirdPersonAssist]\n";
        file << "bTpAssistEnabled=" << (a_data.tpAssistEnabled ? 1 : 0) << "\n";
        file << "iTpAssistMode=" << a_data.tpAssistMode << "\n";
        file << "fTpAssistMaxDistance=" << a_data.tpAssistMaxDistance << "\n";
        file << "fTpAssistConeDegrees=" << a_data.tpAssistConeDegrees << "\n";
        file << "iTpAssistLockMs=" << a_data.tpAssistLockMs << "\n";
        file << "bTpAssistItems=" << (a_data.tpAssistItems ? 1 : 0) << "\n";
        file << "bTpAssistDoors=" << (a_data.tpAssistDoors ? 1 : 0) << "\n";
        file << "bTpAssistFurniture=" << (a_data.tpAssistFurniture ? 1 : 0) << "\n";
        file << "bTpAssistContainers=" << (a_data.tpAssistContainers ? 1 : 0) << "\n\n";

        file << "[HoldToActivate]\n";
        file << "bHoldToActivateEnabled=" << (a_data.holdToActivateEnabled ? 1 : 0) << "\n";
        file << "fHoldToActivateDuration=" << a_data.holdToActivateDuration << "\n";
        file << "iHoldRingStyle=" << a_data.holdRingStyle << "\n";
        file << "bHoldDrink=" << (a_data.holdDrink ? 1 : 0) << "\n";
        file << "bHoldSitSleep=" << (a_data.holdSitSleep ? 1 : 0) << "\n";
        file << "bHoldStealPickpocket=" << (a_data.holdStealPickpocket ? 1 : 0) << "\n";
        file << "bHoldEatCannibal=" << (a_data.holdEatCannibal ? 1 : 0) << "\n";
        file << "bHoldCraftPowerArmor=" << (a_data.holdCraftPowerArmor ? 1 : 0) << "\n";
        file << "sHoldCustomVerbs=" << a_data.holdCustomVerbs << "\n\n";

        file << "[Offsets]\n";
        file << "fDoorOffsetX=" << a_data.doorOffset.x << "\n";
        file << "fDoorOffsetY=" << a_data.doorOffset.y << "\n";
        file << "fDoorOffsetZ=" << a_data.doorOffset.z << "\n";
        file << "fNpcOffsetX=" << a_data.npcOffset.x << "\n";
        file << "fNpcOffsetY=" << a_data.npcOffset.y << "\n";
        file << "fNpcOffsetZ=" << a_data.npcOffset.z << "\n";
        file << "fContainerOffsetX=" << a_data.containerOffset.x << "\n";
        file << "fContainerOffsetY=" << a_data.containerOffset.y << "\n";
        file << "fContainerOffsetZ=" << a_data.containerOffset.z << "\n";
        file << "fItemOffsetX=" << a_data.itemOffset.x << "\n";
        file << "fItemOffsetY=" << a_data.itemOffset.y << "\n";
        file << "fItemOffsetZ=" << a_data.itemOffset.z << "\n";
        file << "fFurnitureOffsetX=" << a_data.furnitureOffset.x << "\n";
        file << "fFurnitureOffsetY=" << a_data.furnitureOffset.y << "\n";
        file << "fFurnitureOffsetZ=" << a_data.furnitureOffset.z << "\n\n";

        file << "[Visual]\n";
        file << "iPromptStyle=" << a_data.promptStyle << "\n";
        file << "iButtonIconStyle=" << a_data.buttonIconStyle << "\n";
        file << "iIconLibrary=" << a_data.iconLibrary << "\n";
        file << "bModernShowItemIcon=" << (a_data.modernShowItemIcon ? 1 : 0) << "\n";
        file << "bColoredCategoryIcons=" << (a_data.coloredCategoryIcons ? 1 : 0) << "\n";
        file << "fModernScale=" << a_data.modernScale << "\n";
        file << "fModernOpacity=" << a_data.modernOpacity << "\n";
        file << "bInlineItemInfo=" << (a_data.inlineItemInfo ? 1 : 0) << "\n";
        file << "iModernTextMode=" << a_data.modernTextMode << "\n";
        file << "bShowDoorDestination=" << (a_data.showDoorDestination ? 1 : 0) << "\n";
        file << "bShowInteractableHeader=" << (a_data.showInteractableHeader ? 1 : 0) << "\n";
        file << "bCleanSorterTags=" << (a_data.cleanSorterTags ? 1 : 0) << "\n";
        file << "fModernBgOpacity=" << a_data.modernBgOpacity << "\n";
        file << "bModernBorder=" << (a_data.modernBorder ? 1 : 0) << "\n";
        file << "bModernTextShadow=" << (a_data.modernTextShadow ? 1 : 0) << "\n";
        file << "fModernRounding=" << a_data.modernRounding << "\n";
        file << "fModernPadX=" << a_data.modernPadX << "\n";
        file << "fModernPadY=" << a_data.modernPadY << "\n";
        file << "fModernItemGap=" << a_data.modernItemGap << "\n";
        file << "iMultiActionLayout=" << a_data.multiActionLayout << "\n";
        file << "fModernActionGap=" << a_data.modernActionGap << "\n";
        file << "fModernButtonSize=" << a_data.modernButtonSize << "\n";
        file << "fModernKeyTextScale=" << a_data.modernKeyTextScale << "\n";
        file << "fModernActionTextScale=" << a_data.modernActionTextScale << "\n";
        file << "fModernIconScale=" << a_data.modernIconScale << "\n";
        file << "fModernScreenOffsetX=" << a_data.modernScreenOffsetX << "\n";
        file << "fModernScreenOffsetY=" << a_data.modernScreenOffsetY << "\n";
        file << "bIconWeapon=" << (a_data.iconWeapon ? 1 : 0) << "\n";
        file << "bIconArmor=" << (a_data.iconArmor ? 1 : 0) << "\n";
        file << "bIconAid=" << (a_data.iconAid ? 1 : 0) << "\n";
        file << "bIconAmmo=" << (a_data.iconAmmo ? 1 : 0) << "\n";
        file << "bIconMiscJunk=" << (a_data.iconMiscJunk ? 1 : 0) << "\n";
        file << "bIconKey=" << (a_data.iconKey ? 1 : 0) << "\n";
        file << "bIconBookNote=" << (a_data.iconBookNote ? 1 : 0) << "\n";
        file << "bIconHolotape=" << (a_data.iconHolotape ? 1 : 0) << "\n";
        file << "bIconContainer=" << (a_data.iconContainer ? 1 : 0) << "\n";
        file << "bIconDoor=" << (a_data.iconDoor ? 1 : 0) << "\n";
        file << "bIconFurniture=" << (a_data.iconFurniture ? 1 : 0) << "\n";
        file << "bIconTerminal=" << (a_data.iconTerminal ? 1 : 0) << "\n";
        file << "bIconNpc=" << (a_data.iconNpc ? 1 : 0) << "\n";
        file << "sPromptFont=" << a_data.promptFont << "\n";
        file << "uModernBgColor=" << FormatColorHex(a_data.modernBgColor) << "\n";
        file << "uModernBorderColor=" << FormatColorHex(a_data.modernBorderColor) << "\n";
        file << "uModernTextColor=" << FormatColorHex(a_data.modernTextColor) << "\n";
        file << "uModernNameColor=" << FormatColorHex(a_data.modernNameColor) << "\n";
        file << "uModernIconColor=" << FormatColorHex(a_data.modernIconColor) << "\n";
        file << "uModernAccentColor=" << FormatColorHex(a_data.modernAccentColor) << "\n";
        file << "bHighlightCrimePrompts=" << (a_data.highlightCrimePrompts ? 1 : 0) << "\n";
        file << "uCrimeTextColor=" << FormatColorHex(a_data.crimeTextColor) << "\n\n";

        file << "[InfoCard]\n";
        file << "iInfoCardPosition=" << a_data.infoCardPosition << "\n";
        file << "fInfoCardOffsetX=" << a_data.infoCardOffsetX << "\n";
        file << "fInfoCardOffsetY=" << a_data.infoCardOffsetY << "\n";
        file << "fInfoCardScale=" << a_data.infoCardScale << "\n";
        file << "fInfoCardOpacity=" << a_data.infoCardOpacity << "\n";
        file << "fInfoCardDelaySeconds=" << a_data.infoCardDelaySeconds << "\n";
        file << "fInfoCardBgOpacity=" << a_data.infoCardBgOpacity << "\n";
        file << "bInfoCardBorder=" << (a_data.infoCardBorder ? 1 : 0) << "\n";
        file << "bInfoCardAccentBar=" << (a_data.infoCardAccentBar ? 1 : 0) << "\n";
        file << "bInfoCardShowIcon=" << (a_data.infoCardShowIcon ? 1 : 0) << "\n";
        file << "fInfoCardRounding=" << a_data.infoCardRounding << "\n";
        file << "fInfoCardPad=" << a_data.infoCardPad << "\n";
        file << "fInfoCardRowGap=" << a_data.infoCardRowGap << "\n";
        file << "uInfoCardBgColor=" << FormatColorHex(a_data.infoCardBgColor) << "\n";
        file << "uInfoCardBorderColor=" << FormatColorHex(a_data.infoCardBorderColor) << "\n";
        file << "uInfoCardAccentColor=" << FormatColorHex(a_data.infoCardAccentColor) << "\n";
        file << "uInfoCardTitleColor=" << FormatColorHex(a_data.infoCardTitleColor) << "\n";
        file << "uInfoCardLabelColor=" << FormatColorHex(a_data.infoCardLabelColor) << "\n";
        file << "uInfoCardValueColor=" << FormatColorHex(a_data.infoCardValueColor) << "\n";
        file << "bShowScrapComponents=" << (a_data.showScrapComponents ? 1 : 0) << "\n";
        file << "bShowItemValueWeight=" << (a_data.showItemValueWeight ? 1 : 0) << "\n";
        file << "bShowWeaponArmorStats=" << (a_data.showWeaponArmorStats ? 1 : 0) << "\n";
        file << "bShowNpcStats=" << (a_data.showNpcStats ? 1 : 0) << "\n";
        file << "bInfoCardInQuickContainer=" << (a_data.infoCardInQuickContainer ? 1 : 0) << "\n";
        file << "iInfoCardConditionDisplay=" << a_data.infoCardConditionDisplay << "\n";
        file << "sInfoCardFont=" << a_data.infoCardFont << "\n\n";

        file << "[QuickContainer]\n";
        file << "bFloatQuickContainer=" << (a_data.floatQuickContainer ? 1 : 0) << "\n";
        file << "bModernQuickContainer=" << (a_data.modernQuickContainer ? 1 : 0) << "\n";
        file << "fQuickContainerSmoothing=" << a_data.quickContainerSmoothing << "\n";
        file << "fQuickContainerDeadzone=" << a_data.quickContainerDeadzone << "\n";
        file << "fQuickContainerOffsetX=" << a_data.quickContainerOffset.x << "\n";
        file << "fQuickContainerOffsetY=" << a_data.quickContainerOffset.y << "\n";
        file << "fQuickContainerOffsetZ=" << a_data.quickContainerOffset.z << "\n";
        file << "fQcScale=" << a_data.qcScale << "\n";
        file << "fQcWidth=" << a_data.qcWidth << "\n";
        file << "bQcExpandInventoryList=" << (a_data.qcExpandInventoryList ? 1 : 0) << "\n";
        file << "iQcMaxVisibleRows=" << a_data.qcMaxVisibleRows << "\n";
        file << "fQcRowHeightPad=" << a_data.qcRowHeightPad << "\n";
        file << "fQcHeaderTextScale=" << a_data.qcHeaderTextScale << "\n";
        file << "fQcItemTextScale=" << a_data.qcItemTextScale << "\n";
        file << "fQcButtonSize=" << a_data.qcButtonSize << "\n";
        file << "fQcKeyTextScale=" << a_data.qcKeyTextScale << "\n";
        file << "fQcButtonTextScale=" << a_data.qcButtonTextScale << "\n";
        file << "bQcShowItemIcons=" << (a_data.qcShowItemIcons ? 1 : 0) << "\n";
        file << "sQcFont=" << a_data.qcFont << "\n";
        file << "iQcSortMode=" << a_data.qcSortMode << "\n";
        file << "iQcCategorySecondarySort=" << a_data.qcCategorySecondarySort << "\n";
        file << "bQcPinQuestItems=" << (a_data.qcPinQuestItems ? 1 : 0) << "\n";
        file << "bQcPinLegendary=" << (a_data.qcPinLegendary ? 1 : 0) << "\n";
        file << "bQcPinTaggedForSearch=" << (a_data.qcPinTaggedForSearch ? 1 : 0) << "\n";
        file << "bQcPinCaps=" << (a_data.qcPinCaps ? 1 : 0) << "\n";
        file << "iQcTakeDebounceMs=" << a_data.qcTakeDebounceMs << "\n";
        file << "iItemConditionDisplay=" << a_data.itemConditionDisplay << "\n";
        file << "sQcCategoryOrder=" << FormatQuickLootCategoryOrder(a_data.qcCategoryOrder) << "\n";

        return file.good();
    }
}

const char* GetQuickLootCategoryKey(int a_catIndex) noexcept
{
    if (a_catIndex < 0 || a_catIndex >= static_cast<int>(kQuickLootCategoryCount)) {
        return "Misc";
    }
    return kQuickLootCategoryKeys[static_cast<std::size_t>(a_catIndex)];
}

const char* GetQuickLootCategoryLabel(int a_catIndex) noexcept
{
    if (a_catIndex < 0 || a_catIndex >= static_cast<int>(kQuickLootCategoryCount)) {
        return "Misc";
    }
    return kQuickLootCategoryLabels[static_cast<std::size_t>(a_catIndex)];
}

std::array<int, kQuickLootCategoryCount> DefaultQuickLootCategoryOrder() noexcept
{
    return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 };
}

void SanitizeQuickLootCategoryOrder(std::array<int, kQuickLootCategoryCount>& a_order) noexcept
{
    std::array<bool, kQuickLootCategoryCount> seen{};
    std::array<int, kQuickLootCategoryCount> cleaned{};
    std::size_t outIdx = 0;

    for (const int cat : a_order) {
        if (cat >= 0 && cat < static_cast<int>(kQuickLootCategoryCount) &&
            !seen[static_cast<std::size_t>(cat)]) {
            seen[static_cast<std::size_t>(cat)] = true;
            cleaned[outIdx++] = cat;
        }
    }

    for (int cat = 0; cat < static_cast<int>(kQuickLootCategoryCount); ++cat) {
        if (!seen[static_cast<std::size_t>(cat)]) {
            cleaned[outIdx++] = cat;
        }
    }

    a_order = cleaned;
}

std::array<int, kQuickLootCategoryCount> ParseQuickLootCategoryOrder(std::string_view a_csv) noexcept
{
    std::array<int, kQuickLootCategoryCount> parsed{};
    parsed.fill(-1);
    std::array<bool, kQuickLootCategoryCount> seen{};
    std::size_t outIdx = 0;

    auto trimToken = [](std::string_view a_tok) noexcept -> std::string_view {
        const std::size_t first = a_tok.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        const std::size_t last = a_tok.find_last_not_of(" \t\r\n");
        return a_tok.substr(first, last - first + 1);
    };

    bool hasExplicitArmorSub = false;
    for (std::size_t pos = 0; pos <= a_csv.size();) {
        const std::size_t comma = a_csv.find(',', pos);
        const std::string_view token = trimToken(
            (comma == std::string_view::npos) ? a_csv.substr(pos) : a_csv.substr(pos, comma - pos));
        if (EqualsIgnoreCaseAscii(token, "PowerArmor") || EqualsIgnoreCaseAscii(token, "Clothing")) {
            hasExplicitArmorSub = true;
            break;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }

    auto pushCat = [&](int a_cat) noexcept {
        if (outIdx < kQuickLootCategoryCount &&
            a_cat >= 0 &&
            a_cat < static_cast<int>(kQuickLootCategoryCount) &&
            !seen[static_cast<std::size_t>(a_cat)]) {
            seen[static_cast<std::size_t>(a_cat)] = true;
            parsed[outIdx++] = a_cat;
        }
    };

    std::size_t pos = 0;
    while (pos <= a_csv.size() && outIdx < kQuickLootCategoryCount) {
        const std::size_t comma = a_csv.find(',', pos);
        const std::string_view token = trimToken(
            (comma == std::string_view::npos) ? a_csv.substr(pos) : a_csv.substr(pos, comma - pos));

        if (!token.empty()) {
            if (EqualsIgnoreCaseAscii(token, "Aid")) {
                pushCat(static_cast<int>(QuickLootCategory::Medicine));
                pushCat(static_cast<int>(QuickLootCategory::Chem));
                pushCat(static_cast<int>(QuickLootCategory::Drink));
                pushCat(static_cast<int>(QuickLootCategory::Food));
            } else if (EqualsIgnoreCaseAscii(token, "Weapon")) {
                pushCat(static_cast<int>(QuickLootCategory::Explosive));
                pushCat(static_cast<int>(QuickLootCategory::Pistol));
                pushCat(static_cast<int>(QuickLootCategory::Rifle));
                pushCat(static_cast<int>(QuickLootCategory::HeavyWeapon));
                pushCat(static_cast<int>(QuickLootCategory::Melee));
            } else if (!hasExplicitArmorSub && EqualsIgnoreCaseAscii(token, "Armor")) {
                pushCat(static_cast<int>(QuickLootCategory::PowerArmor));
                pushCat(static_cast<int>(QuickLootCategory::Armor));
                pushCat(static_cast<int>(QuickLootCategory::Clothing));
            } else {
                int matchedCat = -1;
                for (int c = 0; c < static_cast<int>(kQuickLootCategoryCount); ++c) {
                    if (EqualsIgnoreCaseAscii(token, kQuickLootCategoryKeys[static_cast<std::size_t>(c)])) {
                        matchedCat = c;
                        break;
                    }
                }
                if (matchedCat < 0 && (token.size() == 1 || token.size() == 2)) {
                    bool allDigits = true;
                    int val = 0;
                    for (const char ch : token) {
                        if (ch < '0' || ch > '9') {
                            allDigits = false;
                            break;
                        }
                        val = val * 10 + (ch - '0');
                    }
                    if (allDigits && val >= 0 && val < static_cast<int>(kQuickLootCategoryCount)) {
                        matchedCat = val;
                    }
                }
                pushCat(matchedCat);
            }
        }

        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }

    SanitizeQuickLootCategoryOrder(parsed);
    return parsed;
}

std::string FormatQuickLootCategoryOrder(const std::array<int, kQuickLootCategoryCount>& a_order)
{
    auto clean = a_order;
    SanitizeQuickLootCategoryOrder(clean);
    std::string out;
    for (std::size_t i = 0; i < kQuickLootCategoryCount; ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out.append(GetQuickLootCategoryKey(clean[i]));
    }
    return out;
}

ConfigData Config::GetSnapshot() const
{
    std::lock_guard lock{ _mutex };
    return _data;
}

void Config::ApplySnapshot(const ConfigData& a_data, bool a_markDirty)
{
    ConfigData clamped = a_data;
    ClampData(clamped);

    {
        std::lock_guard lock{ _mutex };
        _data = clamped;
    }

    if (a_markDirty) {
        _hasUnsavedChanges.store(true, std::memory_order_release);
    }
}

bool Config::HasUnsavedChanges() const noexcept
{
    return _hasUnsavedChanges.load(std::memory_order_acquire);
}

void Config::ClearUnsavedChanges() noexcept
{
    _hasUnsavedChanges.store(false, std::memory_order_release);
}

void Config::ResetDefaults()
{
    std::lock_guard lock{ _mutex };
    _data = ConfigData{};
    _hasUnsavedChanges.store(false, std::memory_order_release);
}

bool Config::Load(const std::filesystem::path& a_path)
{
    ConfigData next = GetSnapshot();
    if (!ParseFileInto(a_path, next)) {
        return false;
    }

    {
        std::lock_guard lock{ _mutex };
        _data = next;
    }
    return true;
}

bool Config::Save(const std::filesystem::path& a_path) const
{
    const ConfigData snapshot = GetSnapshot();
    return WriteDataToFile(snapshot, a_path);
}

void Config::LoadFromPaths(
    const std::filesystem::path& a_defaultPath,
    const std::filesystem::path& a_pluginPath,
    const std::filesystem::path& a_mcmUserPath)
{
    ConfigData merged{};
    (void)ParseFileInto(a_defaultPath, merged);
    (void)ParseFileInto(a_pluginPath, merged);
    (void)ParseFileInto(a_mcmUserPath, merged);
    ClampData(merged);

    {
        std::lock_guard lock{ _mutex };
        _data = merged;
    }
    _hasUnsavedChanges.store(false, std::memory_order_release);
}

void Config::LoadAll()
{
    LoadFromPaths(kDefaultIniPath, kPluginIniPath, kMcmUserIniPath);
}

bool Config::SaveUserTo(const std::filesystem::path& a_userPath)
{
    const ConfigData snapshot = GetSnapshot();
    if (!WriteDataToFile(snapshot, a_userPath)) {
        return false;
    }
    _hasUnsavedChanges.store(false, std::memory_order_release);
    return true;
}

bool Config::SaveUser()
{
    return SaveUserTo(kMcmUserIniPath);
}

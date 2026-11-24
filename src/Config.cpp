#include "Config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>

namespace
{
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
        file << "bCompanionDeprioritizeLoot=" << (a_data.companionDeprioritizeLoot ? 1 : 0) << "\n\n";

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
        file << "uModernAccentColor=" << FormatColorHex(a_data.modernAccentColor) << "\n\n";

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

        return file.good();
    }
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

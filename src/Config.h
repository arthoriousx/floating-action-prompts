#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

struct Vector3
{
    float x{ 0.0f };
    float y{ 0.0f };
    float z{ 0.0f };
};

struct ConfigData
{
    // General toggles
    bool enabled{ true };
    float smoothingSpeed{ 0.25f };
    bool powerArmorBypass{ true };
    bool enableClamping{ true };
    float edgeMargin{ 40.0f };

    // Context-aware 3D Offsets (world units)
    Vector3 doorOffset{ 0.0f, 0.0f, -20.0f };
    Vector3 npcOffset{ 0.0f, 0.0f, 10.0f };
    Vector3 containerOffset{ 0.0f, 0.0f, 15.0f };
    Vector3 itemOffset{ 0.0f, 0.0f, -8.0f };
    Vector3 furnitureOffset{ 0.0f, 0.0f, 10.0f };

    // Visual style ([Visual])
    int promptStyle{ 1 };            // 0 = Vanilla, 1 = Modern (GTA 6)
    int buttonIconStyle{ 0 };        // 0 = Keyboard (E/R), 1 = PS Colored, 2 = Xbox Colored, 3 = PS Monochrome, 4 = Xbox Monochrome
    int iconLibrary{ 1 };            // 0 = FontAwesome, 1 = FallUI Icon Library
    bool modernShowItemIcon{ true };
    bool coloredCategoryIcons{ false };
    float modernScale{ 0.85f };
    float modernOpacity{ 0.85f };
    bool inlineItemInfo{ false };
    int modernTextMode{ 0 };         // 0 = Action Only, 1 = Action + Name, 2 = Name on World Objects Only
    bool showDoorDestination{ true };
    bool cleanSorterTags{ true };
    float modernBgOpacity{ 0.0f };
    bool modernBorder{ false };
    bool modernTextShadow{ true };
    float modernRounding{ 6.0f };
    float modernPadX{ 8.0f };
    float modernPadY{ 4.0f };
    float modernItemGap{ 7.0f };
    int multiActionLayout{ 0 };      // 0 = Stacked Vertical (Modern), 1 = Inline Horizontal
    float modernActionGap{ 4.0f };
    float modernButtonSize{ 0.70f };
    float modernKeyTextScale{ 0.80f };
    float modernActionTextScale{ 0.85f };
    float modernIconScale{ 0.70f };
    float modernScreenOffsetX{ 10.0f };
    float modernScreenOffsetY{ 0.0f };
    bool iconWeapon{ true };
    bool iconArmor{ true };
    bool iconAid{ true };
    bool iconAmmo{ true };
    bool iconMiscJunk{ true };
    bool iconKey{ true };
    bool iconBookNote{ true };
    bool iconHolotape{ true };
    bool iconContainer{ true };
    bool iconDoor{ false };
    bool iconFurniture{ false };
    bool iconTerminal{ true };
    bool iconNpc{ false };
    std::string promptFont{};
    std::uint32_t modernBgColor{ 0x0E1116u };
    std::uint32_t modernBorderColor{ 0xFFFFFFu };
    std::uint32_t modernTextColor{ 0xFFFFFFu };
    std::uint32_t modernNameColor{ 0xE0E8F0u };
    std::uint32_t modernIconColor{ 0xFFFFFFu };
    std::uint32_t modernAccentColor{ 0xF6C86Eu };

    // Smooth Mini Info Card ([InfoCard])
    int infoCardPosition{ 3 };       // 0 = Off, 1 = Below, 2 = Above, 3 = Right, 4 = Left, 5 = Fixed Top-Right, 6 = Fixed Bottom-Right
    float infoCardOffsetX{ 16.0f };
    float infoCardOffsetY{ 0.0f };
    float infoCardScale{ 0.75f };
    float infoCardOpacity{ 0.90f };
    float infoCardDelaySeconds{ 1.0f };
    float infoCardBgOpacity{ 0.70f };
    bool infoCardBorder{ true };
    bool infoCardAccentBar{ false };
    bool infoCardShowIcon{ true };
    float infoCardRounding{ 0.0f };
    float infoCardPad{ 12.0f };
    float infoCardRowGap{ 2.0f };
    std::string infoCardFont{};
    std::uint32_t infoCardBgColor{ 0x0E1116u };
    std::uint32_t infoCardBorderColor{ 0xFFFFFFu };
    std::uint32_t infoCardAccentColor{ 0xF6C86Eu };
    std::uint32_t infoCardTitleColor{ 0xFFFFFFu };
    std::uint32_t infoCardLabelColor{ 0xA8B2C0u };
    std::uint32_t infoCardValueColor{ 0xF0F5FAu };
    bool showScrapComponents{ true };
    bool showItemValueWeight{ true };
    bool showWeaponArmorStats{ true };
    bool showNpcStats{ true };
    bool infoCardInQuickContainer{ true };

    // Floating QuickContainer ([QuickContainer])
    bool floatQuickContainer{ true };
    bool modernQuickContainer{ true };
    float quickContainerSmoothing{ 0.30f };
    float quickContainerDeadzone{ 6.0f };
    Vector3 quickContainerOffset{ 0.0f, 0.0f, 15.0f };
    float qcScale{ 0.80f };
    float qcWidth{ 300.0f };         // 0 = Auto, >0 = Fixed Width (px)
    bool qcExpandInventoryList{ true };
    int qcMaxVisibleRows{ 12 };
    float qcRowHeightPad{ 4.0f };
    float qcHeaderTextScale{ 1.0f };
    float qcItemTextScale{ 1.0f };
    float qcButtonSize{ 0.85f };
    float qcKeyTextScale{ 0.75f };
    float qcButtonTextScale{ 0.95f };
    bool qcShowItemIcons{ true };
    std::string qcFont{};
};

class Config
{
public:
    static constexpr const char* kDefaultIniPath = "Data/MCM/Config/FloatingActionPrompts/settings.ini";
    static constexpr const char* kPluginIniPath = "Data/F4SE/Plugins/FloatingActionPrompts.ini";
    static constexpr const char* kMcmUserIniPath = "Data/MCM/Settings/FloatingActionPrompts.ini";

    static Config& GetSingleton()
    {
        static Config instance;
        return instance;
    }

    [[nodiscard]] ConfigData GetSnapshot() const;
    void ApplySnapshot(const ConfigData& a_data, bool a_markDirty = true);

    [[nodiscard]] bool HasUnsavedChanges() const noexcept;
    void ClearUnsavedChanges() noexcept;

    void ResetDefaults();
    bool Load(const std::filesystem::path& a_path);
    bool Save(const std::filesystem::path& a_path) const;

    void LoadFromPaths(
        const std::filesystem::path& a_defaultPath,
        const std::filesystem::path& a_pluginPath,
        const std::filesystem::path& a_mcmUserPath);
    void LoadAll();

    bool SaveUserTo(const std::filesystem::path& a_userPath);
    bool SaveUser();

private:
    Config() = default;

    mutable std::mutex _mutex;
    ConfigData _data{};
    std::atomic<bool> _hasUnsavedChanges{ false };
};

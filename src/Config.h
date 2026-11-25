#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

struct Vector3
{
    float x{ 0.0f };
    float y{ 0.0f };
    float z{ 0.0f };
};

inline constexpr std::size_t kQuickLootCategoryCount = 19;

enum class QuickLootCategory : std::uint8_t
{
    Key = 0,
    Medicine = 1,
    Chem = 2,
    Drink = 3,
    Food = 4,
    Ammo = 5,
    Explosive = 6,
    Pistol = 7,
    Rifle = 8,
    HeavyWeapon = 9,
    Melee = 10,
    PowerArmor = 11,
    Armor = 12,
    Clothing = 13,
    Mod = 14,
    BookNote = 15,
    Holotape = 16,
    Misc = 17,
    Junk = 18
};

[[nodiscard]] const char* GetQuickLootCategoryKey(int a_catIndex) noexcept;
[[nodiscard]] const char* GetQuickLootCategoryLabel(int a_catIndex) noexcept;
[[nodiscard]] std::array<int, kQuickLootCategoryCount> DefaultQuickLootCategoryOrder() noexcept;
void SanitizeQuickLootCategoryOrder(std::array<int, kQuickLootCategoryCount>& a_order) noexcept;
[[nodiscard]] std::array<int, kQuickLootCategoryCount> ParseQuickLootCategoryOrder(std::string_view a_csv) noexcept;
[[nodiscard]] std::string FormatQuickLootCategoryOrder(const std::array<int, kQuickLootCategoryCount>& a_order);

struct ConfigData
{
    // General toggles
    bool enabled{ true };
    float smoothingSpeed{ 0.25f };
    bool powerArmorBypass{ false };
    bool enableClamping{ true };
    float edgeMargin{ 40.0f };

    // Companion prompt distance & focus protection ([General])
    bool limitCompanionPromptDistance{ true };
    float companionMaxDistance{ 120.0f };
    float companionCombatMaxDistance{ 80.0f };
    bool companionDeprioritizeLoot{ true };
    bool blockHiddenCompanionInteractions{ true };
    float grabHoldDelay{ 1.50f };

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
    bool highlightCrimePrompts{ true };
    std::uint32_t crimeTextColor{ 0xFF4D4Du };

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
    int qcSortMode{ 0 };              // 0=Category Priority, 1=Value Desc, 2=Value/Weight Desc, 3=Alpha A-Z, 4=Weight Asc, 5=Weight Desc, 6=Vanilla Passive Mirror
    int qcCategorySecondarySort{ 0 }; // 0=Value Desc, 1=Value/Weight Desc, 2=Alpha A-Z, 3=Weight Asc
    bool qcPinQuestItems{ true };
    bool qcPinLegendary{ true };
    bool qcPinTaggedForSearch{ true };
    bool qcPinCaps{ true };
    int qcTakeDebounceMs{ 0 };        // 0..300 ms post-release anti-chatter debounce
    std::array<int, kQuickLootCategoryCount> qcCategoryOrder{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 };
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

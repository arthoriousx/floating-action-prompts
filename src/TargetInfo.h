#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "AnchorManager.h"
#include "Config.h"

namespace TargetInfo
{
    enum class ItemKind : std::uint8_t
    {
        None = 0,
        MiscJunk,
        Weapon,
        Armor,
        Aid,
        Ammo,
        Key,
        BookNote,
        Holotape,
        Container,
        Door,
        Furniture,
        Terminal,
        Npc
    };

    struct ScrapComponent
    {
        std::string name;
        std::uint32_t count{ 0 };
    };

    struct CardRow
    {
        std::string label;
        std::string value;
    };

    struct TargetMetadata
    {
        std::uint32_t formID{ 0 };
        AnchorManager::TargetCategory category{ AnchorManager::TargetCategory::kItem };
        ItemKind itemKind{ ItemKind::None };

        std::string rawDisplayName;
        std::string displayName;
        std::string typeLabel;
        std::string actionVerb;
        std::string secondaryVerb;

        // Loose item properties
        bool hasItemStats{ false };
        std::uint32_t value{ 0 };
        float weight{ 0.0f };
        std::vector<ScrapComponent> scrapComponents;

        // Weapon stats
        bool isWeapon{ false };
        std::uint32_t damage{ 0 };
        std::string ammoName;

        // Armor stats
        bool isArmor{ false };
        std::uint32_t armorRating{ 0 };

        // NPC stats
        bool isNpc{ false };
        std::string equippedWeapon;
        float health{ 0.0f };
        float maxHealth{ 0.0f };
        float actionPoints{ 0.0f };
        float maxActionPoints{ 0.0f };
        float damageResist{ 0.0f };
        float energyResist{ 0.0f };
    };

    struct ActionParts
    {
        std::string keyLabel;
        std::string verb;
    };

    [[nodiscard]] std::string ToUpperAscii(const std::string& a_input);
    [[nodiscard]] std::string CleanDisplayName(const std::string& a_raw, bool a_stripSorterTags);
    [[nodiscard]] std::string ResolveTranslationFallback(std::string_view a_raw);
    [[nodiscard]] ActionParts SplitActivateText(const std::string& a_rawActionLine);
    [[nodiscard]] std::string FormatInlineSummary(const TargetMetadata& a_meta, const ConfigData& a_cfg);
    [[nodiscard]] std::vector<CardRow> BuildInfoCardRows(const TargetMetadata& a_meta, const ConfigData& a_cfg);

#ifndef FAP_UNIT_TEST
    [[nodiscard]] ItemKind ClassifyBoundObject(const RE::TESBoundObject* a_baseObj);
    [[nodiscard]] TargetMetadata ExtractTargetMetadata(RE::TESObjectREFR* a_targetRef);
    [[nodiscard]] TargetMetadata ExtractInventoryItemMetadata(RE::TESBoundObject* a_baseObj, RE::ExtraDataList* a_extraList);
#endif
}

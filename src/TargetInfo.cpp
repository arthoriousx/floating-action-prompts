#include "TargetInfo.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

namespace TargetInfo
{
    namespace
    {
        [[nodiscard]] std::string Trim(std::string_view a_sv)
        {
            const auto first = a_sv.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = a_sv.find_last_not_of(" \t\r\n");
            return std::string(a_sv.substr(first, last - first + 1));
        }

        [[nodiscard]] std::string StripSimpleHtml(std::string_view a_text)
        {
            std::string out;
            out.reserve(a_text.size());
            for (std::size_t i = 0; i < a_text.size(); ++i) {
                if (a_text[i] == '<' && (i + 1) < a_text.size()) {
                    const char next = a_text[i + 1];
                    const bool isTagStart =
                        next == '/' ||
                        (next >= 'a' && next <= 'z') ||
                        (next >= 'A' && next <= 'Z');
                    if (isTagStart) {
                        std::size_t closePos = std::string_view::npos;
                        for (std::size_t j = i + 2; j < a_text.size(); ++j) {
                            if (a_text[j] == '<') {
                                break;
                            }
                            if (a_text[j] == '>') {
                                closePos = j;
                                break;
                            }
                        }
                        if (closePos != std::string_view::npos) {
                            i = closePos;
                            continue;
                        }
                    }
                }
                out.push_back(a_text[i]);
            }
            return out;
        }

        [[nodiscard]] std::string FormatFloatCompact(float a_val)
        {
            if (std::isnan(a_val) || std::isinf(a_val)) {
                return "0";
            }
            const float rounded = std::round(a_val * 100.0f) / 100.0f;
            if (std::fabs(rounded - std::round(rounded)) < 0.005f) {
                return std::to_string(static_cast<long long>(std::llround(rounded)));
            }
            char buf[32]{};
            std::snprintf(buf, sizeof(buf), "%.2f", rounded);
            std::string s(buf);
            while (!s.empty() && s.back() == '0') {
                s.pop_back();
            }
            if (!s.empty() && s.back() == '.') {
                s.pop_back();
            }
            return s;
        }

        [[nodiscard]] std::string FormatComponentsList(const std::vector<ScrapComponent>& a_comps, std::size_t a_maxItems)
        {
            if (a_comps.empty()) {
                return {};
            }
            std::string out;
            const std::size_t limit = (a_maxItems == 0) ? a_comps.size() : std::min(a_maxItems, a_comps.size());
            for (std::size_t i = 0; i < limit; ++i) {
                if (i > 0) {
                    out += ", ";
                }
                out += std::to_string(a_comps[i].count) + "x " + a_comps[i].name;
            }
            if (limit < a_comps.size()) {
                out += " +" + std::to_string(a_comps.size() - limit);
            }
            return out;
        }

        void AppendPart(std::string& a_dst, std::string_view a_part)
        {
            if (a_part.empty()) {
                return;
            }
            if (!a_dst.empty()) {
                a_dst += "  |  ";
            }
            a_dst.append(a_part);
        }
    }

    std::string ToUpperAscii(const std::string& a_input)
    {
        std::string out = a_input;
        for (char& ch : out) {
            if (ch >= 'a' && ch <= 'z') {
                ch = static_cast<char>(ch - ('a' - 'A'));
            }
        }
        return out;
    }

    std::string CleanDisplayName(const std::string& a_raw, bool a_stripSorterTags)
    {
        const std::string original = Trim(StripSimpleHtml(a_raw));
        if (!a_stripSorterTags || original.empty()) {
            return original;
        }

        // 1. Strip any "{...}" or "{{...}}" component/tag blocks using brace depth
        std::string s;
        s.reserve(original.size());
        int braceDepth = 0;
        for (const char ch : original) {
            if (ch == '{') {
                ++braceDepth;
                continue;
            }
            if (ch == '}') {
                if (braceDepth > 0) {
                    --braceDepth;
                }
                continue;
            }
            if (braceDepth == 0) {
                s.push_back(ch);
            }
        }
        s = Trim(s);

        // 2. Strip leading FIS/VIS prefix tags like "[Scrap] " / "[[Scrap]] " / "|Misc| "
        // Keep leading parentheses intact so "(Mk II) Combat Armor" or "(Empty) Bottle" is preserved.
        while (!s.empty()) {
            std::size_t closePos = std::string::npos;
            if (s.front() == '[') {
                int bracketDepth = 0;
                const std::size_t limit = std::min<std::size_t>(s.size(), 32);
                for (std::size_t i = 0; i < limit; ++i) {
                    if (s[i] == '[') {
                        ++bracketDepth;
                    } else if (s[i] == ']') {
                        if (--bracketDepth == 0) {
                            closePos = i;
                            break;
                        }
                    }
                }
            } else if (s.front() == '|') {
                const auto pos = s.find('|', 1);
                if (pos != std::string::npos && pos < 32) {
                    closePos = pos;
                }
            } else {
                break;
            }

            if (closePos == std::string::npos) {
                break;
            }

            const std::string remainder = Trim(std::string_view(s).substr(closePos + 1));
            if (remainder.empty()) {
                break;
            }
            s = remainder;
        }

        // 3. Collapse multiple spaces cleanly
        std::string collapsed;
        collapsed.reserve(s.size());
        bool prevSpace = false;
        for (const char ch : s) {
            if (ch == ' ' || ch == '\t') {
                if (!prevSpace) {
                    collapsed.push_back(' ');
                    prevSpace = true;
                }
            } else {
                collapsed.push_back(ch);
                prevSpace = false;
            }
        }

        collapsed = Trim(collapsed);
        return collapsed.empty() ? original : collapsed;
    }

    std::string ResolveTranslationFallback(std::string_view a_raw)
    {
        std::string clean = Trim(a_raw);
        if (clean.empty() || clean.front() != '$') {
            return clean;
        }

        const std::string key = ToUpperAscii(clean);
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 38> kFallbackTable{ {
            { "$TAKE", "TAKE" },
            { "$STEAL", "STEAL" },
            { "$CRAFT", "CRAFT" },
            { "$WORKSHOP", "WORKSHOP" },
            { "$QUICKCONTAINERTRANSFER", "TRANSFER" },
            { "$TRANSFER", "TRANSFER" },
            { "$STORE", "STORE" },
            { "$TAKEALL", "TAKE ALL" },
            { "$UNLOCK", "UNLOCK" },
            { "$OPEN", "OPEN" },
            { "$CLOSE", "CLOSE" },
            { "$SEARCH", "SEARCH" },
            { "$USE", "USE" },
            { "$ACTIVATE", "ACTIVATE" },
            { "$TALK", "TALK" },
            { "$TRADE", "TRADE" },
            { "$DRINK", "DRINK" },
            { "$SLEEP", "SLEEP" },
            { "$SIT", "SIT" },
            { "$PICKPOCKET", "PICKPOCKET" },
            { "$PLAY", "PLAY" },
            { "$READ", "READ" },
            { "$EQUIP", "EQUIP" },
            { "$COMMAND", "COMMAND" },
            { "$DISMISS", "DISMISS" },
            { "$INSPECT", "INSPECT" },
            { "$SCRAP", "SCRAP" },
            { "$RENAME", "RENAME" },
            { "$MODIFY", "MODIFY" },
            { "$PLACE", "PLACE" },
            { "$ENTER", "ENTER" },
            { "$EXIT", "EXIT" },
            { "$HARVEST", "HARVEST" },
            { "$COOK", "COOK" },
            { "$BUILD", "BUILD" },
            { "$REPAIR", "REPAIR" },
            { "$MANAGE", "MANAGE" },
            { "$LOOT", "LOOT" }
        } };

        for (const auto& [k, val] : kFallbackTable) {
            if (key == k) {
                return std::string(val);
            }
        }

        std::string stripped = key.substr(1);
        static constexpr std::string_view kQcPrefix = "QUICKCONTAINER";
        if (stripped.size() > kQcPrefix.size() && stripped.rfind(kQcPrefix, 0) == 0) {
            stripped = stripped.substr(kQcPrefix.size());
        }
        std::replace(stripped.begin(), stripped.end(), '_', ' ');
        stripped = Trim(stripped);
        return stripped.empty() ? clean : stripped;
    }

    std::string ResolveLoadDoorDestination(
        std::string_view a_rolloverName,
        std::string_view a_doorBaseName,
        std::string_view a_teleportCellName,
        std::string_view a_teleportWorldName)
    {
        auto cleanFirstLine = [](std::string_view a_raw) -> std::string {
            const std::string stripped = Trim(StripSimpleHtml(a_raw));
            if (stripped.empty()) {
                return {};
            }
            std::size_t start = 0;
            while (start < stripped.size()) {
                const auto nl = stripped.find_first_of("\r\n", start);
                const std::string_view line = (nl == std::string_view::npos)
                    ? std::string_view(stripped).substr(start)
                    : std::string_view(stripped).substr(start, nl - start);
                std::string cleaned = Trim(line);
                if (!cleaned.empty()) {
                    return cleaned;
                }
                if (nl == std::string::npos) {
                    break;
                }
                start = nl + 1;
            }
            return {};
        };

        const std::string rolloverFirst = cleanFirstLine(a_rolloverName);
        const std::string baseClean = cleanFirstLine(a_doorBaseName);
        const std::string cellClean = cleanFirstLine(a_teleportCellName);
        const std::string worldClean = cleanFirstLine(a_teleportWorldName);

        const std::string rolloverUpper = ToUpperAscii(rolloverFirst);
        const std::string baseUpper = ToUpperAscii(baseClean);

        if (!rolloverFirst.empty() &&
            rolloverUpper != "DOOR" &&
            (baseUpper.empty() || rolloverUpper != baseUpper)) {
            return rolloverFirst;
        }
        if (!cellClean.empty()) {
            return cellClean;
        }
        if (!worldClean.empty()) {
            return worldClean;
        }
        return {};
    }

    ActionParts SplitActivateText(const std::string& a_rawActionLine)
    {
        ActionParts parts;
        std::string clean = Trim(StripSimpleHtml(a_rawActionLine));
        if (clean.empty()) {
            return parts;
        }

        // Fallout 4 GetActivateText returns "<Name>\n<Verb>" on multi-line targets
        if (clean.find('\n') != std::string::npos || clean.find('\r') != std::string::npos) {
            std::string lastLine;
            std::size_t pos = clean.size();
            while (pos > 0) {
                const auto prevNl = clean.find_last_of("\r\n", pos - 1);
                const std::size_t start = (prevNl == std::string::npos) ? 0 : (prevNl + 1);
                std::string candidate = Trim(std::string_view(clean).substr(start, pos - start));
                if (!candidate.empty()) {
                    lastLine = std::move(candidate);
                    break;
                }
                if (prevNl == std::string::npos) {
                    break;
                }
                pos = prevNl;
            }
            clean = std::move(lastLine);
            if (clean.empty()) {
                return parts;
            }
        }

        // Handle "[E] Open" (do not eat "[Open]" with empty trailing verb)
        if (clean.front() == '[') {
            const auto closeIdx = clean.find(']');
            if (closeIdx != std::string::npos && closeIdx > 1) {
                const std::string candidateKey = Trim(std::string_view(clean).substr(1, closeIdx - 1));
                const std::string candidateVerb = ToUpperAscii(Trim(std::string_view(clean).substr(closeIdx + 1)));
                if (!candidateKey.empty() && !candidateVerb.empty()) {
                    parts.keyLabel = ToUpperAscii(candidateKey);
                    parts.verb = candidateVerb;
                    return parts;
                }
            }
        }

        // Handle "E) Take" or "Enter) Open" (reject balanced tokens like "Use(1)" or trailing ")")
        const auto parenIdx = clean.find(')');
        if (parenIdx != std::string::npos && parenIdx > 0 && parenIdx <= 8) {
            const std::string candidateKey = Trim(std::string_view(clean).substr(0, parenIdx));
            const std::string candidateVerb = ToUpperAscii(Trim(std::string_view(clean).substr(parenIdx + 1)));
            if (!candidateKey.empty() &&
                candidateKey.find(' ') == std::string::npos &&
                candidateKey.find('(') == std::string::npos &&
                !candidateVerb.empty()) {
                parts.keyLabel = ToUpperAscii(candidateKey);
                parts.verb = candidateVerb;
                return parts;
            }
        }

        parts.verb = ToUpperAscii(clean);
        return parts;
    }

    std::string FormatInlineSummary(const TargetMetadata& a_meta, const ConfigData& a_cfg)
    {
        std::string out;

        if (a_meta.isNpc) {
            if (!a_cfg.showNpcStats) {
                return {};
            }
            if (a_meta.maxHealth > 0.0f) {
                AppendPart(out, "HP " + FormatFloatCompact(a_meta.health) + "/" + FormatFloatCompact(a_meta.maxHealth));
            }
            if (a_meta.damageResist > 0.0f) {
                AppendPart(out, "DR " + FormatFloatCompact(a_meta.damageResist));
            }
            if (!a_meta.equippedWeapon.empty()) {
                AppendPart(out, CleanDisplayName(a_meta.equippedWeapon, a_cfg.cleanSorterTags));
            }
            return out;
        }

        if (a_meta.isWeapon && a_cfg.showWeaponArmorStats) {
            AppendPart(out, "DMG " + std::to_string(a_meta.damage));
            if (!a_meta.ammoName.empty()) {
                AppendPart(out, CleanDisplayName(a_meta.ammoName, a_cfg.cleanSorterTags));
            }
        }

        if (a_meta.isArmor && a_cfg.showWeaponArmorStats) {
            AppendPart(out, "DR " + std::to_string(a_meta.armorRating));
        }

        if (!a_meta.scrapComponents.empty() && a_cfg.showScrapComponents) {
            AppendPart(out, FormatComponentsList(a_meta.scrapComponents, 3));
        }

        if (a_meta.hasItemStats && a_cfg.showItemValueWeight) {
            AppendPart(out, "Val " + std::to_string(a_meta.value));
            AppendPart(out, "Wt " + FormatFloatCompact(a_meta.weight));
        }

        return out;
    }

    std::vector<CardRow> BuildInfoCardRows(const TargetMetadata& a_meta, const ConfigData& a_cfg)
    {
        std::vector<CardRow> rows;

        if (a_meta.isNpc) {
            if (!a_cfg.showNpcStats) {
                return rows;
            }
            if (!a_meta.equippedWeapon.empty()) {
                rows.push_back({ "Weapon", CleanDisplayName(a_meta.equippedWeapon, a_cfg.cleanSorterTags) });
            }
            if (a_meta.maxHealth > 0.0f) {
                rows.push_back({ "Health", FormatFloatCompact(a_meta.health) + " / " + FormatFloatCompact(a_meta.maxHealth) });
            }
            if (a_meta.maxActionPoints > 0.0f) {
                rows.push_back({ "Action Points", FormatFloatCompact(a_meta.actionPoints) + " / " + FormatFloatCompact(a_meta.maxActionPoints) });
            }
            if (a_meta.damageResist > 0.0f || a_meta.energyResist > 0.0f) {
                rows.push_back({ "Damage Resist", FormatFloatCompact(a_meta.damageResist) });
                rows.push_back({ "Energy Resist", FormatFloatCompact(a_meta.energyResist) });
            }
            return rows;
        }

        if (a_meta.isWeapon && a_cfg.showWeaponArmorStats) {
            rows.push_back({ "Damage", std::to_string(a_meta.damage) });
            if (!a_meta.ammoName.empty()) {
                rows.push_back({ "Ammo", CleanDisplayName(a_meta.ammoName, a_cfg.cleanSorterTags) });
            }
        }

        if (a_meta.isArmor && a_cfg.showWeaponArmorStats) {
            rows.push_back({ "Armor Rating", std::to_string(a_meta.armorRating) });
        }

        if (!a_meta.scrapComponents.empty() && a_cfg.showScrapComponents) {
            rows.push_back({ "Components", FormatComponentsList(a_meta.scrapComponents, 0) });
        }

        if (a_meta.hasItemStats && a_cfg.showItemValueWeight) {
            rows.push_back({ "Weight", FormatFloatCompact(a_meta.weight) });
            rows.push_back({ "Value", std::to_string(a_meta.value) });
            if (a_meta.weight > 0.001f && a_meta.value > 0) {
                const float ratio = static_cast<float>(a_meta.value) / a_meta.weight;
                rows.push_back({ "Val / Wt", FormatFloatCompact(ratio) });
            }
        }

        return rows;
    }

#ifndef FAP_UNIT_TEST
    namespace
    {
        [[nodiscard]] std::string SafeFullName(const RE::TESForm* a_form)
        {
            if (!a_form) {
                return {};
            }
            const std::string_view sv = RE::TESFullName::GetFullName(*a_form);
            return sv.empty() ? std::string{} : std::string(sv);
        }

        [[nodiscard]] std::string ResolveDisplayNameFromExtra(RE::ExtraDataList* a_extraList, RE::TESBoundObject* a_base)
        {
            if (a_extraList) {
                if (auto* textExtra = a_extraList->GetByType<RE::ExtraTextDisplayData>()) {
                    if (!textExtra->displayName.empty()) {
                        return std::string(textExtra->displayName.c_str());
                    }
                    if (a_base) {
                        const auto& resolved = textExtra->GetDisplayName(a_base);
                        if (!resolved.empty()) {
                            return std::string(resolved.c_str());
                        }
                    }
                }
            }
            return SafeFullName(a_base);
        }

        [[nodiscard]] bool IsWeaponInstanceData(const RE::TBO_InstanceData* a_ptr)
        {
            if (!a_ptr) {
                return false;
            }
            const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(a_ptr);
            const auto weapInstVtbl = REL::Relocation<std::uintptr_t>{ RE::TESObjectWEAP::InstanceData::VTABLE[0] }.address();
            const auto weapDataVtbl = REL::Relocation<std::uintptr_t>{ RE::TESObjectWEAP::Data::VTABLE[0] }.address();
            return vtbl == weapInstVtbl || vtbl == weapDataVtbl;
        }

        [[nodiscard]] bool IsArmorInstanceData(const RE::TBO_InstanceData* a_ptr)
        {
            if (!a_ptr) {
                return false;
            }
            const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(a_ptr);
            const auto armoInstVtbl = REL::Relocation<std::uintptr_t>{ RE::TESObjectARMO::InstanceData::VTABLE[0] }.address();
            return vtbl == armoInstVtbl;
        }

        void PopulateBoundObjectMetadata(
            RE::TESBoundObject* a_baseObj,
            RE::ExtraDataList* a_extraList,
            TargetMetadata& a_meta)
        {
            if (!a_baseObj) {
                return;
            }

            const RE::TBO_InstanceData* instData = nullptr;
            if (a_extraList) {
                if (auto* instExtra = a_extraList->GetByType<RE::ExtraInstanceData>()) {
                    if (instExtra->data && (!instExtra->base || instExtra->base == a_baseObj)) {
                        instData = instExtra->data.get();
                    }
                }
            }
            if (!instData) {
                instData = a_baseObj->GetBaseInstanceData();
            }

            switch (a_baseObj->GetFormType()) {
            case RE::ENUM_FORM_ID::kMISC:
                a_meta.itemKind = ItemKind::MiscJunk;
                a_meta.typeLabel = "Misc";
                a_meta.hasItemStats = true;
                if (const auto* misc = a_baseObj->As<RE::TESObjectMISC>(); misc && misc->componentData) {
                    for (const auto& entry : *misc->componentData) {
                        if (entry.first && entry.second.i > 0) {
                            std::string compName = SafeFullName(entry.first);
                            if (!compName.empty()) {
                                a_meta.scrapComponents.push_back({ std::move(compName), entry.second.i });
                            }
                        }
                    }
                }
                break;

            case RE::ENUM_FORM_ID::kOMOD:
                a_meta.itemKind = ItemKind::MiscJunk;
                a_meta.typeLabel = "Mod";
                a_meta.hasItemStats = true;
                break;

            case RE::ENUM_FORM_ID::kWEAP:
                a_meta.itemKind = ItemKind::Weapon;
                a_meta.typeLabel = "Weapon";
                a_meta.hasItemStats = true;
                a_meta.isWeapon = true;
                if (const auto* weap = a_baseObj->As<RE::TESObjectWEAP>()) {
                    const auto* weapInst = IsWeaponInstanceData(instData)
                        ? static_cast<const RE::TESObjectWEAP::InstanceData*>(instData)
                        : &weap->weaponData;
                    if (weapInst) {
                        a_meta.damage = weapInst->attackDamage;
                        if (weapInst->ammo) {
                            a_meta.ammoName = SafeFullName(weapInst->ammo);
                        }
                    }
                }
                break;

            case RE::ENUM_FORM_ID::kARMO:
                a_meta.itemKind = ItemKind::Armor;
                a_meta.typeLabel = "Armor";
                a_meta.hasItemStats = true;
                a_meta.isArmor = true;
                if (const auto* armo = a_baseObj->As<RE::TESObjectARMO>()) {
                    const auto* armoInst = IsArmorInstanceData(instData)
                        ? static_cast<const RE::TESObjectARMO::InstanceData*>(instData)
                        : &armo->data;
                    if (armoInst) {
                        a_meta.armorRating = armoInst->rating;
                    }
                }
                break;

            case RE::ENUM_FORM_ID::kALCH:
                a_meta.itemKind = ItemKind::Aid;
                a_meta.typeLabel = "Aid";
                a_meta.hasItemStats = true;
                break;

            case RE::ENUM_FORM_ID::kAMMO:
                a_meta.itemKind = ItemKind::Ammo;
                a_meta.typeLabel = "Ammo";
                a_meta.hasItemStats = true;
                break;

            case RE::ENUM_FORM_ID::kKEYM:
                a_meta.itemKind = ItemKind::Key;
                a_meta.typeLabel = "Key";
                a_meta.hasItemStats = true;
                break;

            case RE::ENUM_FORM_ID::kBOOK:
                a_meta.itemKind = ItemKind::BookNote;
                a_meta.typeLabel = "Book";
                a_meta.hasItemStats = true;
                break;

            case RE::ENUM_FORM_ID::kNOTE:
                a_meta.itemKind = ItemKind::Holotape;
                a_meta.typeLabel = "Holotape";
                a_meta.hasItemStats = true;
                break;

            case RE::ENUM_FORM_ID::kCONT:
                a_meta.itemKind = ItemKind::Container;
                a_meta.typeLabel = "Container";
                break;

            case RE::ENUM_FORM_ID::kDOOR:
                a_meta.itemKind = ItemKind::Door;
                a_meta.typeLabel = "Door";
                break;

            case RE::ENUM_FORM_ID::kFURN:
                a_meta.itemKind = ItemKind::Furniture;
                a_meta.typeLabel = "Furniture";
                break;

            case RE::ENUM_FORM_ID::kTERM:
                a_meta.itemKind = ItemKind::Terminal;
                a_meta.typeLabel = "Terminal";
                break;

            default:
                a_meta.itemKind = ItemKind::None;
                break;
            }

            if (a_meta.hasItemStats) {
                const std::uint32_t rawVal = RE::TESValueForm::GetFormValue(a_baseObj, instData);
                a_meta.value = (static_cast<std::int32_t>(rawVal) < 0) ? 0u : rawVal;
                const float rawWt = RE::TESWeightForm::GetFormWeight(a_baseObj, instData);
                a_meta.weight = (!std::isfinite(rawWt) || rawWt < 0.0f) ? 0.0f : rawWt;
            }
        }
    }

    ItemKind ClassifyBoundObject(const RE::TESBoundObject* a_baseObj)
    {
        if (!a_baseObj) {
            return ItemKind::None;
        }
        switch (a_baseObj->GetFormType()) {
        case RE::ENUM_FORM_ID::kMISC:
        case RE::ENUM_FORM_ID::kOMOD:
            return ItemKind::MiscJunk;
        case RE::ENUM_FORM_ID::kWEAP:
            return ItemKind::Weapon;
        case RE::ENUM_FORM_ID::kARMO:
            return ItemKind::Armor;
        case RE::ENUM_FORM_ID::kALCH:
            return ItemKind::Aid;
        case RE::ENUM_FORM_ID::kAMMO:
            return ItemKind::Ammo;
        case RE::ENUM_FORM_ID::kKEYM:
            return ItemKind::Key;
        case RE::ENUM_FORM_ID::kBOOK:
            return ItemKind::BookNote;
        case RE::ENUM_FORM_ID::kNOTE:
            return ItemKind::Holotape;
        case RE::ENUM_FORM_ID::kCONT:
            return ItemKind::Container;
        case RE::ENUM_FORM_ID::kDOOR:
            return ItemKind::Door;
        case RE::ENUM_FORM_ID::kFURN:
            return ItemKind::Furniture;
        case RE::ENUM_FORM_ID::kTERM:
            return ItemKind::Terminal;
        case RE::ENUM_FORM_ID::kNPC_:
            return ItemKind::Npc;
        default:
            return ItemKind::None;
        }
    }

    TargetMetadata ExtractInventoryItemMetadata(RE::TESBoundObject* a_baseObj, RE::ExtraDataList* a_extraList)
    {
        TargetMetadata meta;
        if (!a_baseObj) {
            return meta;
        }
        meta.formID = a_baseObj->GetFormID();
        meta.category = AnchorManager::TargetCategory::kItem;
        meta.displayName = ResolveDisplayNameFromExtra(a_extraList, a_baseObj);
        meta.rawDisplayName = meta.displayName;
        PopulateBoundObjectMetadata(a_baseObj, a_extraList, meta);
        return meta;
    }

    TargetMetadata ExtractTargetMetadata(RE::TESObjectREFR* a_targetRef)
    {
        TargetMetadata meta;
        if (!a_targetRef) {
            return meta;
        }

        meta.formID = a_targetRef->GetFormID();
        meta.category = AnchorManager::ClassifyReference(a_targetRef);

        auto* baseObj = a_targetRef->GetObjectReference();
        if (!baseObj) {
            return meta;
        }

        auto* extraList = a_targetRef->extraList.get();
        meta.displayName = ResolveDisplayNameFromExtra(extraList, baseObj);
        meta.rawDisplayName = meta.displayName;

        RE::BSStringT<char> actText;
        if (baseObj->GetActivateText(a_targetRef, actText) && !actText.empty()) {
            const auto parsed = SplitActivateText(actText.c_str());
            meta.actionVerb = parsed.verb;
        }

        if (auto* actor = a_targetRef->As<RE::Actor>()) {
            meta.isNpc = true;
            meta.itemKind = ItemKind::Npc;
            meta.typeLabel = "NPC";
            if (meta.actionVerb.empty()) {
                meta.actionVerb = "TALK";
            }

            RE::BGSEquipIndex eqIdx;
            eqIdx.index = 0;
            const auto equipped = actor->GetCurrentWeapon(eqIdx);
            if (equipped.object) {
                meta.equippedWeapon = SafeFullName(equipped.object);
            }

            if (const auto* av = RE::ActorValue::GetSingleton()) {
                if (av->health) {
                    meta.health = actor->GetActorValue(*av->health);
                    meta.maxHealth = actor->GetPermanentActorValue(*av->health);
                }
                if (av->actionPoints) {
                    meta.actionPoints = actor->GetActorValue(*av->actionPoints);
                    meta.maxActionPoints = actor->GetPermanentActorValue(*av->actionPoints);
                }
                if (av->damageResistance) {
                    meta.damageResist = actor->GetActorValue(*av->damageResistance);
                }
                if (av->energyResistance) {
                    meta.energyResist = actor->GetActorValue(*av->energyResistance);
                }
            }
            return meta;
        }

        PopulateBoundObjectMetadata(baseObj, extraList, meta);

        if (meta.category == AnchorManager::TargetCategory::kDoor && extraList) {
            if (const auto* teleport = extraList->GetByType<RE::ExtraTeleport>();
                teleport && teleport->teleportData &&
                (teleport->teleportData->transitionCell || teleport->teleportData->linkedDoor)) {
                meta.isLoadDoor = true;
                RE::TESObjectCELL* destCell = teleport->teleportData->transitionCell;
                if (!destCell && teleport->teleportData->linkedDoor) {
                    if (auto linked = teleport->teleportData->linkedDoor.get()) {
                        destCell = linked->GetParentCell();
                    }
                }
                if (destCell) {
                    std::string cellName;
                    std::string worldName;
                    if (destCell->IsInterior()) {
                        cellName = SafeFullName(destCell);
                        if (cellName.empty()) {
                            cellName = SafeFullName(destCell->GetLocation());
                        }
                    } else {
                        if (destCell->worldSpace) {
                            worldName = SafeFullName(destCell->worldSpace);
                        }
                        if (worldName.empty()) {
                            worldName = SafeFullName(destCell);
                        }
                        if (worldName.empty()) {
                            worldName = SafeFullName(destCell->GetLocation());
                        }
                    }
                    meta.doorDestination = ResolveLoadDoorDestination({}, meta.displayName, cellName, worldName);
                }
            }
        }

        if (meta.actionVerb.empty()) {
            switch (meta.category) {
            case AnchorManager::TargetCategory::kDoor:
            case AnchorManager::TargetCategory::kContainer:
                meta.actionVerb = "OPEN";
                break;
            case AnchorManager::TargetCategory::kActor:
                meta.actionVerb = "TALK";
                break;
            default:
                meta.actionVerb = meta.hasItemStats ? "TAKE" : "USE";
                break;
            }
        }

        return meta;
    }
#endif
}

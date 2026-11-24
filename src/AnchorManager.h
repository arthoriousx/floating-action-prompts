#pragma once

#include <string_view>
#include "Config.h"

#ifndef FAP_UNIT_TEST
#include "RE/Fallout.h"
#endif

namespace AnchorManager
{
    enum class TargetCategory
    {
        kDoor,
        kActor,
        kContainer,
        kFurnitureTerminal,
        kItem
    };

    struct AnchorInput
    {
        TargetCategory category{ TargetCategory::kItem };
        Vector3 refPosition{};
        bool hasBoundCenter{ false };
        Vector3 boundCenter{};
        bool hasNamedNode{ false };
        Vector3 namedNodeWorldPos{};
    };

    [[nodiscard]] Vector3 GetCategoryOffset(TargetCategory a_category, const ConfigData& a_config);
    [[nodiscard]] Vector3 GetCategoryOffset(TargetCategory a_category, const Config& a_config);
    [[nodiscard]] Vector3 ComputeAnchorPosition(const AnchorInput& a_input, const ConfigData& a_config);
    [[nodiscard]] Vector3 ComputeAnchorPosition(const AnchorInput& a_input, const Config& a_config);
    [[nodiscard]] Vector3 SelectSafeItemBoundCenter(
        const Vector3& a_refPosition,
        bool a_hasRootGeomCenter,
        const Vector3& a_rootGeomCenter,
        bool a_hasTargetedCenter,
        const Vector3& a_targetedCenter,
        float a_maxDeviation = 40.0f) noexcept;
    [[nodiscard]] bool IsDoorAnchorNodeName(std::string_view a_name);
    [[nodiscard]] bool IsFurnitureMarkerNodeName(std::string_view a_name);
    [[nodiscard]] bool IsTransientEffectNodeName(std::string_view a_name);

#ifndef FAP_UNIT_TEST
    [[nodiscard]] RE::TESObjectREFR* ResolveRefFrom3D(RE::NiAVObject* a_obj);
    [[nodiscard]] bool Does3DBelongToRef(RE::NiAVObject* a_obj, const RE::TESObjectREFR* a_ref);
    [[nodiscard]] TargetCategory ClassifyReference(const RE::TESObjectREFR* a_ref);
    [[nodiscard]] RE::NiPoint3 ResolveAnchorWorldPosition(
        RE::TESObjectREFR* a_ref,
        RE::NiAVObject* a_targeted3D,
        const ConfigData& a_config,
        bool* a_outUsedGeometry = nullptr);
    [[nodiscard]] RE::NiPoint3 ResolveAnchorWorldPosition(
        RE::TESObjectREFR* a_ref,
        RE::NiAVObject* a_targeted3D,
        const Config& a_config,
        bool* a_outUsedGeometry = nullptr);
#endif
}

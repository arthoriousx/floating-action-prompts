#include "AnchorManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace AnchorManager
{
    namespace
    {
        [[nodiscard]] std::string ToLowerASCII(std::string_view a_str)
        {
            std::string out;
            out.reserve(a_str.size());
            for (const unsigned char ch : a_str) {
                out.push_back(static_cast<char>(std::tolower(ch)));
            }
            return out;
        }
    }

    Vector3 GetCategoryOffset(TargetCategory a_category, const ConfigData& a_config)
    {
        switch (a_category) {
        case TargetCategory::kDoor:
            return a_config.doorOffset;
        case TargetCategory::kActor:
            return a_config.npcOffset;
        case TargetCategory::kContainer:
            return a_config.containerOffset;
        case TargetCategory::kFurnitureTerminal:
            return a_config.furnitureOffset;
        case TargetCategory::kItem:
        default:
            return a_config.itemOffset;
        }
    }

    Vector3 GetCategoryOffset(TargetCategory a_category, const Config& a_config)
    {
        return GetCategoryOffset(a_category, a_config.GetSnapshot());
    }

    Vector3 ComputeAnchorPosition(const AnchorInput& a_input, const ConfigData& a_config)
    {
        Vector3 base = a_input.refPosition;
        if (a_input.hasNamedNode) {
            base = a_input.namedNodeWorldPos;
        } else if (a_input.hasBoundCenter) {
            base = a_input.boundCenter;
        }

        const Vector3 offset = GetCategoryOffset(a_input.category, a_config);
        return {
            base.x + offset.x,
            base.y + offset.y,
            base.z + offset.z
        };
    }

    Vector3 ComputeAnchorPosition(const AnchorInput& a_input, const Config& a_config)
    {
        return ComputeAnchorPosition(a_input, a_config.GetSnapshot());
    }

    bool IsDoorAnchorNodeName(std::string_view a_name)
    {
        if (a_name.empty()) {
            return false;
        }

        const std::string lower = ToLowerASCII(a_name);
        static constexpr std::array<std::string_view, 4> kKeywords{
            "handle",
            "lock",
            "knob",
            "latch"
        };

        for (const auto kw : kKeywords) {
            if (lower.find(kw) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    bool IsFurnitureMarkerNodeName(std::string_view a_name)
    {
        if (a_name.empty()) {
            return false;
        }

        const std::string lower = ToLowerASCII(a_name);
        static constexpr std::array<std::string_view, 6> kMarkerKeywords{
            "marker",
            "furnituremarker",
            "animobject",
            "editor",
            "collision",
            "bsfurniture"
        };

        for (const auto kw : kMarkerKeywords) {
            if (lower.find(kw) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    bool IsTransientEffectNodeName(std::string_view a_name)
    {
        if (a_name.empty()) {
            return false;
        }

        // Segment prefixes separated only by non-alphanumeric delimiters (with trailing digits stripped).
        static constexpr std::array<std::string_view, 4> kSegmentPrefixes{
            "bsvaluenode",
            "addonnode",
            "psys",
            "vfx"
        };
        // Exact alphabetic tokens after splitting on non-letters and camelCase/acronym boundaries.
        static constexpr std::array<std::string_view, 17> kEffectTokens{
            "psys",
            "particle",
            "particles",
            "emitter",
            "emitters",
            "smoke",
            "flame",
            "flames",
            "ember",
            "embers",
            "spark",
            "sparks",
            "mps",
            "vfx",
            "fx",
            "mist",
            "mists"
        };

        auto matchToken = [](std::string_view a_tok) noexcept {
            for (const auto kw : kEffectTokens) {
                if (a_tok == kw) {
                    return true;
                }
            }
            return false;
        };

        std::string segment;
        segment.reserve(a_name.size());
        auto flushSegment = [&]() {
            while (!segment.empty() && std::isdigit(static_cast<unsigned char>(segment.back()))) {
                segment.pop_back();
            }
            for (const auto prefix : kSegmentPrefixes) {
                if (segment.size() >= prefix.size() &&
                    segment.compare(0, prefix.size(), prefix) == 0) {
                    return true;
                }
            }
            segment.clear();
            return false;
        };

        for (const unsigned char ch : a_name) {
            if (std::isalnum(ch)) {
                segment.push_back(static_cast<char>(std::tolower(ch)));
            } else if (!segment.empty() && flushSegment()) {
                return true;
            }
        }
        if (!segment.empty() && flushSegment()) {
            return true;
        }

        std::string token;
        token.reserve(a_name.size());
        for (std::size_t i = 0; i < a_name.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(a_name[i]);
            if (!std::isalpha(ch)) {
                if (!token.empty()) {
                    if (matchToken(token)) {
                        return true;
                    }
                    token.clear();
                }
                continue;
            }

            if (!token.empty() && std::isupper(ch)) {
                const unsigned char prev = static_cast<unsigned char>(a_name[i - 1]);
                const bool prevLower = std::islower(prev) != 0;
                const bool acronymEnd =
                    (std::isupper(prev) != 0) &&
                    (i + 1 < a_name.size()) &&
                    (std::islower(static_cast<unsigned char>(a_name[i + 1])) != 0);
                if (prevLower || acronymEnd) {
                    if (matchToken(token)) {
                        return true;
                    }
                    token.clear();
                }
            }
            token.push_back(static_cast<char>(std::tolower(ch)));
        }

        return !token.empty() && matchToken(token);
    }

#ifndef FAP_UNIT_TEST
    namespace
    {
        struct TraversalVisitedSet
        {
            std::size_t visitedCount{ 0 };
            std::vector<const RE::NiAVObject*> visited{};

            bool TryMarkVisited(const RE::NiAVObject* a_obj)
            {
                if (!a_obj || visitedCount >= 256) {
                    return false;
                }
                if (std::find(visited.begin(), visited.end(), a_obj) != visited.end()) {
                    return false;
                }
                visited.push_back(a_obj);
                ++visitedCount;
                return true;
            }
        };

        struct GeometryBoundsAccumulator
        {
            float minX{ 0.0f };
            float minY{ 0.0f };
            float minZ{ 0.0f };
            float maxX{ 0.0f };
            float maxY{ 0.0f };
            float maxZ{ 0.0f };
            std::size_t geomCount{ 0 };

            void AddSphere(const RE::NiPoint3& a_center, float a_radius)
            {
                if (!std::isfinite(a_center.x) || !std::isfinite(a_center.y) ||
                    !std::isfinite(a_center.z) || !std::isfinite(a_radius) ||
                    a_radius <= 0.001f || a_radius >= 10000.0f) {
                    return;
                }
                const float x0 = a_center.x - a_radius;
                const float y0 = a_center.y - a_radius;
                const float z0 = a_center.z - a_radius;
                const float x1 = a_center.x + a_radius;
                const float y1 = a_center.y + a_radius;
                const float z1 = a_center.z + a_radius;
                if (geomCount == 0) {
                    minX = x0; minY = y0; minZ = z0;
                    maxX = x1; maxY = y1; maxZ = z1;
                } else {
                    minX = std::min(minX, x0);
                    minY = std::min(minY, y0);
                    minZ = std::min(minZ, z0);
                    maxX = std::max(maxX, x1);
                    maxY = std::max(maxY, y1);
                    maxZ = std::max(maxZ, z1);
                }
                ++geomCount;
            }
        };

        [[nodiscard]] bool IsExcludedByRTTI(const RE::NiAVObject* a_obj)
        {
            if (!a_obj) {
                return true;
            }
            static constexpr std::array<std::string_view, 8> kExcludedRTTI{
                "BSValueNode",
                "BSMasterParticleSystem",
                "NiBillboardNode",
                "NiParticleSystem",
                "NiParticles",
                "NiParticleMeshes",
                "BSDebrisNode",
                "BSBlastNode"
            };
            int hops = 0;
            for (const RE::NiRTTI* rtti = a_obj->GetRTTI(); rtti && hops < 32; rtti = rtti->GetBaseRTTI(), ++hops) {
                const char* name = rtti->GetName();
                if (!name) {
                    continue;
                }
                const std::string_view sv{ name };
                for (const auto ex : kExcludedRTTI) {
                    if (sv == ex) {
                        return true;
                    }
                }
            }
            return false;
        }

        [[nodiscard]] bool IsExcludedNodeOrEffect(RE::NiAVObject* a_obj, bool a_checkName = true)
        {
            if (!a_obj || a_obj->GetAppCulled()) {
                return true;
            }
            if (a_obj->IsParticlesGeom() != nullptr ||
                a_obj->IsParticleSystem() != nullptr ||
                a_obj->IsLinesGeom() != nullptr ||
                a_obj->IsLight() != nullptr ||
                a_obj->IsDynamicTriShape() != nullptr ||
                IsExcludedByRTTI(a_obj)) {
                return true;
            }
            if (a_checkName &&
                (IsFurnitureMarkerNodeName(a_obj->GetName()) ||
                 IsTransientEffectNodeName(a_obj->GetName()))) {
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsStaticTriangleMesh(RE::NiAVObject* a_obj)
        {
            return a_obj->IsTriShape() != nullptr ||
                   a_obj->IsNiTriShape() != nullptr ||
                   a_obj->IsTriStrips() != nullptr ||
                   a_obj->IsSubIndexTriShape() != nullptr ||
                   a_obj->IsSegmentedTriShape() != nullptr;
        }

        void AccumulateVisibleGeometryBoundsRecursive(
            RE::NiAVObject* a_obj,
            TraversalVisitedSet& a_visited,
            GeometryBoundsAccumulator& a_staticAcc,
            GeometryBoundsAccumulator& a_fallbackAcc,
            int a_depth = 0)
        {
            if (!a_obj || a_depth > 16 || IsExcludedNodeOrEffect(a_obj, true)) {
                return;
            }
            if (!a_visited.TryMarkVisited(a_obj)) {
                return;
            }

            if (a_obj->IsGeometry() != nullptr) {
                if (IsStaticTriangleMesh(a_obj)) {
                    a_staticAcc.AddSphere(a_obj->worldBound.center, a_obj->worldBound.fRadius);
                } else {
                    a_fallbackAcc.AddSphere(a_obj->worldBound.center, a_obj->worldBound.fRadius);
                }
            }

            if (auto* node = a_obj->IsNode(); node && node->children.data()) {
                const auto cap = node->children.capacity();
                for (std::uint16_t i = 0; i < cap; ++i) {
                    if (auto* child = node->children[i].get()) {
                        AccumulateVisibleGeometryBoundsRecursive(child, a_visited, a_staticAcc, a_fallbackAcc, a_depth + 1);
                    }
                }
            }
        }

        [[nodiscard]] bool ComputeVisibleGeometryBoundCenter(RE::NiAVObject* a_root, Vector3& a_outCenter)
        {
            if (!a_root) {
                return false;
            }
            TraversalVisitedSet visited;
            visited.visited.reserve(32);
            GeometryBoundsAccumulator staticAcc;
            GeometryBoundsAccumulator fallbackAcc;
            AccumulateVisibleGeometryBoundsRecursive(a_root, visited, staticAcc, fallbackAcc, 0);
            const auto& chosen = (staticAcc.geomCount > 0) ? staticAcc : fallbackAcc;
            if (chosen.geomCount == 0) {
                return false;
            }
            a_outCenter = {
                0.5f * (chosen.minX + chosen.maxX),
                0.5f * (chosen.minY + chosen.maxY),
                0.5f * (chosen.minZ + chosen.maxZ)
            };
            return true;
        }

        [[nodiscard]] bool IsTargetedSubtreeVisibleNonMarker(
            RE::NiAVObject* a_targeted3D,
            RE::NiAVObject* a_root3D)
        {
            if (!a_targeted3D) {
                return false;
            }
            auto* cur = a_targeted3D;
            int guard = 0;
            while (cur && guard++ < 64) {
                if (IsExcludedNodeOrEffect(cur, cur != a_root3D)) {
                    return false;
                }
                if (cur == a_root3D) {
                    break;
                }
                cur = cur->parent;
            }
            return true;
        }

        [[nodiscard]] RE::NiAVObject* FindDoorAnchorNodeRecursive(RE::NiAVObject* a_obj, int a_depth = 0)
        {
            if (!a_obj || a_obj->GetAppCulled() || a_depth > 16) {
                return nullptr;
            }

            if (IsDoorAnchorNodeName(a_obj->GetName())) {
                return a_obj;
            }

            if (auto* node = a_obj->IsNode(); node && node->children.data()) {
                const auto cap = node->children.capacity();
                for (std::uint16_t i = 0; i < cap; ++i) {
                    if (auto* child = node->children[i].get()) {
                        if (auto* found = FindDoorAnchorNodeRecursive(child, a_depth + 1)) {
                            return found;
                        }
                    }
                }
            }

            return nullptr;
        }

        [[nodiscard]] RE::NiAVObject* FindDoorAnchorFromTargetedSubtree(
            RE::NiAVObject* a_targeted3D,
            RE::NiAVObject* a_root3D)
        {
            if (!a_targeted3D) {
                return nullptr;
            }

            auto* probe = a_targeted3D;
            for (int up = 0; up < 3 && probe && probe != a_root3D; ++up) {
                if (auto* found = FindDoorAnchorNodeRecursive(probe)) {
                    return found;
                }
                probe = probe->parent;
            }
            return nullptr;
        }

        [[nodiscard]] RE::NiAVObject* FindActorAnchorNode(RE::NiAVObject* a_root3D)
        {
            if (!a_root3D) {
                return nullptr;
            }

            static constexpr std::array<const char*, 6> kActorBoneNames{
                "SPINE2",
                "SPINE1",
                "Chest",
                "Bip01 Spine2",
                "Bip01 Spine1",
                "HEAD"
            };

            for (const char* boneName : kActorBoneNames) {
                if (auto* found = a_root3D->GetObjectByName(RE::BSFixedString(boneName))) {
                    if (!found->GetAppCulled()) {
                        return found;
                    }
                }
            }
            return nullptr;
        }
    }

    RE::TESObjectREFR* ResolveRefFrom3D(RE::NiAVObject* a_obj)
    {
        auto* current = a_obj;
        int guard = 0;
        while (current && guard++ < 64) {
            if (current->userData > 0x10000) {
                return reinterpret_cast<RE::TESObjectREFR*>(current->userData);
            }
            current = current->parent;
        }
        return nullptr;
    }

    bool Does3DBelongToRef(RE::NiAVObject* a_obj, const RE::TESObjectREFR* a_ref)
    {
        if (!a_obj || !a_ref) {
            return false;
        }

        const auto targetAddr = reinterpret_cast<std::uintptr_t>(a_ref);
        const auto* refRoot = a_ref->Get3D();

        auto* current = a_obj;
        int guard = 0;
        while (current && guard++ < 64) {
            if (current == refRoot || current->userData == targetAddr) {
                return true;
            }
            current = current->parent;
        }
        return false;
    }

    TargetCategory ClassifyReference(const RE::TESObjectREFR* a_ref)
    {
        if (!a_ref) {
            return TargetCategory::kItem;
        }

        if (a_ref->Is(RE::ENUM_FORM_ID::kACHR)) {
            return TargetCategory::kActor;
        }

        if (const auto* base = a_ref->GetObjectReference()) {
            switch (base->GetFormType()) {
            case RE::ENUM_FORM_ID::kNPC_:
                return TargetCategory::kActor;
            case RE::ENUM_FORM_ID::kDOOR:
                return TargetCategory::kDoor;
            case RE::ENUM_FORM_ID::kCONT:
                return TargetCategory::kContainer;
            case RE::ENUM_FORM_ID::kTERM:
            case RE::ENUM_FORM_ID::kFURN:
                return TargetCategory::kFurnitureTerminal;
            default:
                break;
            }
        }

        return TargetCategory::kItem;
    }

    RE::NiPoint3 ResolveAnchorWorldPosition(
        RE::TESObjectREFR* a_ref,
        RE::NiAVObject* a_targeted3D,
        const ConfigData& a_config,
        bool* a_outUsedGeometry)
    {
        if (a_outUsedGeometry) {
            *a_outUsedGeometry = false;
        }
        if (!a_ref) {
            return RE::NiPoint3{};
        }

        AnchorInput input{};
        input.category = ClassifyReference(a_ref);

        const auto pos = a_ref->GetPosition();
        input.refPosition = { pos.x, pos.y, pos.z };

        RE::NiAVObject* refRoot3D = a_ref->Get3D();
        RE::NiAVObject* root3D = refRoot3D ? refRoot3D : a_targeted3D;

        const auto* base = a_ref->GetObjectReference();
        const bool isTerminalOrFurniture = (input.category == TargetCategory::kFurnitureTerminal);
        const bool isActivator = (base && base->GetFormType() == RE::ENUM_FORM_ID::kACTI);

        if (isTerminalOrFurniture || isActivator) {
            Vector3 geomCenter{};
            if (refRoot3D && ComputeVisibleGeometryBoundCenter(refRoot3D, geomCenter)) {
                input.hasBoundCenter = true;
                input.boundCenter = geomCenter;
                if (a_outUsedGeometry) {
                    *a_outUsedGeometry = true;
                }
            } else if (IsTargetedSubtreeVisibleNonMarker(a_targeted3D, refRoot3D) &&
                       ComputeVisibleGeometryBoundCenter(a_targeted3D, geomCenter)) {
                input.hasBoundCenter = true;
                input.boundCenter = geomCenter;
                if (a_outUsedGeometry) {
                    *a_outUsedGeometry = true;
                }
            }
        }

        if (!input.hasBoundCenter && root3D && root3D->worldBound.fRadius > 0.001f) {
            input.hasBoundCenter = true;
            input.boundCenter = {
                root3D->worldBound.center.x,
                root3D->worldBound.center.y,
                root3D->worldBound.center.z
            };
        }

        RE::NiAVObject* namedNode = nullptr;
        if (input.category == TargetCategory::kDoor) {
            namedNode = FindDoorAnchorFromTargetedSubtree(a_targeted3D, root3D);
            if (!namedNode && root3D) {
                namedNode = FindDoorAnchorNodeRecursive(root3D);
            }
        } else if (input.category == TargetCategory::kActor && root3D) {
            namedNode = FindActorAnchorNode(root3D);
        }

        if (namedNode) {
            input.hasNamedNode = true;
            input.namedNodeWorldPos = {
                namedNode->world.translate.x,
                namedNode->world.translate.y,
                namedNode->world.translate.z
            };
        }

        const Vector3 finalPos = ComputeAnchorPosition(input, a_config);
        return RE::NiPoint3{ finalPos.x, finalPos.y, finalPos.z };
    }

    RE::NiPoint3 ResolveAnchorWorldPosition(
        RE::TESObjectREFR* a_ref,
        RE::NiAVObject* a_targeted3D,
        const Config& a_config,
        bool* a_outUsedGeometry)
    {
        return ResolveAnchorWorldPosition(a_ref, a_targeted3D, a_config.GetSnapshot(), a_outUsedGeometry);
    }
#endif
}

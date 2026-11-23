#include "HUDController.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>

#include "OverlayLayout.h"

#ifndef FAP_UNIT_TEST
#include <Windows.h>
#include "AnchorManager.h"
#include "MenuFrameworkUI.h"
#endif

namespace HUDController
{
    void HUDTransformState::Reset() noexcept
    {
        _isFloating = false;
        _hasBaseline = false;
        _baseX = 0.0f;
        _baseY = 0.0f;
        _hasPrevScreen = false;
        _prevScreen = {};
        _hasAnchorTarget = false;
        _anchorTargetX = 0.0f;
        _anchorTargetY = 0.0f;
        _modifiedAlpha = false;
        _hidingAlpha = false;
        _lossFrames = 0;
    }

    HUDFrameOutput HUDTransformState::Step(const HUDFrameInput& a_in)
    {
        const bool immediateAbort =
            !a_in.enabled ||
            (a_in.inPowerArmor && a_in.powerArmorBypass) ||
            a_in.quickContainerVisible;

        if (immediateAbort) {
            _lossFrames = 0;
            _hasPrevScreen = false;
            _hasAnchorTarget = false;
            _hidingAlpha = false;

            if (_isFloating && _hasBaseline) {
                _isFloating = false;
                const bool restoreAlpha = _modifiedAlpha;
                _modifiedAlpha = false;
                if (a_in.inPowerArmor && a_in.powerArmorBypass) {
                    _hasBaseline = false;
                }
                return {
                    HUDAction::kRestoreBaseline,
                    _baseX,
                    _baseY,
                    1.0f,
                    true,
                    restoreAlpha
                };
            }

            if (a_in.inPowerArmor && a_in.powerArmorBypass) {
                _hasBaseline = false;
            }
            return {};
        }

        const bool targetActive = a_in.rolloverVisible && a_in.hasTarget;
        if (!targetActive) {
            if (_isFloating && _hasBaseline) {
                if (++_lossFrames <= kLossGraceFrames) {
                    return {};
                }
                _isFloating = false;
                _lossFrames = 0;
                _hasPrevScreen = false;
                _hasAnchorTarget = false;
                _hidingAlpha = false;
                const bool restoreAlpha = _modifiedAlpha;
                _modifiedAlpha = false;
                return {
                    HUDAction::kRestoreBaseline,
                    _baseX,
                    _baseY,
                    1.0f,
                    true,
                    restoreAlpha
                };
            }

            _lossFrames = 0;
            _hasPrevScreen = false;
            _hasAnchorTarget = false;
            _hidingAlpha = false;
            return {};
        }

        _lossFrames = 0;
        if (!_isFloating) {
            _baseX = a_in.currentClipX;
            _baseY = a_in.currentClipY;
            _hasBaseline = true;
            _isFloating = true;
            _hasPrevScreen = false;
            _hasAnchorTarget = false;
            _modifiedAlpha = false;
            _hidingAlpha = false;
        }

        if (!a_in.projectedTarget.visible) {
            _hasPrevScreen = false;
            _hasAnchorTarget = false;
            _modifiedAlpha = true;
            _hidingAlpha = true;
            return {
                HUDAction::kApplyFloat,
                0.0f,
                0.0f,
                0.0f,
                false,
                true
            };
        }

        Projection::ScreenCoord targetForStep = a_in.projectedTarget;
        if (a_in.deadzonePx > 0.0f) {
            if (!_hasAnchorTarget) {
                _anchorTargetX = a_in.projectedTarget.x;
                _anchorTargetY = a_in.projectedTarget.y;
                _hasAnchorTarget = true;
            } else {
                const float dist = std::hypot(
                    a_in.projectedTarget.x - _anchorTargetX,
                    a_in.projectedTarget.y - _anchorTargetY);
                if (dist >= a_in.deadzonePx) {
                    _anchorTargetX = a_in.projectedTarget.x;
                    _anchorTargetY = a_in.projectedTarget.y;
                }
            }
            targetForStep.x = _anchorTargetX;
            targetForStep.y = _anchorTargetY;
        } else {
            _anchorTargetX = a_in.projectedTarget.x;
            _anchorTargetY = a_in.projectedTarget.y;
            _hasAnchorTarget = true;
        }

        const auto smoothed = Projection::SmoothStep(
            _prevScreen,
            targetForStep,
            a_in.smoothingSpeed,
            _hasPrevScreen,
            a_in.deltaSeconds);

        _prevScreen = smoothed;
        _hasPrevScreen = true;

        if (a_in.hideNativeClip) {
            _modifiedAlpha = true;
            _hidingAlpha = true;
            return {
                HUDAction::kApplyFloat,
                smoothed.x,
                smoothed.y,
                0.0f,
                true,
                true
            };
        }

        const bool isFullOpaque = smoothed.alpha >= 0.999f;
        const bool applyAlpha = !isFullOpaque || _modifiedAlpha;
        _modifiedAlpha = !isFullOpaque;
        _hidingAlpha = (smoothed.alpha <= 0.01f);

        return {
            HUDAction::kApplyFloat,
            smoothed.x,
            smoothed.y,
            smoothed.alpha,
            true,
            applyAlpha
        };
    }

    void StageToParentLocalFallback(
        float a_stageX,
        float a_stageY,
        float a_parentX,
        float a_parentY,
        float a_parentScaleX,
        float a_parentScaleY,
        float& a_outLocalX,
        float& a_outLocalY) noexcept
    {
        const float sx = (std::fabs(a_parentScaleX) < 0.0001f) ? 1.0f : a_parentScaleX;
        const float sy = (std::fabs(a_parentScaleY) < 0.0001f) ? 1.0f : a_parentScaleY;
        a_outLocalX = (a_stageX - a_parentX) / sx;
        a_outLocalY = (a_stageY - a_parentY) / sy;
    }

    namespace
    {
        [[nodiscard]] std::string TrimWhitespace(std::string_view a_sv)
        {
            const auto first = a_sv.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = a_sv.find_last_not_of(" \t\r\n");
            return std::string(a_sv.substr(first, last - first + 1));
        }

        [[nodiscard]] bool DoItemNamesMatch(std::string_view a_lhs, std::string_view a_rhs)
        {
            if (a_lhs == a_rhs) {
                return true;
            }
            const std::string cLhs = TargetInfo::ToUpperAscii(TargetInfo::CleanDisplayName(std::string(a_lhs), true));
            const std::string cRhs = TargetInfo::ToUpperAscii(TargetInfo::CleanDisplayName(std::string(a_rhs), true));
            return !cLhs.empty() && cLhs == cRhs;
        }

        [[nodiscard]] bool IsEllipsisItem(const QuickContainerItemEntry& a_item)
        {
            const std::string t = TrimWhitespace(a_item.name);
            return t == "..." || t == "\xE2\x80\xA6";
        }

        [[nodiscard]] std::string RawUpperKey(std::string_view a_sv)
        {
            return TargetInfo::ToUpperAscii(TrimWhitespace(a_sv));
        }

        [[nodiscard]] std::string CleanUpperKey(std::string_view a_sv)
        {
            return TargetInfo::ToUpperAscii(TargetInfo::CleanDisplayName(std::string(a_sv), true));
        }

        [[nodiscard]] int EntryMatchScorePrecomputed(
            const QuickContainerItemEntry& a_lhs,
            const std::string& a_lhsRawUpper,
            const std::string& a_lhsCleanUpper,
            const QuickContainerItemEntry& a_rhs,
            const std::string& a_rhsRawUpper,
            const std::string& a_rhsCleanUpper)
        {
            if (a_lhs.name == a_rhs.name) {
                return (a_lhs.count == a_rhs.count) ? 6 : 4;
            }
            if (!a_lhsRawUpper.empty() && a_lhsRawUpper == a_rhsRawUpper) {
                return (a_lhs.count == a_rhs.count) ? 5 : 3;
            }
            if (!a_lhsCleanUpper.empty() && a_lhsCleanUpper == a_rhsCleanUpper) {
                return (a_lhs.count == a_rhs.count) ? 3 : 1;
            }
            return 0;
        }
    }

    void AggregateItemStacks(std::vector<QuickContainerItemEntry>& a_stacks)
    {
        if (a_stacks.size() <= 1) {
            return;
        }

        std::vector<QuickContainerItemEntry> merged;
        merged.reserve(a_stacks.size());

        for (auto& cur : a_stacks) {
            bool found = false;
            for (auto& existing : merged) {
                if (existing.isEquipped == cur.isEquipped &&
                    existing.isFavorite == cur.isFavorite &&
                    existing.isLegendary == cur.isLegendary &&
                    existing.stackSignature == cur.stackSignature &&
                    existing.name == cur.name) {
                    const std::uint64_t sum =
                        static_cast<std::uint64_t>(existing.count) + cur.count;
                    existing.count = static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(std::numeric_limits<std::uint32_t>::max(), sum));
                    existing.isTaggedForSearch = existing.isTaggedForSearch || cur.isTaggedForSearch;
                    if (existing.itemKind == TargetInfo::ItemKind::None) {
                        existing.itemKind = cur.itemKind;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                merged.push_back(std::move(cur));
            }
        }

        a_stacks = std::move(merged);
    }

    QuickContainerMergeResult MergeQuickContainerInventory(
        const std::vector<QuickContainerItemEntry>& a_engineWindow,
        std::int32_t a_engineSelectedIdx,
        const std::vector<QuickContainerItemEntry>& a_inventoryItems,
        int a_maxVisibleRows,
        int a_preferredOrder,
        std::int32_t a_prevStart)
    {
        QuickContainerMergeResult out;
        const int maxRows = std::clamp(a_maxVisibleRows, 3, 12);

        bool hasEllipsisBefore = false;
        bool hasEllipsisAfter = false;
        std::vector<QuickContainerItemEntry> realEngine;
        realEngine.reserve(a_engineWindow.size());
        std::int32_t mappedSel = -1;

        for (std::size_t i = 0; i < a_engineWindow.size(); ++i) {
            const auto& item = a_engineWindow[i];
            if (IsEllipsisItem(item)) {
                if (realEngine.empty()) {
                    hasEllipsisBefore = true;
                } else {
                    hasEllipsisAfter = true;
                }
                continue;
            }
            if (static_cast<std::int32_t>(i) == a_engineSelectedIdx) {
                mappedSel = static_cast<std::int32_t>(realEngine.size());
            }
            realEngine.push_back(item);
        }

        if (realEngine.empty()) {
            return out;
        }
        if (mappedSel < 0) {
            mappedSel = std::clamp(a_engineSelectedIdx, 0, static_cast<std::int32_t>(realEngine.size()) - 1);
        }

        std::vector<std::string> engRawUpper;
        std::vector<std::string> engCleanUpper;
        engRawUpper.reserve(realEngine.size());
        engCleanUpper.reserve(realEngine.size());
        for (const auto& eng : realEngine) {
            engRawUpper.push_back(RawUpperKey(eng.name));
            engCleanUpper.push_back(CleanUpperKey(eng.name));
        }

        const bool invExpandable =
            !a_inventoryItems.empty() &&
            a_inventoryItems.size() <= kMaxExpandedInventoryItems;

        std::vector<std::string> invRawUpper;
        std::vector<std::string> invCleanUpper;
        if (invExpandable) {
            invRawUpper.reserve(a_inventoryItems.size());
            invCleanUpper.reserve(a_inventoryItems.size());
            for (const auto& inv : a_inventoryItems) {
                invRawUpper.push_back(RawUpperKey(inv.name));
                invCleanUpper.push_back(CleanUpperKey(inv.name));
            }

            std::vector<bool> invConsumed(a_inventoryItems.size(), false);
            for (std::size_t j = 0; j < realEngine.size(); ++j) {
                int bestIdx = -1;
                int bestScore = 0;
                for (std::size_t k = 0; k < a_inventoryItems.size(); ++k) {
                    if (invConsumed[k]) {
                        continue;
                    }
                    const int s = EntryMatchScorePrecomputed(
                        realEngine[j], engRawUpper[j], engCleanUpper[j],
                        a_inventoryItems[k], invRawUpper[k], invCleanUpper[k]);
                    if (s > bestScore) {
                        bestScore = s;
                        bestIdx = static_cast<int>(k);
                    }
                }
                if (bestIdx >= 0) {
                    invConsumed[static_cast<std::size_t>(bestIdx)] = true;
                    if (realEngine[j].itemKind == TargetInfo::ItemKind::None) {
                        realEngine[j].itemKind = a_inventoryItems[static_cast<std::size_t>(bestIdx)].itemKind;
                    }
                }
            }
        } else if (!a_inventoryItems.empty()) {
            // Large container past kMaxExpandedInventoryItems: only enrich icon kinds without full sorting
            const std::size_t scanLimit = std::min<std::size_t>(a_inventoryItems.size(), 2048);
            for (std::size_t j = 0; j < realEngine.size(); ++j) {
                if (realEngine[j].itemKind != TargetInfo::ItemKind::None) {
                    continue;
                }
                for (std::size_t k = 0; k < scanLimit; ++k) {
                    if (realEngine[j].name == a_inventoryItems[k].name ||
                        (!engCleanUpper[j].empty() && engCleanUpper[j] == CleanUpperKey(a_inventoryItems[k].name))) {
                        realEngine[j].itemKind = a_inventoryItems[k].itemKind;
                        break;
                    }
                }
            }
        }

        const bool engineTruncated = hasEllipsisBefore || hasEllipsisAfter;
        const bool bottomFullWindow =
            !engineTruncated && realEngine.size() >= 5 && (a_prevStart >= 0);
        const bool canAttemptExpand =
            invExpandable &&
            (a_inventoryItems.size() > realEngine.size()) &&
            (engineTruncated || bottomFullWindow);

        if (!canAttemptExpand) {
            const int total = static_cast<int>(realEngine.size());
            const auto win = OverlayLayout::ComputeListWindow(total, mappedSel, maxRows);
            out.totalItemCount = total;
            out.windowStartIndex = win.start;
            out.selectedIndex = win.clampedSelected;
            out.matchedStartIndex = -1;
            out.hasMoreAbove = hasEllipsisBefore || (win.start > 0);
            out.hasMoreBelow = hasEllipsisAfter || (win.end < total);
            for (int i = win.start; i < win.end; ++i) {
                out.windowItems.push_back(std::move(realEngine[static_cast<std::size_t>(i)]));
            }
            return out;
        }

        const std::size_t nInv = a_inventoryItems.size();
        const std::size_t winLen = realEngine.size();

        // Build 5 lightweight index orderings with zero string copies in comparators:
        // 0 = forward BGSInventoryList, 1 = reversed, 2 = case-insensitive raw (with [FIS] tags),
        // 3 = case-sensitive raw, 4 = tag-stripped case-insensitive
        std::array<std::vector<std::size_t>, 5> orderCandidates;
        orderCandidates[0].resize(nInv);
        for (std::size_t k = 0; k < nInv; ++k) {
            orderCandidates[0][k] = k;
        }
        orderCandidates[1].assign(orderCandidates[0].rbegin(), orderCandidates[0].rend());
        orderCandidates[2] = orderCandidates[0];
        std::stable_sort(orderCandidates[2].begin(), orderCandidates[2].end(),
            [&invRawUpper](std::size_t a, std::size_t b) {
                return invRawUpper[a] < invRawUpper[b];
            });
        orderCandidates[3] = orderCandidates[0];
        std::stable_sort(orderCandidates[3].begin(), orderCandidates[3].end(),
            [&a_inventoryItems](std::size_t a, std::size_t b) {
                return a_inventoryItems[a].name < a_inventoryItems[b].name;
            });
        orderCandidates[4] = orderCandidates[0];
        std::stable_sort(orderCandidates[4].begin(), orderCandidates[4].end(),
            [&invCleanUpper](std::size_t a, std::size_t b) {
                return invCleanUpper[a] < invCleanUpper[b];
            });

        // Boundary enforcement tailored to vanilla FO4 (which omits leading "..." mid-scroll and omits
        // all "..." on the 5-item bottom window) as well as dual-ellipsis UI mods.
        std::size_t startMin = 0;
        std::size_t startMax = nInv - winLen;
        if (hasEllipsisBefore && hasEllipsisAfter) {
            if (nInv < winLen + 2) {
                startMin = 1;
                startMax = 0;
            } else {
                startMin = 1;
                startMax = nInv - winLen - 1;
            }
        } else if (!hasEllipsisBefore && hasEllipsisAfter) {
            // Fresh open at top (a_prevStart < 0 && mappedSel == 0): strictly require start == 0
            // (protects Steamer Trunk against inserting an unverified row above real row 0).
            // Once scrolled or mappedSel > 0, vanilla FO4 omits leading "...", so allow any interior start before trailing "...".
            if (a_prevStart < 0 && mappedSel == 0) {
                startMin = 0;
                startMax = 0;
            } else {
                startMin = 0;
                startMax = (nInv > winLen) ? (nInv - winLen - 1) : 0;
            }
        } else {
            // !hasEllipsisAfter (either hasEllipsisBefore or vanilla 5-item bottom window):
            // must anchor strictly at the end of the container.
            startMin = nInv - winLen;
            startMax = nInv - winLen;
        }

        int bestCand = -1;
        std::size_t bestStart = 0;
        int bestCandScore = std::numeric_limits<int>::min();

        for (int c = 0; c < 5; ++c) {
            const auto& idxs = orderCandidates[static_cast<std::size_t>(c)];
            for (std::size_t start = startMin; start <= startMax; ++start) {
                int totalScore = 0;
                bool allMatch = true;
                for (std::size_t j = 0; j < winLen; ++j) {
                    const std::size_t k = idxs[start + j];
                    const int s = EntryMatchScorePrecomputed(
                        realEngine[j], engRawUpper[j], engCleanUpper[j],
                        a_inventoryItems[k], invRawUpper[k], invCleanUpper[k]);
                    if (s <= 0) {
                        allMatch = false;
                        break;
                    }
                    totalScore += s * 10;
                }
                if (!allMatch) {
                    continue;
                }
                if (!hasEllipsisBefore && a_prevStart < 0 && start == 0) {
                    totalScore += 50;
                }
                if (!hasEllipsisAfter && (start + winLen == nInv)) {
                    totalScore += 50;
                }
                if (c == a_preferredOrder) {
                    totalScore += 60;
                }
                if (a_prevStart >= 0) {
                    const int dist = std::abs(static_cast<int>(start) - a_prevStart);
                    if (dist <= 1) {
                        totalScore += 15;
                    } else if (dist <= 4) {
                        totalScore += 8;
                    } else {
                        totalScore -= std::min(dist, 20);
                    }
                }
                if (totalScore > bestCandScore) {
                    bestCandScore = totalScore;
                    bestCand = c;
                    bestStart = start;
                }
            }
        }

        if (bestCand < 0) {
            // Never fabricate unverified neighbor rows or fake N/2 positions if engine uses custom sort order
            const int total = static_cast<int>(realEngine.size());
            const auto win = OverlayLayout::ComputeListWindow(total, mappedSel, maxRows);
            out.totalItemCount = total;
            out.windowStartIndex = win.start;
            out.selectedIndex = win.clampedSelected;
            out.matchedStartIndex = -1;
            out.hasMoreAbove = hasEllipsisBefore || (win.start > 0);
            out.hasMoreBelow = hasEllipsisAfter || (win.end < total);
            for (int i = win.start; i < win.end; ++i) {
                out.windowItems.push_back(std::move(realEngine[static_cast<std::size_t>(i)]));
            }
            return out;
        }

        const auto& winningOrder = orderCandidates[static_cast<std::size_t>(bestCand)];
        const int total = static_cast<int>(nInv);
        const int fullSel = static_cast<int>(bestStart) + mappedSel;
        const auto win = OverlayLayout::ComputeListWindow(total, fullSel, maxRows);
        out.totalItemCount = total;
        out.windowStartIndex = win.start;
        out.selectedIndex = win.clampedSelected;
        out.matchedStartIndex = static_cast<std::int32_t>(bestStart);
        out.matchedOrderIndex = bestCand;
        out.expanded = true;
        out.hasMoreAbove = (win.start > 0);
        out.hasMoreBelow = (win.end < total);
        out.windowItems.reserve(static_cast<std::size_t>(std::max(0, win.end - win.start)));

        for (int i = win.start; i < win.end; ++i) {
            QuickContainerItemEntry entry = a_inventoryItems[winningOrder[static_cast<std::size_t>(i)]];
            if (i >= static_cast<int>(bestStart) && i < static_cast<int>(bestStart + winLen)) {
                const auto& src = realEngine[static_cast<std::size_t>(i) - bestStart];
                entry.name = src.name;
                entry.count = src.count;
                entry.isLegendary = src.isLegendary;
                entry.isFavorite = src.isFavorite;
                entry.isTaggedForSearch = src.isTaggedForSearch;
                entry.isEquipped = src.isEquipped;
                if (entry.itemKind == TargetInfo::ItemKind::None) {
                    entry.itemKind = src.itemKind;
                }
            }
            out.windowItems.push_back(std::move(entry));
        }
        return out;
    }

#ifndef FAP_UNIT_TEST
    namespace
    {
        struct ResolvedClips
        {
            RE::Scaleform::GFx::Value rootClip;
            bool hasRollover{ false };
            RE::Scaleform::GFx::Value rolloverParent;
            RE::Scaleform::GFx::Value rollover;

            bool hasQuickContainer{ false };
            RE::Scaleform::GFx::Value qcParent;
            RE::Scaleform::GFx::Value quickContainer;
            bool qcParentIsRoot{ false };
            bool quickContainerVisible{ false };
        };

        [[nodiscard]] bool TryReadNumber(const RE::Scaleform::GFx::Value& a_val, float& a_out) noexcept
        {
            if (a_val.IsNumber()) {
                const float v = static_cast<float>(a_val.GetNumber());
                if (!std::isfinite(v)) {
                    return false;
                }
                a_out = v;
                return true;
            }
            if (a_val.IsInt()) {
                a_out = static_cast<float>(a_val.GetInt());
                return true;
            }
            if (a_val.IsUInt()) {
                a_out = static_cast<float>(a_val.GetUInt());
                return true;
            }
            return false;
        }

        [[nodiscard]] bool TryReadNumberMember(
            const RE::Scaleform::GFx::Value& a_obj,
            std::string_view a_name1,
            std::string_view a_name2,
            float& a_out)
        {
            if (!a_obj.IsObject()) {
                return false;
            }
            RE::Scaleform::GFx::Value val;
            if (!a_name1.empty() && a_obj.GetMember(a_name1, &val) && TryReadNumber(val, a_out)) {
                return true;
            }
            if (!a_name2.empty() && a_obj.GetMember(a_name2, &val) && TryReadNumber(val, a_out)) {
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsClipVisible(const RE::Scaleform::GFx::Value& a_clip, bool a_ignoreAlpha)
        {
            if (!a_clip.IsObject()) {
                return false;
            }
            RE::Scaleform::GFx::Value visVal;
            if (!a_clip.GetMember("visible"sv, &visVal) || !visVal.IsBoolean() || !visVal.GetBoolean()) {
                return false;
            }
            if (a_ignoreAlpha) {
                RE::Scaleform::GFx::Value shownVal;
                if (a_clip.GetMember("Shown"sv, &shownVal) && shownVal.IsBoolean() && !shownVal.GetBoolean()) {
                    return false;
                }
                if (a_clip.GetMember("bShown"sv, &shownVal) && shownVal.IsBoolean() && !shownVal.GetBoolean()) {
                    return false;
                }
                if (a_clip.GetMember("isVisible"sv, &shownVal) && shownVal.IsBoolean() && !shownVal.GetBoolean()) {
                    return false;
                }
                return true;
            }
            RE::Scaleform::GFx::Value alphaVal;
            float alpha = 1.0f;
            if (a_clip.GetMember("alpha"sv, &alphaVal) && TryReadNumber(alphaVal, alpha)) {
                return alpha > 0.01f;
            }
            return true;
        }

        void ScanContainerForClips(
            const RE::Scaleform::GFx::Value& a_container,
            bool a_isRootContainer,
            bool a_ignoreQcAlpha,
            ResolvedClips& a_out)
        {
            if (!a_container.IsObject()) {
                return;
            }

            if (!a_out.hasRollover) {
                RE::Scaleform::GFx::Value rollover;
                if (a_container.GetMember("RolloverWidget_mc"sv, &rollover) && rollover.IsDisplayObject()) {
                    a_out.rolloverParent = a_container;
                    a_out.rollover = rollover;
                    a_out.hasRollover = true;
                }
            }

            if (!a_out.hasQuickContainer) {
                RE::Scaleform::GFx::Value qc;
                if (a_container.GetMember("QuickContainerWidget_mc"sv, &qc) && qc.IsDisplayObject()) {
                    a_out.qcParent = a_container;
                    a_out.quickContainer = qc;
                    a_out.hasQuickContainer = true;
                    a_out.qcParentIsRoot = a_isRootContainer;
                    a_out.quickContainerVisible = IsClipVisible(qc, a_ignoreQcAlpha);
                }
            }
        }

        void ResolveFromRootObject(const RE::Scaleform::GFx::Value& a_root, bool a_ignoreQcAlpha, ResolvedClips& a_out)
        {
            if (!a_root.IsObject()) {
                return;
            }

            a_out.rootClip = a_root;

            RE::Scaleform::GFx::Value centerGroup;
            if (a_root.GetMember("CenterGroup_mc"sv, &centerGroup) && centerGroup.IsObject()) {
                ScanContainerForClips(centerGroup, false, a_ignoreQcAlpha, a_out);
            }

            RE::Scaleform::GFx::Value hudMenuMc;
            if (a_root.GetMember("HUDMenu_mc"sv, &hudMenuMc) && hudMenuMc.IsObject()) {
                ScanContainerForClips(hudMenuMc, false, a_ignoreQcAlpha, a_out);
            }

            ScanContainerForClips(a_root, true, a_ignoreQcAlpha, a_out);
        }

        [[nodiscard]] bool LocateHUDClips(
            RE::IMenu& a_hudMenu,
            RE::Scaleform::GFx::Movie& a_movie,
            bool a_ignoreQcAlpha,
            ResolvedClips& a_out)
        {
            a_out = {};

            RE::Scaleform::GFx::Value root;
            if (a_movie.GetVariable(&root, "root1")) {
                ResolveFromRootObject(root, a_ignoreQcAlpha, a_out);
                if (a_out.hasRollover || a_out.hasQuickContainer) {
                    return true;
                }
            }

            if (a_movie.GetVariable(&root, "root")) {
                ResolveFromRootObject(root, a_ignoreQcAlpha, a_out);
                if (a_out.hasRollover || a_out.hasQuickContainer) {
                    return true;
                }
            }

            ResolveFromRootObject(a_hudMenu.menuObj, a_ignoreQcAlpha, a_out);
            return a_out.hasRollover || a_out.hasQuickContainer;
        }

        void ConvertStageToParentLocal(
            RE::Scaleform::GFx::Movie& a_movie,
            RE::Scaleform::GFx::Value& a_parent,
            float a_stageX,
            float a_stageY,
            float& a_outLocalX,
            float& a_outLocalY)
        {
            RE::Scaleform::GFx::Value args[2]{
                RE::Scaleform::GFx::Value(static_cast<double>(a_stageX)),
                RE::Scaleform::GFx::Value(static_cast<double>(a_stageY))
            };
            RE::Scaleform::GFx::Value stagePt;
            a_movie.CreateObject(&stagePt, "flash.geom.Point", args, 2);

            RE::Scaleform::GFx::Value localPt;
            if (stagePt.IsObject() &&
                a_parent.Invoke("globalToLocal", &localPt, &stagePt, 1) &&
                localPt.IsObject()) {
                RE::Scaleform::GFx::Value lxVal;
                RE::Scaleform::GFx::Value lyVal;
                float lx = 0.0f;
                float ly = 0.0f;
                if (localPt.GetMember("x"sv, &lxVal) && TryReadNumber(lxVal, lx) &&
                    localPt.GetMember("y"sv, &lyVal) && TryReadNumber(lyVal, ly)) {
                    a_outLocalX = lx;
                    a_outLocalY = ly;
                    return;
                }
            }

            float px = 0.0f;
            float py = 0.0f;
            float psx = 1.0f;
            float psy = 1.0f;
            RE::Scaleform::GFx::Value tmp;
            if (a_parent.GetMember("x"sv, &tmp)) (void)TryReadNumber(tmp, px);
            if (a_parent.GetMember("y"sv, &tmp)) (void)TryReadNumber(tmp, py);
            if (a_parent.GetMember("scaleX"sv, &tmp)) (void)TryReadNumber(tmp, psx);
            if (a_parent.GetMember("scaleY"sv, &tmp)) (void)TryReadNumber(tmp, psy);

            StageToParentLocalFallback(a_stageX, a_stageY, px, py, psx, psy, a_outLocalX, a_outLocalY);
        }

        [[nodiscard]] bool ApplyTransformToClip(
            RE::Scaleform::GFx::Movie& a_movie,
            RE::Scaleform::GFx::Value& a_parent,
            RE::Scaleform::GFx::Value& a_clip,
            const HUDFrameOutput& a_out)
        {
            if (a_out.action == HUDAction::kNoOp) {
                return false;
            }

            bool changed = false;
            if (a_out.applyPosition) {
                float localX = a_out.x;
                float localY = a_out.y;
                if (a_out.action == HUDAction::kApplyFloat) {
                    ConvertStageToParentLocal(a_movie, a_parent, a_out.x, a_out.y, localX, localY);
                }
                a_clip.SetMember("x"sv, RE::Scaleform::GFx::Value(static_cast<double>(localX)));
                a_clip.SetMember("y"sv, RE::Scaleform::GFx::Value(static_cast<double>(localY)));
                changed = true;
            }

            if (a_out.applyAlpha) {
                a_clip.SetMember("alpha"sv, RE::Scaleform::GFx::Value(static_cast<double>(a_out.alpha)));
                changed = true;
            }

            return changed;
        }

        [[nodiscard]] bool TryReadTextField(
            const RE::Scaleform::GFx::Value& a_clip,
            std::string_view a_fieldName,
            std::string& a_out)
        {
            if (!a_clip.IsDisplayObject()) {
                return false;
            }
            RE::Scaleform::GFx::Value tf;
            if (!a_clip.GetMember(a_fieldName, &tf) || !tf.IsDisplayObject()) {
                return false;
            }
            RE::Scaleform::GFx::Value txt;
            if (!tf.GetMember("text"sv, &txt) || !txt.IsString()) {
                return false;
            }
            const char* raw = txt.GetString();
            if (!raw || raw[0] == '\0') {
                return false;
            }
            a_out = raw;
            return true;
        }

        [[nodiscard]] bool TryReadStringMember(
            const RE::Scaleform::GFx::Value& a_obj,
            std::string_view a_name1,
            std::string_view a_name2,
            std::string& a_out)
        {
            if (!a_obj.IsObject()) {
                return false;
            }
            RE::Scaleform::GFx::Value val;
            if (a_obj.GetMember(a_name1, &val) && val.IsString()) {
                const char* s = val.GetString();
                if (s && s[0] != '\0') {
                    a_out = s;
                    return true;
                }
            }
            if (!a_name2.empty() && a_obj.GetMember(a_name2, &val) && val.IsString()) {
                const char* s = val.GetString();
                if (s && s[0] != '\0') {
                    a_out = s;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool TryReadBoolMember(
            const RE::Scaleform::GFx::Value& a_obj,
            std::string_view a_name1,
            std::string_view a_name2,
            bool a_default)
        {
            if (!a_obj.IsObject()) {
                return a_default;
            }
            RE::Scaleform::GFx::Value val;
            if (a_obj.GetMember(a_name1, &val) && val.IsBoolean()) {
                return val.GetBoolean();
            }
            if (!a_name2.empty() && a_obj.GetMember(a_name2, &val) && val.IsBoolean()) {
                return val.GetBoolean();
            }
            return a_default;
        }

        void AppendUniqueButton(std::vector<ButtonHintEntry>& a_out, ButtonHintEntry a_entry)
        {
            if (a_entry.label.empty()) {
                return;
            }
            for (auto& existing : a_out) {
                if (existing.label == a_entry.label &&
                    (existing.key == a_entry.key || existing.key.empty() || a_entry.key.empty())) {
                    if (existing.key.empty() && !a_entry.key.empty()) {
                        existing.key = std::move(a_entry.key);
                        existing.enabled = a_entry.enabled;
                    }
                    return;
                }
            }
            a_out.push_back(std::move(a_entry));
        }

        [[nodiscard]] std::string SanitizeKeyText(std::string_view a_raw)
        {
            const std::size_t nl = a_raw.find_first_of("\r\n");
            if (nl != std::string_view::npos) {
                a_raw = a_raw.substr(0, nl);
            }
            std::string trimmed = TrimWhitespace(a_raw);
            if (trimmed.size() > 16) {
                trimmed.resize(16);
            }
            return TargetInfo::ToUpperAscii(trimmed);
        }

        [[nodiscard]] std::string WideToUtf8(const wchar_t* a_wstr)
        {
            if (!a_wstr || a_wstr[0] == L'\0') {
                return {};
            }
            const int wlen = static_cast<int>(std::wcslen(a_wstr));
            if (wlen <= 0) {
                return {};
            }
            const int len = ::WideCharToMultiByte(CP_UTF8, 0, a_wstr, wlen, nullptr, 0, nullptr, nullptr);
            if (len <= 0) {
                return {};
            }
            std::string out(static_cast<std::size_t>(len), '\0');
            const int written = ::WideCharToMultiByte(CP_UTF8, 0, a_wstr, wlen, out.data(), len, nullptr, nullptr);
            if (written <= 0) {
                return {};
            }
            out.resize(static_cast<std::size_t>(written));
            return out;
        }

        [[nodiscard]] std::wstring Utf8ToWide(std::string_view a_utf8)
        {
            if (a_utf8.empty()) {
                return {};
            }
            const int len = ::MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
            if (len <= 0) {
                return {};
            }
            std::wstring out(static_cast<std::size_t>(len), L'\0');
            const int written = ::MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), out.data(), len);
            if (written <= 0) {
                return {};
            }
            out.resize(static_cast<std::size_t>(written));
            return out;
        }

        [[nodiscard]] std::string TranslateScaleformKey(std::string_view a_raw)
        {
            const std::string trimmed = TrimWhitespace(a_raw);
            if (trimmed.empty() || trimmed.front() != '$') {
                return trimmed;
            }

            static std::mutex s_cacheMutex;
            static std::unordered_map<std::string, std::string> s_translationCache;

            const std::string upperKey = TargetInfo::ToUpperAscii(trimmed);
            {
                std::lock_guard lock{ s_cacheMutex };
                if (const auto it = s_translationCache.find(upperKey); it != s_translationCache.end()) {
                    return it->second;
                }
            }

            bool translatorAvailable = false;
            std::string resolved;
            if (auto* sm = RE::BSScaleformManager::GetSingleton(); sm && sm->loader) {
                struct StateGuard
                {
                    RE::Scaleform::GFx::State* ptr{ nullptr };
                    explicit StateGuard(RE::Scaleform::GFx::State* a_p) noexcept : ptr(a_p) {}
                    StateGuard(const StateGuard&) = delete;
                    StateGuard& operator=(const StateGuard&) = delete;
                    StateGuard(StateGuard&&) = delete;
                    StateGuard& operator=(StateGuard&&) = delete;
                    ~StateGuard()
                    {
                        if (ptr) {
                            ptr->Release();
                        }
                    }
                } guard{ sm->loader->GetStateAddRef(RE::Scaleform::GFx::State::StateType::kTranslator) };

                if (guard.ptr && guard.ptr->GetStateType() == RE::Scaleform::GFx::State::StateType::kTranslator) {
                    translatorAvailable = true;
                    const auto* bsTrans = static_cast<const RE::BSScaleformTranslator*>(guard.ptr);
                    const std::wstring wKey = Utf8ToWide(trimmed);
                    if (!wKey.empty()) {
                        const auto& map = bsTrans->translator.translationMap;
                        const RE::BSFixedStringWCS fixedWKey(wKey.c_str());
                        if (const auto it = map.find(fixedWKey); it != map.end() && !it->second.empty()) {
                            std::string utf8Val = TrimWhitespace(WideToUtf8(it->second.c_str()));
                            if (!utf8Val.empty() && utf8Val.front() != '$') {
                                resolved = std::move(utf8Val);
                            }
                        } else {
                            for (const auto& kv : map) {
                                if (!kv.first.empty() && !kv.second.empty() && ::_wcsicmp(kv.first.c_str(), wKey.c_str()) == 0) {
                                    std::string utf8Val = TrimWhitespace(WideToUtf8(kv.second.c_str()));
                                    if (!utf8Val.empty() && utf8Val.front() != '$') {
                                        resolved = std::move(utf8Val);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (resolved.empty()) {
                resolved = TargetInfo::ResolveTranslationFallback(trimmed);
            }

            if (translatorAvailable) {
                std::lock_guard lock{ s_cacheMutex };
                if (s_translationCache.size() < 512) {
                    s_translationCache[upperKey] = resolved;
                }
            }
            return resolved;
        }

        [[nodiscard]] bool TryExtractHintFromObject(const RE::Scaleform::GFx::Value& a_obj, ButtonHintEntry& a_outEntry)
        {
            if (!a_obj.IsObject()) {
                return false;
            }

            if (a_obj.IsDisplayObject()) {
                RE::Scaleform::GFx::Value visVal;
                if (a_obj.GetMember("visible"sv, &visVal) && visVal.IsBoolean() && !visVal.GetBoolean()) {
                    return false;
                }
            }

            if (!TryReadBoolMember(a_obj, "ButtonVisible"sv, "_bButtonVisible"sv, true)) {
                return false;
            }

            RE::Scaleform::GFx::Value dataObj;
            const bool hasDataObj = a_obj.GetMember("ButtonHintData"sv, &dataObj) && dataObj.IsObject();
            if (hasDataObj && !TryReadBoolMember(dataObj, "ButtonVisible"sv, "_bButtonVisible"sv, true)) {
                return false;
            }

            std::string renderedLabel;
            if (!TryReadTextField(a_obj, "textField_tf"sv, renderedLabel)) {
                (void)TryReadTextField(a_obj, "ButtonText_tf"sv, renderedLabel);
            }

            std::string dataLabel;
            if (hasDataObj) {
                (void)TryReadStringMember(dataObj, "ButtonText"sv, "_strButtonText"sv, dataLabel);
            }
            if (dataLabel.empty()) {
                (void)TryReadStringMember(a_obj, "ButtonText"sv, "_strButtonText"sv, dataLabel);
            }

            std::string rawLabel;
            if (!dataLabel.empty() && dataLabel.front() != '$') {
                rawLabel = std::move(dataLabel);
            } else if (!renderedLabel.empty() && renderedLabel.front() != '$') {
                rawLabel = std::move(renderedLabel);
            } else if (!dataLabel.empty()) {
                rawLabel = std::move(dataLabel);
            } else {
                rawLabel = std::move(renderedLabel);
            }
            if (rawLabel.empty()) {
                return false;
            }
            rawLabel = TranslateScaleformKey(rawLabel);

            std::string rawKey;
            if (hasDataObj) {
                (void)TryReadStringMember(dataObj, "PCKey"sv, "_strPCKey"sv, rawKey);
            }
            if (rawKey.empty()) {
                (void)TryReadStringMember(a_obj, "PCKey"sv, "_strPCKey"sv, rawKey);
            }
            if (rawKey.empty()) {
                RE::Scaleform::GFx::Value iconHolder;
                if (a_obj.GetMember("IconHolderInstance"sv, &iconHolder) && iconHolder.IsObject()) {
                    if (!TryReadTextField(iconHolder, "textField_tf"sv, rawKey)) {
                        (void)TryReadTextField(iconHolder, "Icon_tf"sv, rawKey);
                    }
                    if (rawKey.empty()) {
                        (void)TryReadStringMember(iconHolder, "PCKey"sv, "text"sv, rawKey);
                    }
                }
            }

            const std::string cleanKey = SanitizeKeyText(rawKey);
            auto parsed = TargetInfo::SplitActivateText(rawLabel);
            if (!parsed.verb.empty() && parsed.verb.front() == '$') {
                parsed.verb = TargetInfo::ToUpperAscii(TranslateScaleformKey(parsed.verb));
            }
            a_outEntry.key = cleanKey.empty() ? parsed.keyLabel : cleanKey;
            a_outEntry.label = std::move(parsed.verb);

            const bool disabled =
                TryReadBoolMember(a_obj, "ButtonDisabled"sv, "_bButtonDisabled"sv, false) ||
                (hasDataObj && TryReadBoolMember(dataObj, "ButtonDisabled"sv, "_bButtonDisabled"sv, false));
            const bool enabled =
                TryReadBoolMember(a_obj, "ButtonEnabled"sv, "_bButtonEnabled"sv, true) &&
                (!hasDataObj || TryReadBoolMember(dataObj, "ButtonEnabled"sv, "_bButtonEnabled"sv, true));
            a_outEntry.enabled = enabled && !disabled;

            return !a_outEntry.label.empty();
        }

        void ReadHintArrayMember(
            const RE::Scaleform::GFx::Value& a_owner,
            std::string_view a_name,
            std::vector<ButtonHintEntry>& a_out)
        {
            RE::Scaleform::GFx::Value vec;
            if (!a_owner.GetMember(a_name, &vec) || !vec.IsObject()) {
                return;
            }
            RE::Scaleform::GFx::Value lenVal;
            float lenFloat = 0.0f;
            if (!vec.GetMember("length"sv, &lenVal) || !TryReadNumber(lenVal, lenFloat) || !std::isfinite(lenFloat)) {
                return;
            }
            const int count = static_cast<int>(std::clamp(lenFloat, 0.0f, 16.0f));
            for (int i = 0; i < count; ++i) {
                const std::string idxKey = std::to_string(i);
                RE::Scaleform::GFx::Value entry;
                if (vec.GetMember(idxKey.c_str(), &entry) && entry.IsObject()) {
                    ButtonHintEntry item;
                    if (TryExtractHintFromObject(entry, item)) {
                        AppendUniqueButton(a_out, std::move(item));
                    }
                }
            }
        }

        void ReadHintContainerChildren(RE::Scaleform::GFx::Value& a_container, std::vector<ButtonHintEntry>& a_out)
        {
            if (!a_container.IsDisplayObject()) {
                return;
            }
            float numF = 0.0f;
            if (!TryReadNumberMember(a_container, "numChildren"sv, ""sv, numF) || !std::isfinite(numF)) {
                return;
            }
            const int count = static_cast<int>(std::clamp(numF, 0.0f, 16.0f));
            for (int i = 0; i < count; ++i) {
                RE::Scaleform::GFx::Value idxArg(static_cast<std::int32_t>(i));
                RE::Scaleform::GFx::Value child;
                if (a_container.Invoke("getChildAt", &child, &idxArg, 1) && child.IsObject()) {
                    ButtonHintEntry item;
                    if (TryExtractHintFromObject(child, item)) {
                        AppendUniqueButton(a_out, std::move(item));
                    }
                }
            }
        }

        void ReadButtonHintBar(
            const RE::Scaleform::GFx::Value& a_widget,
            std::vector<ButtonHintEntry>& a_out,
            bool& a_outHasPrimarySource)
        {
            a_out.clear();
            a_outHasPrimarySource = false;
            if (!a_widget.IsObject()) {
                return;
            }

            RE::Scaleform::GFx::Value bar;
            if (a_widget.GetMember("ButtonHintBar_mc"sv, &bar) && IsClipVisible(bar, false)) {
                RE::Scaleform::GFx::Value internalBar;
                if (bar.GetMember("ButtonHintBarInternal_mc"sv, &internalBar) && IsClipVisible(internalBar, false)) {
                    ReadHintContainerChildren(internalBar, a_out);
                }
                if (a_out.empty()) {
                    ReadHintContainerChildren(bar, a_out);
                }
                if (a_out.empty()) {
                    ReadHintArrayMember(bar, "ButtonPoolV"sv, a_out);
                }
                if (a_out.empty()) {
                    ReadHintArrayMember(bar, "_buttonHintDataV"sv, a_out);
                }
                if (a_out.empty()) {
                    ReadHintArrayMember(bar, "ButtonHintDataV"sv, a_out);
                }
                if (a_out.empty()) {
                    ReadHintArrayMember(bar, "_buttonHintData"sv, a_out);
                }
                if (a_out.empty()) {
                    ReadHintArrayMember(bar, "myButtons"sv, a_out);
                }
                if (!a_out.empty()) {
                    a_outHasPrimarySource = true;
                }
            }

            // Append any visible direct button objects exposed on RolloverWidget or QuickContainerWidget
            static constexpr std::array<std::tuple<std::string_view, std::string_view, bool>, 5> kDirectHints{ {
                { "ActivateButton"sv, "E"sv, true },
                { "AButton"sv, "E"sv, true },
                { "XButton"sv, "R"sv, false },
                { "PlayHolotapeButton"sv, "R"sv, false },
                { "YButton"sv, "SPACE"sv, false }
            } };
            for (const auto& [memberName, fallbackKey, isPrimary] : kDirectHints) {
                RE::Scaleform::GFx::Value btn;
                if (a_widget.GetMember(memberName, &btn) && btn.IsObject()) {
                    ButtonHintEntry item;
                    if (TryExtractHintFromObject(btn, item)) {
                        if (item.key.empty()) {
                            item.key = std::string(fallbackKey);
                        }
                        if (isPrimary) {
                            a_outHasPrimarySource = true;
                        }
                        AppendUniqueButton(a_out, std::move(item));
                    }
                }
            }
        }

        struct EngineQuickContainerSnapshot
        {
            bool valid{ false };
            RE::ObjectRefHandle containerRef{};
            RE::ObjectRefHandle inventoryRef{};
            std::string containerName;
            std::string aButtonText;
            std::string perkButtonText;
            std::int32_t selectedClipIndex{ -1 };
            RE::QuickContainerMode mode{ RE::QuickContainerMode::kLoot };
            bool perkButtonEnabled{ false };
            bool isLocked{ false };
            bool buttonAEnabled{ false };
            bool buttonXEnabled{ false };
            std::vector<QuickContainerItemEntry> items;
        };

        void TryReadEngineQuickContainerState(
            EngineQuickContainerSnapshot& a_out,
            RE::TESObjectREFR* a_targetRef,
            bool a_requireMatchingTarget = false)
        {
            a_out = {};
            auto* fm = RE::FavoritesManager::GetSingleton();
            if (!fm) {
                return;
            }

            RE::BSAutoLock lock{ fm->quickContainerMode.dataLock };
            const auto& opt = fm->quickContainerMode.eventDataStruct.optionalValue;
            if (!opt.has_value()) {
                return;
            }

            const auto& qc = opt.value();
            auto contPtr = qc.containerRef.get();
            auto invPtr = qc.inventoryRef.get();
            if (!contPtr && !invPtr) {
                return;
            }
            if (!a_targetRef) {
                if (a_requireMatchingTarget) {
                    return;
                }
            } else {
                const bool exactMatch =
                    (contPtr && contPtr.get() == a_targetRef) ||
                    (invPtr && invPtr.get() == a_targetRef);
                const bool linkedWorkbench =
                    !exactMatch &&
                    (qc.mode == RE::QuickContainerMode::kCrafting ||
                     qc.mode == RE::QuickContainerMode::kWorkshop) &&
                    AnchorManager::ClassifyReference(a_targetRef) == AnchorManager::TargetCategory::kFurnitureTerminal;
                if (!exactMatch && !linkedWorkbench) {
                    return;
                }
            }

            a_out.valid = true;
            a_out.containerRef = qc.containerRef;
            a_out.inventoryRef = qc.inventoryRef;
            a_out.containerName = qc.containerName.empty() ? std::string{} : std::string(qc.containerName.c_str());
            a_out.aButtonText = qc.aButtonText.empty() ? std::string{} : std::string(qc.aButtonText.c_str());
            a_out.perkButtonText = qc.perkButtonText.empty() ? std::string{} : std::string(qc.perkButtonText.c_str());
            a_out.selectedClipIndex = qc.selectedClipIndex;
            a_out.mode = qc.mode.get();
            a_out.perkButtonEnabled = qc.perkButtonEnabled;
            a_out.isLocked = qc.isLocked;
            a_out.buttonAEnabled = qc.buttonAEnabled;
            a_out.buttonXEnabled = qc.buttonXEnabled;

            a_out.items.reserve(qc.itemData.size());
            for (const auto& it : qc.itemData) {
                QuickContainerItemEntry entry;
                entry.name = it.itemName.empty() ? std::string("(Item)") : std::string(it.itemName.c_str());
                entry.count = it.itemCount > 0 ? it.itemCount : 1u;
                entry.isLegendary = it.isLegendary;
                entry.isFavorite = it.isFavorite;
                entry.isTaggedForSearch = it.isTaggedForSearch;
                entry.isEquipped = (it.equipState > 0);
                a_out.items.push_back(std::move(entry));
            }
        }

        [[nodiscard]] bool TryReadPositiveIndex(
            const RE::Scaleform::GFx::Value& a_obj,
            std::string_view a_name,
            int& a_outIdx)
        {
            float val = -1.0f;
            if (a_obj.IsObject() && TryReadNumberMember(a_obj, a_name, ""sv, val) &&
                std::isfinite(val) && val >= -0.1f && val <= 1000000.0f) {
                a_outIdx = static_cast<int>(std::clamp(val + 0.1f, 0.0f, 1000000.0f));
                return true;
            }
            return false;
        }

        [[nodiscard]] bool ReadSingleItemEntry(const RE::Scaleform::GFx::Value& a_itemObj, QuickContainerItemEntry& a_out)
        {
            if (!a_itemObj.IsObject()) {
                return false;
            }
            std::string itemName;
            if (TryReadStringMember(a_itemObj, "text"sv, "itemName"sv, itemName)) {
                a_out.name = std::move(itemName);
            } else {
                a_out.name = "(Item)";
            }

            RE::Scaleform::GFx::Value countVal;
            float countFloat = 1.0f;
            if (a_itemObj.GetMember("count"sv, &countVal) && TryReadNumber(countVal, countFloat) && countFloat >= 1.0f) {
                const float clampedCount = std::clamp(countFloat, 1.0f, 999999.0f);
                a_out.count = static_cast<std::uint32_t>(clampedCount);
            }
            a_out.isLegendary = TryReadBoolMember(a_itemObj, "isLegendary"sv, ""sv, false);
            a_out.isFavorite = TryReadBoolMember(a_itemObj, "favorite"sv, ""sv, false);
            a_out.isTaggedForSearch = TryReadBoolMember(a_itemObj, "taggedForSearch"sv, ""sv, false);

            RE::Scaleform::GFx::Value eqVal;
            float eqFloat = 0.0f;
            if (a_itemObj.GetMember("equipState"sv, &eqVal) && TryReadNumber(eqVal, eqFloat)) {
                a_out.isEquipped = (eqFloat > 0.5f);
            }
            return true;
        }

        [[nodiscard]] std::uint64_t ComputeStackSignature(
            RE::ExtraDataList* a_extraList,
            std::uint32_t a_stackIdx,
            bool& a_outLegendary)
        {
            a_outLegendary = false;
            if (!a_extraList) {
                return 0;
            }

            std::uint64_t sig = 0;
            auto mixSig = [&sig](std::uint64_t v) {
                if (sig == 0) {
                    sig = 14695981039346656037ull;
                }
                sig ^= v;
                sig *= 1099511628211ull;
                if (sig == 0) {
                    sig = 1;
                }
            };

            if (auto* healthEx = a_extraList->GetByType<RE::ExtraHealth>()) {
                const float h = (healthEx->health == 0.0f) ? 0.0f : healthEx->health;
                mixSig(0x1001ull);
                mixSig(std::bit_cast<std::uint32_t>(h));
            }
            if (a_extraList->HasType(RE::EXTRA_DATA_TYPE::kCharge) ||
                a_extraList->HasType(RE::EXTRA_DATA_TYPE::kTimeLeft)) {
                mixSig(0x1002ull);
                mixSig(static_cast<std::uint64_t>(a_stackIdx) + 1ull);
            }
            if (auto* objInst = a_extraList->GetByType<RE::BGSObjectInstanceExtra>()) {
                a_outLegendary = (a_extraList->GetLegendaryMod() != nullptr);
                mixSig(0x1003ull);
                mixSig(objInst->itemIndex);
                if (objInst->values) {
                    for (const auto& modIdx : objInst->GetIndexData()) {
                        const std::uint64_t packed =
                            static_cast<std::uint64_t>(modIdx.objectID) |
                            (static_cast<std::uint64_t>(modIdx.index) << 32) |
                            (static_cast<std::uint64_t>(modIdx.rank) << 40) |
                            (static_cast<std::uint64_t>(modIdx.disabled) << 48);
                        mixSig(packed);
                    }
                }
            }
            if (auto* matSwap = a_extraList->GetByType<RE::ExtraMaterialSwap>(); matSwap && matSwap->swap) {
                mixSig(0x1004ull);
                mixSig(matSwap->swap->GetFormID());
            }

            return sig;
        }

        void EnrichWindowKindsFromMap(
            std::vector<QuickContainerItemEntry>& a_items,
            const std::unordered_map<std::string, TargetInfo::ItemKind>& a_kindMap)
        {
            if (a_kindMap.empty()) {
                return;
            }
            for (auto& item : a_items) {
                if (item.itemKind != TargetInfo::ItemKind::None) {
                    continue;
                }
                const std::string key = CleanUpperKey(item.name);
                if (key.empty()) {
                    continue;
                }
                if (const auto it = a_kindMap.find(key); it != a_kindMap.end()) {
                    item.itemKind = it->second;
                }
            }
        }

        void ReadQuickContainerData(
            Controller& a_controller,
            std::uint32_t a_ownerFormID,
            const RE::Scaleform::GFx::Value& a_qcClip,
            const std::string& a_fallbackHeader,
            const EngineQuickContainerSnapshot& a_engineSnap,
            const Controller::CachedInventorySnapshot& a_invSnap,
            int a_maxVisibleRows,
            bool a_expandInventoryList,
            int a_prevSelectedIdx,
            QuickContainerOverlay& a_out)
        {
            // 1. Header title
            if (!TryReadStringMember(a_qcClip, "containerName"sv, ""sv, a_out.headerTitle)) {
                RE::Scaleform::GFx::Value bracket;
                RE::Scaleform::GFx::Value nameMc;
                if (a_qcClip.GetMember("ListHeaderAndBracket_mc"sv, &bracket) && bracket.IsObject() &&
                    bracket.GetMember("ContainerName_mc"sv, &nameMc) && nameMc.IsObject()) {
                    (void)TryReadTextField(nameMc, "textField_tf"sv, a_out.headerTitle);
                }
            }
            if (a_out.headerTitle.empty() && a_engineSnap.valid && !a_engineSnap.containerName.empty()) {
                a_out.headerTitle = a_engineSnap.containerName;
            }
            if (a_out.headerTitle.empty()) {
                a_out.headerTitle = a_fallbackHeader;
            }

            // 2. Active visible clip probe via ItemClipsA
            RE::Scaleform::GFx::Value listMc;
            const bool hasListMc = a_qcClip.GetMember("ListItems_mc"sv, &listMc) && listMc.IsObject();

            bool hasClipsArr = false;
            int visibleClipsCount = 0;
            int activeClipSlot = -1;
            int activeClipExplicitIdx = -1;
            std::string activeClipText;
            std::uint32_t activeClipCount = 1u;

            RE::Scaleform::GFx::Value clipsArr;
            if (a_qcClip.GetMember("ItemClipsA"sv, &clipsArr) && clipsArr.IsObject()) {
                RE::Scaleform::GFx::Value lenVal;
                float lenFloat = 0.0f;
                if (clipsArr.GetMember("length"sv, &lenVal) && TryReadNumber(lenVal, lenFloat) && std::isfinite(lenFloat)) {
                    const int nClips = static_cast<int>(std::clamp(lenFloat, 0.0f, 16.0f));
                    hasClipsArr = (nClips > 0);
                    for (int i = 0; i < nClips; ++i) {
                        const std::string idxKey = std::to_string(i);
                        RE::Scaleform::GFx::Value clipObj;
                        if (!clipsArr.GetMember(idxKey.c_str(), &clipObj) || !clipObj.IsObject()) {
                            continue;
                        }
                        if (!IsClipVisible(clipObj, false)) {
                            continue;
                        }
                        ++visibleClipsCount;

                        bool isSel = TryReadBoolMember(clipObj, "selected"sv, "_selected"sv, false);
                        if (!isSel) {
                            RE::Scaleform::GFx::Value selInd;
                            if (clipObj.GetMember("SelectionIndicator_mc"sv, &selInd) && selInd.IsObject()) {
                                isSel = IsClipVisible(selInd, false);
                            }
                        }
                        if (isSel && activeClipSlot < 0) {
                            activeClipSlot = i;
                            RE::Scaleform::GFx::Value dataVal;
                            if (clipObj.GetMember("data"sv, &dataVal) && dataVal.IsObject()) {
                                (void)TryReadStringMember(dataVal, "text"sv, "itemName"sv, activeClipText);
                                float cF = 1.0f;
                                if (TryReadNumberMember(dataVal, "count"sv, ""sv, cF) && cF >= 1.0f) {
                                    activeClipCount = static_cast<std::uint32_t>(std::clamp(cF, 1.0f, 999999.0f));
                                }
                            }
                            if (activeClipText.empty()) {
                                (void)TryReadTextField(clipObj, "ItemName_tf"sv, activeClipText);
                            }
                            int expIdx = -1;
                            if (TryReadPositiveIndex(clipObj, "EntryIndex"sv, expIdx) ||
                                TryReadPositiveIndex(clipObj, "entryIndex"sv, expIdx) ||
                                TryReadPositiveIndex(clipObj, "itemIndex"sv, expIdx) ||
                                TryReadPositiveIndex(clipObj, "index"sv, expIdx)) {
                                activeClipExplicitIdx = expIdx;
                            }
                        }
                    }
                }
            }

            // 3. Item visibility & Scaleform arrays
            bool listVisible = true;
            if (a_engineSnap.valid && (a_engineSnap.isLocked || a_engineSnap.items.empty())) {
                listVisible = false;
            } else if (hasListMc && !IsClipVisible(listMc, false) && visibleClipsCount == 0) {
                listVisible = false;
            } else if (!hasListMc && hasClipsArr && visibleClipsCount == 0) {
                listVisible = false;
            }

            auto tryGetNonEmptyArray = [](
                const RE::Scaleform::GFx::Value& a_owner,
                std::string_view a_name,
                RE::Scaleform::GFx::Value& a_outArr,
                int& a_outLen) -> bool
            {
                RE::Scaleform::GFx::Value candidate;
                if (!a_owner.GetMember(a_name, &candidate) || !candidate.IsObject()) {
                    return false;
                }
                RE::Scaleform::GFx::Value lenVal;
                float lenFloat = 0.0f;
                if (!candidate.GetMember("length"sv, &lenVal) || !TryReadNumber(lenVal, lenFloat) ||
                    !std::isfinite(lenFloat) || lenFloat < 0.5f) {
                    return false;
                }
                a_outArr = candidate;
                a_outLen = static_cast<int>(std::clamp(lenFloat, 0.0f, 100000.0f));
                return a_outLen > 0;
            };

            auto findBestMatchingArrayIndex = [&](
                const RE::Scaleform::GFx::Value& a_arr,
                int a_totalCount,
                std::string_view a_targetName,
                std::uint32_t a_targetCount) -> int
            {
                if (a_targetName.empty() || a_totalCount <= 0) {
                    return -1;
                }
                const int searchLimit = std::min(a_totalCount, 4096);
                int bestIdx = -1;
                int bestTier = 0; // 2 = exact count match, 1 = name-only match
                int bestDist = 1000000;
                for (int idx = 0; idx < searchLimit; ++idx) {
                    RE::Scaleform::GFx::Value obj;
                    QuickContainerItemEntry e;
                    const std::string k = std::to_string(idx);
                    if (!a_arr.GetMember(k.c_str(), &obj) || !ReadSingleItemEntry(obj, e)) {
                        continue;
                    }
                    if (!DoItemNamesMatch(e.name, a_targetName)) {
                        continue;
                    }
                    const int tier = (e.count == a_targetCount) ? 2 : 1;
                    const int dist = std::abs(idx - a_prevSelectedIdx);
                    if (tier > bestTier || (tier == bestTier && dist < bestDist)) {
                        bestTier = tier;
                        bestDist = dist;
                        bestIdx = idx;
                    }
                }
                return bestIdx;
            };

            const int maxRows = std::clamp(a_maxVisibleRows, 3, 12);
            const int sfMaxRows = a_expandInventoryList ? maxRows : 5;
            if (listVisible) {
                RE::Scaleform::GFx::Value arr;
                int scaleformCount = 0;
                const bool hasScaleformArr =
                    tryGetNonEmptyArray(a_qcClip, "ItemDataA"sv, arr, scaleformCount) ||
                    (hasListMc && tryGetNonEmptyArray(listMc, "entryList"sv, arr, scaleformCount));

                const bool useEngineArray =
                    !hasScaleformArr && a_engineSnap.valid && !a_engineSnap.items.empty();

                const int totalCount = useEngineArray
                    ? static_cast<int>(a_engineSnap.items.size())
                    : scaleformCount;

                if (totalCount > 0) {
                    // Check ListItems_mc (global index when present) BEFORE QuickContainerWidget_mc slot indices
                    int sfNumericSel = -1;
                    if (!((hasListMc && TryReadPositiveIndex(listMc, "selectedIndex"sv, sfNumericSel)) ||
                          (hasListMc && TryReadPositiveIndex(listMc, "_selectedIndex"sv, sfNumericSel)) ||
                          (hasListMc && TryReadPositiveIndex(listMc, "iSelectedIndex"sv, sfNumericSel)) ||
                          TryReadPositiveIndex(a_qcClip, "selectedIndex"sv, sfNumericSel) ||
                          TryReadPositiveIndex(a_qcClip, "_selectedIndex"sv, sfNumericSel) ||
                          TryReadPositiveIndex(a_qcClip, "selectedClipIndex"sv, sfNumericSel))) {
                        sfNumericSel = -1;
                    }

                    int rawSel = 0;
                    const bool engineHasSel =
                        a_engineSnap.valid &&
                        !a_engineSnap.items.empty() &&
                        a_engineSnap.selectedClipIndex >= 0 &&
                        a_engineSnap.selectedClipIndex < static_cast<int>(a_engineSnap.items.size());

                    if (useEngineArray) {
                        rawSel = engineHasSel ? a_engineSnap.selectedClipIndex : 0;
                    } else if (engineHasSel && totalCount == static_cast<int>(a_engineSnap.items.size())) {
                        rawSel = a_engineSnap.selectedClipIndex;
                    } else if (engineHasSel) {
                        const auto& engItem = a_engineSnap.items[static_cast<std::size_t>(a_engineSnap.selectedClipIndex)];
                        bool sfMatchesEng = false;
                        if (sfNumericSel >= 0 && sfNumericSel < totalCount) {
                            RE::Scaleform::GFx::Value sfObj;
                            QuickContainerItemEntry sfEntry;
                            const std::string k = std::to_string(sfNumericSel);
                            if (arr.GetMember(k.c_str(), &sfObj) && ReadSingleItemEntry(sfObj, sfEntry)) {
                                sfMatchesEng = DoItemNamesMatch(sfEntry.name, engItem.name);
                            }
                        }
                        if (sfMatchesEng) {
                            rawSel = sfNumericSel;
                        } else {
                            const int matchedIdx = findBestMatchingArrayIndex(arr, totalCount, engItem.name, engItem.count);
                            if (matchedIdx >= 0) {
                                rawSel = matchedIdx;
                            } else if (activeClipExplicitIdx >= 0 && activeClipExplicitIdx < totalCount) {
                                rawSel = activeClipExplicitIdx;
                            } else if (sfNumericSel >= 0) {
                                rawSel = sfNumericSel;
                            } else if (activeClipSlot >= 0) {
                                rawSel = activeClipSlot;
                            }
                        }
                    } else if (activeClipExplicitIdx >= 0 && activeClipExplicitIdx < totalCount) {
                        rawSel = activeClipExplicitIdx;
                    } else if (sfNumericSel >= 0) {
                        rawSel = sfNumericSel;
                    } else if (activeClipSlot >= 0) {
                        if (totalCount > visibleClipsCount && !activeClipText.empty()) {
                            const int matchedIdx = findBestMatchingArrayIndex(arr, totalCount, activeClipText, activeClipCount);
                            rawSel = (matchedIdx >= 0) ? matchedIdx : activeClipSlot;
                        } else {
                            rawSel = activeClipSlot;
                        }
                    }

                    bool isFullScaleformList = false;
                    if (!useEngineArray && totalCount > 16) {
                        auto isSlotEllipsis = [&](int a_idx) {
                            const std::string k = std::to_string(a_idx);
                            RE::Scaleform::GFx::Value itemObj;
                            QuickContainerItemEntry probe;
                            return arr.GetMember(k.c_str(), &itemObj) && ReadSingleItemEntry(itemObj, probe) && IsEllipsisItem(probe);
                        };
                        if (!isSlotEllipsis(0) && !isSlotEllipsis(totalCount - 1)) {
                            isFullScaleformList = true;
                        }
                    }

                    if (isFullScaleformList) {
                        // Scaleform overhaul mod already exposed full list; read only visible slice & O(1) enrich icons
                        const auto win = OverlayLayout::ComputeListWindow(totalCount, rawSel, sfMaxRows);
                        a_out.totalItemCount = totalCount;
                        a_out.windowStartIndex = win.start;
                        a_out.selectedIndex = win.clampedSelected;
                        a_out.hasMoreAbove = (win.start > 0);
                        a_out.hasMoreBelow = (win.end < totalCount);
                        a_out.hasExactTotal = true;
                        a_out.items.reserve(static_cast<std::size_t>(std::max(0, win.end - win.start)));
                        for (int i = win.start; i < win.end; ++i) {
                            const std::string idxKey = std::to_string(i);
                            RE::Scaleform::GFx::Value itemObj;
                            QuickContainerItemEntry entry;
                            if (!arr.GetMember(idxKey.c_str(), &itemObj) || !ReadSingleItemEntry(itemObj, entry)) {
                                entry.name = "(Item)";
                            }
                            a_out.items.push_back(std::move(entry));
                        }
                        EnrichWindowKindsFromMap(a_out.items, a_invSnap.kindByCleanUpper);
                    } else {
                        std::vector<QuickContainerItemEntry> rawWindow;
                        const int readLimit = std::min(totalCount, 512);
                        rawWindow.reserve(static_cast<std::size_t>(readLimit));
                        bool hasAnyEllipsis = false;

                        for (int i = 0; i < readLimit; ++i) {
                            QuickContainerItemEntry entry;
                            if (useEngineArray) {
                                entry = a_engineSnap.items[static_cast<std::size_t>(i)];
                            } else {
                                const std::string idxKey = std::to_string(i);
                                RE::Scaleform::GFx::Value itemObj;
                                if (!arr.GetMember(idxKey.c_str(), &itemObj) || !ReadSingleItemEntry(itemObj, entry)) {
                                    entry.name = "(Item)";
                                }
                            }
                            if (IsEllipsisItem(entry)) {
                                hasAnyEllipsis = true;
                            }
                            rawWindow.push_back(std::move(entry));
                        }

                        if (!hasAnyEllipsis && totalCount > 5) {
                            const auto win = OverlayLayout::ComputeListWindow(totalCount, rawSel, sfMaxRows);
                            a_out.totalItemCount = totalCount;
                            a_out.windowStartIndex = win.start;
                            a_out.selectedIndex = win.clampedSelected;
                            a_out.hasMoreAbove = (win.start > 0);
                            a_out.hasMoreBelow = (win.end < totalCount);
                            a_out.hasExactTotal = true;
                            a_out.items.reserve(static_cast<std::size_t>(std::max(0, win.end - win.start)));
                            for (int i = win.start; i < win.end; ++i) {
                                QuickContainerItemEntry entry;
                                if (i < static_cast<int>(rawWindow.size())) {
                                    entry = std::move(rawWindow[static_cast<std::size_t>(i)]);
                                } else {
                                    const std::string idxKey = std::to_string(i);
                                    RE::Scaleform::GFx::Value itemObj;
                                    if (!arr.GetMember(idxKey.c_str(), &itemObj) || !ReadSingleItemEntry(itemObj, entry)) {
                                        entry.name = "(Item)";
                                    }
                                }
                                a_out.items.push_back(std::move(entry));
                            }
                            EnrichWindowKindsFromMap(a_out.items, a_invSnap.kindByCleanUpper);
                        } else {
                            const auto& merged = a_controller.ResolveMergedQuickContainerWindow(
                                a_ownerFormID, a_invSnap, rawWindow, rawSel, maxRows, a_expandInventoryList);
                            a_out.totalItemCount = merged.totalItemCount;
                            a_out.windowStartIndex = merged.windowStartIndex;
                            a_out.selectedIndex = merged.selectedIndex;
                            a_out.hasMoreAbove = merged.hasMoreAbove;
                            a_out.hasMoreBelow = merged.hasMoreBelow;
                            a_out.hasExactTotal = merged.expanded;
                            a_out.items = merged.windowItems;
                        }
                    }
                }
            }

            // 4. Button hints (Scaleform public clips first, then engine fallback)
            bool hasPrimaryBtnSource = false;
            ReadButtonHintBar(a_qcClip, a_out.buttons, hasPrimaryBtnSource);
            if (a_out.buttons.empty() && a_engineSnap.valid) {
                if (!a_engineSnap.aButtonText.empty()) {
                    AppendUniqueButton(a_out.buttons, {
                        "E",
                        TargetInfo::ToUpperAscii(TranslateScaleformKey(a_engineSnap.aButtonText)),
                        a_engineSnap.buttonAEnabled
                    });
                }
                if (a_engineSnap.buttonXEnabled) {
                    AppendUniqueButton(a_out.buttons, { "R", "TRANSFER", true });
                }
                if (!a_engineSnap.perkButtonText.empty() && a_engineSnap.perkButtonEnabled) {
                    AppendUniqueButton(a_out.buttons, {
                        "SPACE",
                        TargetInfo::ToUpperAscii(TranslateScaleformKey(a_engineSnap.perkButtonText)),
                        true
                    });
                }
            }
        }

        void ReadClipBaseTransform(const RE::Scaleform::GFx::Value& a_clip, HUDFrameInput& a_in)
        {
            if (!a_clip.IsObject()) {
                return;
            }
            RE::Scaleform::GFx::Value val;
            if (a_clip.GetMember("x"sv, &val)) (void)TryReadNumber(val, a_in.currentClipX);
            if (a_clip.GetMember("y"sv, &val)) (void)TryReadNumber(val, a_in.currentClipY);
            if (a_clip.GetMember("alpha"sv, &val)) (void)TryReadNumber(val, a_in.currentClipAlpha);
        }

        void ReadClipStageDimensions(
            const RE::Scaleform::GFx::Value& a_rootClip,
            const RE::Scaleform::GFx::Value& a_parent,
            bool a_parentIsRoot,
            const RE::Scaleform::GFx::Value& a_clip,
            float a_defaultW,
            float a_defaultH,
            float& a_outW,
            float& a_outH)
        {
            float w = a_defaultW;
            float h = a_defaultH;
            RE::Scaleform::GFx::Value val;
            if (a_clip.GetMember("width"sv, &val)) {
                float readW = 0.0f;
                if (TryReadNumber(val, readW) && readW > 1.0f) {
                    w = readW;
                }
            }
            if (a_clip.GetMember("height"sv, &val)) {
                float readH = 0.0f;
                if (TryReadNumber(val, readH) && readH > 1.0f) {
                    h = readH;
                }
            }

            float psx = 1.0f;
            float psy = 1.0f;
            if (a_parent.GetMember("scaleX"sv, &val) && TryReadNumber(val, psx) && std::fabs(psx) > 0.05f) {
                w *= std::fabs(psx);
            }
            if (a_parent.GetMember("scaleY"sv, &val) && TryReadNumber(val, psy) && std::fabs(psy) > 0.05f) {
                h *= std::fabs(psy);
            }

            if (!a_parentIsRoot && a_rootClip.IsObject()) {
                float rsx = 1.0f;
                float rsy = 1.0f;
                if (a_rootClip.GetMember("scaleX"sv, &val) && TryReadNumber(val, rsx) && std::fabs(rsx) > 0.05f) {
                    w *= std::fabs(rsx);
                }
                if (a_rootClip.GetMember("scaleY"sv, &val) && TryReadNumber(val, rsy) && std::fabs(rsy) > 0.05f) {
                    h *= std::fabs(rsy);
                }
            }

            a_outW = std::clamp(w, 40.0f, 900.0f);
            a_outH = std::clamp(h, 40.0f, 700.0f);
        }

        [[nodiscard]] Projection::ScreenCoord ProjectWorldPoint(
            const RE::NiPoint3& a_worldPos,
            RE::NiCamera& a_cam,
            const RE::Scaleform::Render::RectF& a_visRect,
            bool a_enableClamping,
            float a_edgeMargin,
            float a_boxW,
            float a_boxH,
            float a_pivotX,
            float a_pivotY)
        {
            float normX = 0.0f;
            float normY = 0.0f;
            if (!a_cam.WorldPtToScreenPt(a_worldPos, normX, normY, 0.0001f)) {
                return {};
            }

            const float stageW = a_visRect.x2 - a_visRect.x1;
            const float stageH = a_visRect.y2 - a_visRect.y1;
            return Projection::ClampAndFadeBoxViewportPos(
                normX,
                normY,
                a_cam.port.left,
                a_cam.port.right,
                a_cam.port.bottom,
                a_cam.port.top,
                a_visRect.x1,
                a_visRect.y1,
                stageW,
                stageH,
                a_boxW,
                a_boxH,
                a_pivotX,
                a_pivotY,
                a_enableClamping,
                a_edgeMargin);
        }

        [[nodiscard]] std::int64_t SteadyNowMs() noexcept
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        }
    }

    void Controller::Install()
    {
        if (!_installed.exchange(true)) {
            if (auto* ui = RE::UI::GetSingleton()) {
                ui->RegisterSink<RE::MenuOpenCloseEvent>(this);
                logger::info("Registered MenuOpenCloseEvent sink for HUDMenu");

                if (auto hudMenu = ui->GetMenu("HUDMenu"sv)) {
                    TryHookHUDMenu(hudMenu.get());
                }
            } else {
                _installed.store(false);
                logger::warn("UI singleton unavailable during Controller::Install");
            }
        }

        EnsurePickRefSink();
    }

    void Controller::EnsurePickRefSink()
    {
        if (_pickSinkRegistered.load(std::memory_order_acquire)) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        if (!_pickSinkRegistered.exchange(true)) {
            static_cast<RE::BSTEventSource<RE::PickRefUpdateEvent>*>(player)->RegisterSink(this);
            logger::info("Registered PickRefUpdateEvent sink on PlayerCharacter");
        }
    }

    void Controller::ClearPickedRef()
    {
        std::lock_guard lock{ _pickMutex };
        _pickedRefHandle.reset();
        _hasPickEvent = false;
    }

    void Controller::InvalidateQuickContainerCacheInternal()
    {
        _cachedQcOwnerFormID = 0;
        _cachedQcSelectedIndex = -1;
        _lastQcSelectedIndex = 0;
        _cachedQcItemName.clear();
        _cachedQcItemCount = 0;
        _cachedQcItemEquipped = false;
        _cachedQcItemLegendary = false;
        _cachedQcItemFavorite = false;
        _cachedQcItemTimeMs = 0;
        _cachedQcItemHasMeta = false;
        _cachedQcItemMeta = {};
        _cachedInvSnapshot.ownerFormID = 0;
        _cachedInvSnapshot.rawListSize = 0;
        _cachedInvSnapshot.rawStackCount = 0;
        _cachedInvSnapshot.rawTotalCountSum = 0;
        _cachedInvSnapshot.timestampMs = 0;
        _cachedInvSnapshot.truncatedByCap = false;
        _cachedInvSnapshot.items.clear();
        _cachedInvSnapshot.kindByCleanUpper.clear();
        _cachedMerge = {};
        _qcHoverItemHash.store(0, std::memory_order_relaxed);
        _qcHoverFirstSeenMs.store(0, std::memory_order_relaxed);
    }

    void Controller::InvalidateQuickContainerCache()
    {
        _pendingQcInvalidate.store(true, std::memory_order_release);
    }

    void Controller::PerformDeferredResets()
    {
        if (_pendingReset.exchange(false, std::memory_order_acq_rel)) {
            _pendingQcInvalidate.store(false, std::memory_order_release);
            _state.Reset();
            _qcState.Reset();
            _cachedTargetFormID = 0;
            _cachedTargetTimeMs = 0;
            _cachedMetadata = {};
            _hasCachedAnchor = false;
            _cachedAnchorFormID = 0;
            _cachedAnchorRoot3D = nullptr;
            _cachedAnchorRefScale = 0;
            InvalidateQuickContainerCacheInternal();
            return;
        }
        if (_pendingQcInvalidate.exchange(false, std::memory_order_acq_rel)) {
            InvalidateQuickContainerCacheInternal();
        }
    }

    void Controller::ClearOverlaySnapshot()
    {
        _hoverTargetKey.store(0, std::memory_order_relaxed);
        _hoverFirstSeenMs.store(0, std::memory_order_relaxed);
        _qcHoverItemHash.store(0, std::memory_order_relaxed);
        _qcHoverFirstSeenMs.store(0, std::memory_order_relaxed);
        _pendingQcInvalidate.store(true, std::memory_order_release);
        std::lock_guard lock{ _overlayMutex };
        _overlaySnapshot = {};
    }

    OverlaySnapshot Controller::GetOverlaySnapshot() const
    {
        std::lock_guard lock{ _overlayMutex };
        return _overlaySnapshot;
    }

    void Controller::ResetState()
    {
        _pendingReset.store(true, std::memory_order_release);
        ClearOverlaySnapshot();
    }

    bool Controller::ResolveQuickContainerSelectedItemMeta(
        RE::TESObjectREFR* a_invOwner,
        const QuickContainerItemEntry& a_item,
        std::int32_t a_selectedIndex,
        TargetInfo::TargetMetadata& a_outMeta)
    {
        if (!a_invOwner || !a_invOwner->inventoryList || a_item.name.empty()) {
            return false;
        }

        const std::uint32_t ownerFormID = a_invOwner->GetFormID();
        const std::int64_t nowMs = SteadyNowMs();
        if (ownerFormID != 0 &&
            _cachedQcItemHasMeta &&
            ownerFormID == _cachedQcOwnerFormID &&
            a_selectedIndex == _cachedQcSelectedIndex &&
            a_item.count == _cachedQcItemCount &&
            a_item.isEquipped == _cachedQcItemEquipped &&
            a_item.isLegendary == _cachedQcItemLegendary &&
            a_item.isFavorite == _cachedQcItemFavorite &&
            a_item.name == _cachedQcItemName &&
            (nowMs - _cachedQcItemTimeMs) < 200) {
            a_outMeta = _cachedQcItemMeta;
            return true;
        }

        const int earlyExitScore = (a_item.stackSignature != 0) ? 390 : 350;
        const std::string targetCleanUpper = CleanUpperKey(a_item.name);
        RE::TESBoundObject* bestObj = nullptr;
        RE::BSTSmartPointer<RE::ExtraDataList> bestExtra;
        std::string bestRawName;

        {
            RE::BSAutoWriteLock lock{ a_invOwner->inventoryList->rwLock };
            int bestScore = 0;

            for (const auto& invItem : a_invOwner->inventoryList->data) {
                if (!invItem.object) {
                    continue;
                }
                const std::string_view baseSv = RE::TESFullName::GetFullName(*invItem.object);
                const std::string baseName = baseSv.empty() ? std::string{} : std::string(baseSv);

                std::uint32_t stackIdx = 0;
                for (auto* stack = invItem.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIdx) {
                    if (stack->GetCount() == 0) {
                        continue;
                    }
                    std::string rawName = baseName;
                    RE::ExtraDataList* stackExtra = stack->extra.get();
                    if (stackExtra) {
                        if (auto* textExtra = stackExtra->GetByType<RE::ExtraTextDisplayData>();
                            textExtra && !textExtra->displayName.empty()) {
                            rawName = textExtra->displayName.c_str();
                        } else {
                            const char* full = const_cast<RE::BGSInventoryItem&>(invItem).GetDisplayFullName(stackIdx);
                            if (full && full[0] != '\0') {
                                rawName = full;
                            }
                        }
                    }

                    int score = 0;
                    if (!rawName.empty() && rawName == a_item.name) {
                        score = 300;
                    } else if (!rawName.empty()) {
                        const std::string stackCleanUpper = CleanUpperKey(rawName);
                        if (!stackCleanUpper.empty() && !targetCleanUpper.empty()) {
                            if (stackCleanUpper == targetCleanUpper) {
                                score = 200;
                            } else if (targetCleanUpper.find(stackCleanUpper) != std::string::npos ||
                                       stackCleanUpper.find(targetCleanUpper) != std::string::npos) {
                                score = 100;
                            }
                        }
                    }

                    if (score > 0) {
                        bool stackLeg = false;
                        const std::uint64_t stackSig = ComputeStackSignature(stackExtra, stackIdx, stackLeg);
                        const bool stackFav = (stackExtra && stackExtra->HasType(RE::EXTRA_DATA_TYPE::kFavorite));
                        if (stack->GetCount() == a_item.count) {
                            score += 20;
                        }
                        if (stack->IsEquipped() == a_item.isEquipped) {
                            score += 10;
                        }
                        if (stackFav == a_item.isFavorite) {
                            score += 10;
                        }
                        if (stackLeg == a_item.isLegendary) {
                            score += 10;
                        }
                        if (a_item.stackSignature != 0 && stackSig == a_item.stackSignature) {
                            score += 40;
                        }
                        if (score > bestScore) {
                            bestScore = score;
                            bestObj = invItem.object;
                            bestExtra = stack->extra;
                            bestRawName = std::move(rawName);
                        }
                    }

                    if (bestScore >= earlyExitScore) {
                        break;
                    }
                }
                if (bestScore >= earlyExitScore) {
                    break;
                }
            }
        }

        TargetInfo::TargetMetadata resolvedMeta;
        bool foundMeta = false;
        if (bestObj) {
            resolvedMeta = TargetInfo::ExtractInventoryItemMetadata(bestObj, bestExtra.get());
            if (!bestRawName.empty()) {
                resolvedMeta.rawDisplayName = bestRawName;
                resolvedMeta.displayName = bestRawName;
            }
            foundMeta = true;
        }

        if (foundMeta) {
            _cachedQcOwnerFormID = ownerFormID;
            _cachedQcSelectedIndex = a_selectedIndex;
            _cachedQcItemName = a_item.name;
            _cachedQcItemCount = a_item.count;
            _cachedQcItemEquipped = a_item.isEquipped;
            _cachedQcItemLegendary = a_item.isLegendary;
            _cachedQcItemFavorite = a_item.isFavorite;
            _cachedQcItemTimeMs = nowMs;
            _cachedQcItemMeta = resolvedMeta;
            _cachedQcItemHasMeta = true;
            a_outMeta = std::move(resolvedMeta);
        } else {
            _cachedQcOwnerFormID = 0;
            _cachedQcItemHasMeta = false;
            _cachedQcItemMeta = {};
        }
        return foundMeta;
    }

    const Controller::CachedInventorySnapshot& Controller::CollectQuickContainerInventory(
        RE::TESObjectREFR* a_invOwner,
        bool a_skipEquipped)
    {
        static const CachedInventorySnapshot kEmptySnapshot{};
        if (!a_invOwner || !a_invOwner->inventoryList) {
            return kEmptySnapshot;
        }

        const std::uint32_t ownerFormID = a_invOwner->GetFormID();
        const std::int64_t nowMs = SteadyNowMs();

        CachedInventorySnapshot nextSnap;
        nextSnap.ownerFormID = ownerFormID;
        nextSnap.skipEquipped = a_skipEquipped;
        nextSnap.timestampMs = nowMs;
        nextSnap.generation = _cachedInvSnapshot.generation + 1;

        {
            RE::BSAutoWriteLock lock{ a_invOwner->inventoryList->rwLock };
            const std::size_t liveListSize = a_invOwner->inventoryList->data.size();
            std::size_t liveStackCount = 0;
            std::uint64_t liveCountSum = 0;
            std::size_t probedItems = 0;
            for (const auto& invItem : a_invOwner->inventoryList->data) {
                if (++probedItems > 2048u) {
                    break;
                }
                for (auto* st = invItem.stackData.get(); st; st = st->nextStack.get()) {
                    ++liveStackCount;
                    liveCountSum += st->GetCount();
                }
            }

            if (ownerFormID != 0 &&
                ownerFormID == _cachedInvSnapshot.ownerFormID &&
                a_skipEquipped == _cachedInvSnapshot.skipEquipped &&
                liveListSize == _cachedInvSnapshot.rawListSize &&
                liveStackCount == _cachedInvSnapshot.rawStackCount &&
                liveCountSum == _cachedInvSnapshot.rawTotalCountSum &&
                (nowMs - _cachedInvSnapshot.timestampMs) < 250) {
                return _cachedInvSnapshot;
            }

            nextSnap.rawListSize = liveListSize;
            nextSnap.rawStackCount = liveStackCount;
            nextSnap.rawTotalCountSum = liveCountSum;
            nextSnap.items.reserve(std::min(liveListSize, kMaxExpandedInventoryItems));

            for (const auto& invItem : a_invOwner->inventoryList->data) {
                if (!invItem.object) {
                    continue;
                }
                const auto kind = TargetInfo::ClassifyBoundObject(invItem.object);
                if (kind == TargetInfo::ItemKind::None ||
                    kind == TargetInfo::ItemKind::Container ||
                    kind == TargetInfo::ItemKind::Door ||
                    kind == TargetInfo::ItemKind::Furniture ||
                    kind == TargetInfo::ItemKind::Terminal ||
                    kind == TargetInfo::ItemKind::Npc) {
                    continue;
                }

                const std::string_view baseSv = RE::TESFullName::GetFullName(*invItem.object);
                const std::string baseName = baseSv.empty() ? std::string{} : std::string(baseSv);
                const std::string cleanBaseKey = baseName.empty() ? std::string{} : CleanUpperKey(baseName);

                if (nextSnap.truncatedByCap) {
                    if (!cleanBaseKey.empty() && nextSnap.kindByCleanUpper.size() < 2048) {
                        nextSnap.kindByCleanUpper.try_emplace(cleanBaseKey, kind);
                    }
                    if (nextSnap.kindByCleanUpper.size() >= 2048) {
                        break;
                    }
                    continue;
                }

                if (!cleanBaseKey.empty() && nextSnap.kindByCleanUpper.size() < 2048) {
                    nextSnap.kindByCleanUpper.try_emplace(cleanBaseKey, kind);
                }

                std::vector<QuickContainerItemEntry> itemStacks;
                std::uint32_t stackIdx = 0;
                for (auto* stack = invItem.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIdx) {
                    const std::uint32_t cnt = stack->GetCount();
                    if (cnt == 0) {
                        continue;
                    }
                    const bool equipped = stack->IsEquipped();
                    if (a_skipEquipped && equipped) {
                        continue;
                    }

                    RE::ExtraDataList* stackExtra = stack->extra.get();
                    const RE::TBO_InstanceData* instData = nullptr;
                    if (stackExtra) {
                        if (auto* instExtra = stackExtra->GetByType<RE::ExtraInstanceData>()) {
                            if (instExtra->data && (!instExtra->base || instExtra->base == invItem.object)) {
                                instData = instExtra->data.get();
                            }
                        }
                    }
                    if (!instData) {
                        instData = invItem.object->GetBaseInstanceData();
                    }
                    if (!invItem.object->GetPlayable(instData)) {
                        continue;
                    }

                    std::string displayName = baseName;
                    const bool favorite = (stackExtra && stackExtra->HasType(RE::EXTRA_DATA_TYPE::kFavorite));
                    if (stackExtra) {
                        if (auto* textExtra = stackExtra->GetByType<RE::ExtraTextDisplayData>();
                            textExtra && !textExtra->displayName.empty()) {
                            displayName = textExtra->displayName.c_str();
                        } else {
                            const char* full = const_cast<RE::BGSInventoryItem&>(invItem).GetDisplayFullName(stackIdx);
                            if (full && full[0] != '\0') {
                                displayName = full;
                            }
                        }
                    }
                    if (displayName.empty()) {
                        continue;
                    }

                    if (nextSnap.kindByCleanUpper.size() < 2048) {
                        const std::string cleanKey = CleanUpperKey(displayName);
                        if (!cleanKey.empty()) {
                            nextSnap.kindByCleanUpper.try_emplace(cleanKey, kind);
                        }
                    }

                    bool legendary = false;
                    const std::uint64_t stackSig = ComputeStackSignature(stackExtra, stackIdx, legendary);

                    QuickContainerItemEntry entry;
                    entry.name = std::move(displayName);
                    entry.count = cnt;
                    entry.isLegendary = legendary;
                    entry.isFavorite = favorite;
                    entry.isEquipped = equipped;
                    entry.itemKind = kind;
                    entry.stackSignature = stackSig;
                    itemStacks.push_back(std::move(entry));
                }

                AggregateItemStacks(itemStacks);
                for (auto& stEntry : itemStacks) {
                    if (nextSnap.items.size() >= kMaxExpandedInventoryItems) {
                        nextSnap.truncatedByCap = true;
                        std::vector<QuickContainerItemEntry>().swap(nextSnap.items);
                        break;
                    }
                    nextSnap.items.push_back(std::move(stEntry));
                }
            }
        }

        _cachedInvSnapshot = std::move(nextSnap);
        return _cachedInvSnapshot;
    }

    const QuickContainerMergeResult& Controller::ResolveMergedQuickContainerWindow(
        std::uint32_t a_ownerFormID,
        const CachedInventorySnapshot& a_invSnap,
        const std::vector<QuickContainerItemEntry>& a_rawWindow,
        std::int32_t a_rawSel,
        int a_maxRows,
        bool a_expandEnabled)
    {
        if (a_ownerFormID == 0 || a_ownerFormID != _cachedMerge.stickyOwnerFormID) {
            _cachedMerge.stickyOwnerFormID = a_ownerFormID;
            _cachedMerge.stickyInvGen = a_invSnap.generation;
            _cachedMerge.lockedOrderIndex = -1;
            _cachedMerge.lastMatchedStart = -1;
            _cachedMerge.stickyFallback = false;
        } else if (a_invSnap.generation != _cachedMerge.stickyInvGen) {
            _cachedMerge.stickyInvGen = a_invSnap.generation;
            _cachedMerge.lockedOrderIndex = -1;
            if (_cachedMerge.lastMatchedStart >= 0 && !a_invSnap.items.empty()) {
                _cachedMerge.lastMatchedStart = std::min<std::int32_t>(
                    _cachedMerge.lastMatchedStart,
                    static_cast<std::int32_t>(a_invSnap.items.size() - 1));
            } else {
                _cachedMerge.lastMatchedStart = -1;
            }
        }

        std::size_t rawHash = std::hash<std::size_t>{}(a_rawWindow.size());
        bool windowHasEllipsis = false;
        std::size_t realCount = 0;
        for (const auto& it : a_rawWindow) {
            if (IsEllipsisItem(it)) {
                windowHasEllipsis = true;
            } else {
                ++realCount;
            }
            rawHash ^= std::hash<std::string>{}(it.name) + 0x9e3779b9u + (rawHash << 6) + (rawHash >> 2);
            rawHash ^= std::hash<std::uint32_t>{}(it.count) + 0x9e3779b9u + (rawHash << 6) + (rawHash >> 2);
            rawHash ^= std::hash<bool>{}(it.isEquipped) + 0x9e3779b9u;
        }

        if (a_ownerFormID != 0 &&
            a_ownerFormID == _cachedMerge.ownerFormID &&
            a_invSnap.generation == _cachedMerge.invGeneration &&
            rawHash == _cachedMerge.rawWindowHash &&
            a_rawSel == _cachedMerge.rawSel &&
            a_maxRows == _cachedMerge.maxRows &&
            a_expandEnabled == _cachedMerge.expandEnabled) {
            return _cachedMerge.result;
        }

        QuickContainerMergeResult res;
        if (!a_expandEnabled) {
            res = MergeQuickContainerInventory(a_rawWindow, a_rawSel, {}, 5);
            EnrichWindowKindsFromMap(res.windowItems, a_invSnap.kindByCleanUpper);
        } else {
            const bool skipExpansion = a_invSnap.truncatedByCap || _cachedMerge.stickyFallback;
            res = skipExpansion
                ? MergeQuickContainerInventory(a_rawWindow, a_rawSel, {}, a_maxRows)
                : MergeQuickContainerInventory(
                    a_rawWindow, a_rawSel, a_invSnap.items, a_maxRows,
                    _cachedMerge.lockedOrderIndex, _cachedMerge.lastMatchedStart);

            if (skipExpansion) {
                EnrichWindowKindsFromMap(res.windowItems, a_invSnap.kindByCleanUpper);
            } else if (res.expanded) {
                _cachedMerge.lockedOrderIndex = res.matchedOrderIndex;
                _cachedMerge.lastMatchedStart = res.matchedStartIndex;
            } else {
                const bool hadPrevStart = (_cachedMerge.lastMatchedStart >= 0);
                _cachedMerge.lastMatchedStart = -1;
                if (!a_invSnap.items.empty() &&
                    a_invSnap.items.size() > realCount &&
                    (windowHasEllipsis || (realCount >= 5 && hadPrevStart))) {
                    _cachedMerge.stickyFallback = true;
                }
            }
        }

        _cachedMerge.ownerFormID = a_ownerFormID;
        _cachedMerge.invGeneration = a_invSnap.generation;
        _cachedMerge.rawWindowHash = rawHash;
        _cachedMerge.rawSel = a_rawSel;
        _cachedMerge.maxRows = a_maxRows;
        _cachedMerge.expandEnabled = a_expandEnabled;
        _cachedMerge.result = std::move(res);
        return _cachedMerge.result;
    }

    TargetInfo::TargetMetadata Controller::ResolveTargetMetadata(RE::TESObjectREFR* a_targetRef)
    {
        if (!a_targetRef) {
            return {};
        }

        const std::uint32_t formID = a_targetRef->GetFormID();
        const std::int64_t nowMs = SteadyNowMs();
        const bool needsRefresh =
            formID == 0 ||
            formID != _cachedTargetFormID ||
            (_cachedMetadata.isNpc && (nowMs - _cachedTargetTimeMs) >= 150);

        if (needsRefresh) {
            _cachedMetadata = TargetInfo::ExtractTargetMetadata(a_targetRef);
            _cachedTargetFormID = formID;
            _cachedTargetTimeMs = nowMs;
        }

        return _cachedMetadata;
    }

    RE::NiPoint3 Controller::ResolveStableAnchorWorldPosition(
        RE::TESObjectREFR* a_ref,
        RE::NiAVObject* a_targeted3D,
        const ConfigData& a_cfg)
    {
        if (!a_ref) {
            return {};
        }

        const auto category = AnchorManager::ClassifyReference(a_ref);
        const std::uint32_t formID = a_ref->GetFormID();
        if (category != AnchorManager::TargetCategory::kFurnitureTerminal || formID == 0) {
            return AnchorManager::ResolveAnchorWorldPosition(a_ref, a_targeted3D, a_cfg);
        }

        const void* rootPtr = a_ref->Get3D();
        const RE::NiPoint3 pos = a_ref->GetPosition();
        const RE::NiPoint3 ang{ a_ref->data.angle.x, a_ref->data.angle.y, a_ref->data.angle.z };
        const std::uint16_t refScale = a_ref->refScale;
        const Vector3 offset = a_cfg.furnitureOffset;

        auto nearEq = [](float a, float b, float eps) noexcept {
            return std::fabs(a - b) <= eps;
        };

        if (rootPtr != nullptr &&
            _hasCachedAnchor &&
            formID == _cachedAnchorFormID &&
            rootPtr == _cachedAnchorRoot3D &&
            refScale == _cachedAnchorRefScale &&
            nearEq(pos.x, _cachedAnchorRefPos.x, 0.01f) &&
            nearEq(pos.y, _cachedAnchorRefPos.y, 0.01f) &&
            nearEq(pos.z, _cachedAnchorRefPos.z, 0.01f) &&
            nearEq(ang.x, _cachedAnchorRefAngle.x, 0.001f) &&
            nearEq(ang.y, _cachedAnchorRefAngle.y, 0.001f) &&
            nearEq(ang.z, _cachedAnchorRefAngle.z, 0.001f) &&
            nearEq(offset.x, _cachedAnchorOffset.x, 0.001f) &&
            nearEq(offset.y, _cachedAnchorOffset.y, 0.001f) &&
            nearEq(offset.z, _cachedAnchorOffset.z, 0.001f)) {
            return _cachedAnchorWorldPos;
        }

        bool usedGeometry = false;
        const RE::NiPoint3 resolved = AnchorManager::ResolveAnchorWorldPosition(
            a_ref,
            a_targeted3D,
            a_cfg,
            &usedGeometry);
        if (rootPtr != nullptr && usedGeometry) {
            _cachedAnchorFormID = formID;
            _cachedAnchorRoot3D = rootPtr;
            _cachedAnchorRefPos = pos;
            _cachedAnchorRefAngle = ang;
            _cachedAnchorRefScale = refScale;
            _cachedAnchorOffset = offset;
            _cachedAnchorWorldPos = resolved;
            _hasCachedAnchor = true;
        } else {
            _hasCachedAnchor = false;
        }
        return resolved;
    }

    RE::NiPointer<RE::TESObjectREFR> Controller::GetPickedRef(bool& a_outHasPickEvent) const
    {
        RE::ObjectRefHandle handle;
        {
            std::lock_guard lock{ _pickMutex };
            a_outHasPickEvent = _hasPickEvent;
            handle = _pickedRefHandle;
        }

        if (!handle) {
            return nullptr;
        }
        return handle.get();
    }

    void Controller::TryHookHUDMenu(RE::IMenu* a_hudMenu)
    {
        if (!a_hudMenu) {
            return;
        }

        if (_hooked.exchange(true)) {
            return;
        }

        const auto vtableAddr = *reinterpret_cast<const std::uintptr_t*>(a_hudMenu);
        _AdvanceMovie = REL::Relocation<std::uintptr_t>{ vtableAddr }.write_vfunc(4, &Hook_AdvanceMovie);
        logger::info("Hooked HUDMenu::AdvanceMovie at vtable {:X}", vtableAddr);
    }

    RE::BSEventNotifyControl Controller::ProcessEvent(
        const RE::MenuOpenCloseEvent& a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
    {
        if (a_event.menuName == "HUDMenu"sv) {
            ResetState();
            if (a_event.opening) {
                EnsurePickRefSink();
                if (auto* ui = RE::UI::GetSingleton()) {
                    if (auto hudMenu = ui->GetMenu("HUDMenu"sv)) {
                        TryHookHUDMenu(hudMenu.get());
                    }
                }
            }
        } else if (a_event.menuName == "PauseMenu"sv && !a_event.opening) {
            if (!Config::GetSingleton().HasUnsavedChanges()) {
                Config::GetSingleton().LoadAll();
            }
        } else if (a_event.opening) {
            if (a_event.menuName == "DialogueMenu"sv ||
                a_event.menuName == "VATSMenu"sv ||
                a_event.menuName == "WorkshopMenu"sv ||
                a_event.menuName == "LockpickingMenu"sv ||
                a_event.menuName == "TerminalMenu"sv ||
                a_event.menuName == "PipboyMenu"sv ||
                a_event.menuName == "ContainerMenu"sv ||
                a_event.menuName == "BarterMenu"sv ||
                a_event.menuName == "ExamineMenu"sv ||
                a_event.menuName == "PauseMenu"sv ||
                a_event.menuName == "LoadingMenu"sv ||
                a_event.menuName == "MainMenu"sv ||
                a_event.menuName == "SleepWaitMenu"sv ||
                a_event.menuName == "FaderMenu"sv) {
                ClearOverlaySnapshot();
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl Controller::ProcessEvent(
        const RE::PickRefUpdateEvent& a_event,
        RE::BSTEventSource<RE::PickRefUpdateEvent>*)
    {
        std::lock_guard lock{ _pickMutex };
        _pickedRefHandle = a_event.pickRef;
        _hasPickEvent = true;
        return RE::BSEventNotifyControl::kContinue;
    }

    void Controller::Hook_AdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time)
    {
        if (_AdvanceMovie.address() != 0) {
            _AdvanceMovie(a_this, a_timeDelta, a_time);
        }

        if (a_this && a_this->menuName == "HUDMenu"sv && a_this->uiMovie) {
            if (GetSingleton().OnHUDAdvance(a_this, a_timeDelta)) {
                a_this->uiMovie->Capture(true);
            }
        }
    }

    bool Controller::OnHUDAdvance(RE::IMenu* a_hudMenu, float a_timeDelta)
    {
        PerformDeferredResets();

        if (!a_hudMenu || !a_hudMenu->uiMovie || !a_hudMenu->uiMovie->GetVisible()) {
            ClearOverlaySnapshot();
            return false;
        }

        auto& movie = *a_hudMenu->uiMovie;
        ResolvedClips clips;
        const bool ignoreQcAlpha = _qcState.IsHidingAlpha();
        if (!LocateHUDClips(*a_hudMenu, movie, ignoreQcAlpha, clips)) {
            ClearOverlaySnapshot();
            return false;
        }

        const ConfigData cfg = Config::GetSingleton().GetSnapshot();
        auto* player = RE::PlayerCharacter::GetSingleton();
        const bool inPowerArmor = player && RE::PowerArmor::ActorInPowerArmor(*player);
        const bool bypassActive = inPowerArmor && cfg.powerArmorBypass;
        const auto visRect = movie.GetVisibleFrameRect();
        const float stageW = visRect.x2 - visRect.x1;
        const float stageH = visRect.y2 - visRect.y1;

        RE::NiPointer<RE::TESObjectREFR> pickedSmartPtr;
        RE::TESObjectREFR* targetRef = nullptr;
        RE::NiAVObject* targeted3DHint = nullptr;
        auto* cam = RE::Main::WorldRootCamera();

        if (cfg.enabled && !bypassActive && player) {
            auto* targeted3D = player->targeted3D.get();
            bool hasPickEvent = false;
            pickedSmartPtr = GetPickedRef(hasPickEvent);
            targetRef = pickedSmartPtr.get();
            if (!targetRef && !hasPickEvent) {
                targetRef = AnchorManager::ResolveRefFrom3D(targeted3D);
            }

            if (targeted3D && targetRef && AnchorManager::Does3DBelongToRef(targeted3D, targetRef)) {
                targeted3DHint = targeted3D;
            }
        }

        EngineQuickContainerSnapshot preEngineQc;
        bool hasPreEngineQc = false;
        if (clips.quickContainerVisible && clips.hasRollover &&
            IsClipVisible(clips.rollover, _state.IsHidingAlpha())) {
            TryReadEngineQuickContainerState(preEngineQc, targetRef, true);
            hasPreEngineQc = true;
            if (!preEngineQc.valid) {
                clips.quickContainerVisible = false;
            }
        }

        bool changed = false;
        OverlaySnapshot nextOverlay{};

        // 1. Rollover prompt controller + metadata publishing
        if (clips.hasRollover) {
            HUDFrameInput in{};
            in.enabled = cfg.enabled;
            in.inPowerArmor = inPowerArmor;
            in.powerArmorBypass = cfg.powerArmorBypass;
            in.smoothingSpeed = cfg.smoothingSpeed;
            in.deltaSeconds = a_timeDelta;
            in.quickContainerVisible = clips.quickContainerVisible;
            in.hideNativeClip = (cfg.promptStyle == 1 && MenuFrameworkUI::IsHudOverlayActive());

            RE::Scaleform::GFx::Value visVal;
            in.rolloverVisible =
                clips.rollover.GetMember("visible"sv, &visVal) &&
                visVal.IsBoolean() &&
                visVal.GetBoolean();
            ReadClipBaseTransform(clips.rollover, in);

            if (in.enabled && !bypassActive && !in.quickContainerVisible && in.rolloverVisible && targetRef && cam) {
                in.hasTarget = true;
                const RE::NiPoint3 worldAnchor =
                    ResolveStableAnchorWorldPosition(targetRef, targeted3DHint, cfg);
                in.projectedTarget = ProjectWorldPoint(
                    worldAnchor, *cam, visRect,
                    cfg.enableClamping, cfg.edgeMargin,
                    0.0f, 0.0f, 0.0f, 0.0f);
            }

            const auto out = _state.Step(in);
            if (ApplyTransformToClip(movie, clips.rolloverParent, clips.rollover, out)) {
                changed = true;
            }

            const bool rolloverFloatingVisible =
                (out.action == HUDAction::kApplyFloat && in.projectedTarget.visible &&
                 !clips.quickContainerVisible && targetRef && stageW > 0.0f && stageH > 0.0f);

            if (!rolloverFloatingVisible || clips.quickContainerVisible || bypassActive || !cfg.enabled) {
                _hoverTargetKey.store(0, std::memory_order_relaxed);
                _hoverFirstSeenMs.store(0, std::memory_order_relaxed);
            }

            if (rolloverFloatingVisible) {
                const std::int64_t nowMs = SteadyNowMs();
                const std::uint32_t formId = targetRef->GetFormID();
                const std::uintptr_t targetKey = (formId != 0)
                    ? static_cast<std::uintptr_t>(formId)
                    : reinterpret_cast<std::uintptr_t>(targetRef);
                std::int64_t firstSeen = _hoverFirstSeenMs.load(std::memory_order_relaxed);
                if (targetKey != _hoverTargetKey.load(std::memory_order_relaxed) || firstSeen == 0) {
                    _hoverTargetKey.store(targetKey, std::memory_order_relaxed);
                    firstSeen = nowMs;
                    _hoverFirstSeenMs.store(firstSeen, std::memory_order_relaxed);
                }

                nextOverlay.visible = true;
                nextOverlay.quickContainerVisible = false;
                nextOverlay.normScreenX = std::clamp((out.x - visRect.x1) / stageW, 0.0f, 1.0f);
                nextOverlay.normScreenY = std::clamp((out.y - visRect.y1) / stageH, 0.0f, 1.0f);
                nextOverlay.edgeFade = in.projectedTarget.alpha;
                nextOverlay.updateTimeMs = nowMs;
                nextOverlay.targetFirstSeenMs = firstSeen;
                nextOverlay.target = ResolveTargetMetadata(targetRef);

                std::vector<ButtonHintEntry> hints;
                bool hasPrimaryHintSource = false;
                ReadButtonHintBar(clips.rollover, hints, hasPrimaryHintSource);
                if (!hasPrimaryHintSource) {
                    std::string btnText;
                    if (TryReadTextField(clips.rollover, "RolloverButton_tf"sv, btnText)) {
                        btnText = TranslateScaleformKey(btnText);
                        auto parsed = TargetInfo::SplitActivateText(btnText);
                        if (!parsed.verb.empty() && parsed.verb.front() == '$') {
                            parsed.verb = TargetInfo::ToUpperAscii(TranslateScaleformKey(parsed.verb));
                        }
                        if (!parsed.verb.empty()) {
                            ButtonHintEntry primary;
                            primary.key = parsed.keyLabel.empty() ? std::string("E") : parsed.keyLabel;
                            primary.label = parsed.verb;
                            primary.enabled = true;
                            bool alreadyPresent = false;
                            for (const auto& existing : hints) {
                                if (existing.label == primary.label) {
                                    alreadyPresent = true;
                                    break;
                                }
                            }
                            if (!alreadyPresent) {
                                hints.insert(hints.begin(), std::move(primary));
                            }
                        }
                    }
                }
                if (!hints.empty()) {
                    nextOverlay.actionKey = hints[0].key;
                    nextOverlay.actionVerb = hints[0].label;
                    if (hints.size() >= 2) {
                        nextOverlay.secondaryKey = hints[1].key;
                        nextOverlay.secondaryVerb = hints[1].label;
                    }
                    nextOverlay.actions = std::move(hints);
                }
                if (nextOverlay.actionVerb.empty()) {
                    if (!nextOverlay.target.actionVerb.empty() && nextOverlay.target.actionVerb.front() == '$') {
                        nextOverlay.target.actionVerb = TargetInfo::ToUpperAscii(TranslateScaleformKey(nextOverlay.target.actionVerb));
                    }
                    nextOverlay.actionVerb = nextOverlay.target.actionVerb;
                }
                if (nextOverlay.actions.empty() && !nextOverlay.actionVerb.empty()) {
                    nextOverlay.actions.push_back({
                        nextOverlay.actionKey.empty() ? std::string("E") : nextOverlay.actionKey,
                        nextOverlay.actionVerb,
                        true
                    });
                }

                if (nextOverlay.target.displayName.empty()) {
                    std::string rolloverName;
                    if (TryReadTextField(clips.rollover, "RolloverName_tf"sv, rolloverName)) {
                        nextOverlay.target.displayName = std::move(rolloverName);
                    }
                }
                if (nextOverlay.target.rawDisplayName.empty() && !nextOverlay.target.displayName.empty()) {
                    nextOverlay.target.rawDisplayName = nextOverlay.target.displayName;
                }
            }
        }

        // 2. Floating & Modern QuickContainer / Workbench controller
        if (clips.hasQuickContainer) {
            const bool modernQcActive = (cfg.promptStyle == 1 && cfg.modernQuickContainer && MenuFrameworkUI::IsHudOverlayActive());

            HUDFrameInput qcIn{};
            qcIn.enabled = cfg.enabled && (cfg.floatQuickContainer || modernQcActive);
            qcIn.inPowerArmor = inPowerArmor;
            qcIn.powerArmorBypass = cfg.powerArmorBypass;
            qcIn.quickContainerVisible = false;
            qcIn.rolloverVisible = clips.quickContainerVisible;
            qcIn.smoothingSpeed = cfg.quickContainerSmoothing;
            qcIn.deadzonePx = cfg.quickContainerDeadzone;
            qcIn.hideNativeClip = modernQcActive;
            qcIn.deltaSeconds = a_timeDelta;
            ReadClipBaseTransform(clips.quickContainer, qcIn);

            if (qcIn.enabled && !bypassActive && qcIn.rolloverVisible) {
                if (!cfg.floatQuickContainer && modernQcActive) {
                    qcIn.hasTarget = true;
                    qcIn.projectedTarget = { visRect.x1 + 0.54f * stageW, visRect.y1 + 0.45f * stageH, 1.0f, true };
                } else if (targetRef && cam) {
                    qcIn.hasTarget = true;

                    ConfigData qcCfg = cfg;
                    qcCfg.doorOffset = cfg.quickContainerOffset;
                    qcCfg.npcOffset = cfg.quickContainerOffset;
                    qcCfg.containerOffset = cfg.quickContainerOffset;
                    qcCfg.furnitureOffset = cfg.quickContainerOffset;
                    qcCfg.itemOffset = cfg.quickContainerOffset;

                    const RE::NiPoint3 qcAnchor =
                        ResolveStableAnchorWorldPosition(targetRef, targeted3DHint, qcCfg);

                    float qcW = 260.0f;
                    float qcH = 240.0f;
                    ReadClipStageDimensions(
                        clips.rootClip, clips.qcParent, clips.qcParentIsRoot,
                        clips.quickContainer, 260.0f, 240.0f, qcW, qcH);

                    qcIn.projectedTarget = ProjectWorldPoint(
                        qcAnchor, *cam, visRect,
                        cfg.enableClamping, cfg.edgeMargin,
                        qcW, qcH, 0.0f, 0.15f);
                }
            }

            const auto qcOut = _qcState.Step(qcIn);
            HUDFrameOutput qcApplyOut = qcOut;
            if (!cfg.floatQuickContainer && qcApplyOut.action == HUDAction::kApplyFloat) {
                qcApplyOut.applyPosition = false;
            }
            if (ApplyTransformToClip(movie, clips.qcParent, clips.quickContainer, qcApplyOut)) {
                changed = true;
            }

            const bool publishFloatQc =
                (qcOut.action == HUDAction::kApplyFloat && qcIn.projectedTarget.visible);
            const bool publishGraceQc =
                (qcOut.action == HUDAction::kNoOp && _qcState.IsHidingAlpha() && _qcState.HasLastScreen());

            if (modernQcActive && clips.quickContainerVisible && (publishFloatQc || publishGraceQc) &&
                stageW > 0.0f && stageH > 0.0f) {
                EngineQuickContainerSnapshot engineSnap;
                if (hasPreEngineQc) {
                    engineSnap = std::move(preEngineQc);
                } else {
                    TryReadEngineQuickContainerState(engineSnap, targetRef, cfg.floatQuickContainer);
                }

                const TargetInfo::TargetMetadata targetMeta =
                    targetRef ? ResolveTargetMetadata(targetRef) : TargetInfo::TargetMetadata{};

                auto invPtr = engineSnap.valid ? engineSnap.inventoryRef.get() : RE::NiPointer<RE::TESObjectREFR>{};
                auto contPtr = engineSnap.valid ? engineSnap.containerRef.get() : RE::NiPointer<RE::TESObjectREFR>{};
                RE::TESObjectREFR* invOwner = invPtr ? invPtr.get() : (contPtr ? contPtr.get() : targetRef);
                const std::uint32_t ownerFormID = invOwner ? invOwner->GetFormID() : 0u;

                const bool isWorkbench =
                    (targetMeta.itemKind == TargetInfo::ItemKind::Furniture) ||
                    (engineSnap.valid &&
                     (engineSnap.mode == RE::QuickContainerMode::kCrafting ||
                      engineSnap.mode == RE::QuickContainerMode::kWorkshop));
                const bool isLockedOrEmpty =
                    engineSnap.valid &&
                    (engineSnap.isLocked || engineSnap.items.empty());

                bool anyEngineEquipped = false;
                bool engineHasEllipsis = false;
                if (engineSnap.valid) {
                    for (const auto& engItem : engineSnap.items) {
                        if (engItem.isEquipped) {
                            anyEngineEquipped = true;
                        }
                        if (IsEllipsisItem(engItem)) {
                            engineHasEllipsis = true;
                        }
                    }
                }
                const bool skipEquipped =
                    engineSnap.valid &&
                    (engineSnap.mode == RE::QuickContainerMode::kStealing) &&
                    !anyEngineEquipped;

                static const CachedInventorySnapshot kEmptyInvSnap{};
                const bool shouldCollectInv =
                    !isWorkbench &&
                    !isLockedOrEmpty &&
                    invOwner != nullptr &&
                    (cfg.qcShowItemIcons ||
                     (cfg.qcExpandInventoryList &&
                      (!engineSnap.valid || engineHasEllipsis || engineSnap.items.size() >= 5)));
                const auto& invSnap = shouldCollectInv
                    ? CollectQuickContainerInventory(invOwner, skipEquipped)
                    : kEmptyInvSnap;

                ReadQuickContainerData(
                    *this,
                    ownerFormID,
                    clips.quickContainer,
                    targetMeta.displayName,
                    engineSnap,
                    invSnap,
                    cfg.qcMaxVisibleRows,
                    cfg.qcExpandInventoryList,
                    _lastQcSelectedIndex,
                    nextOverlay.quickContainer);
                _lastQcSelectedIndex = nextOverlay.quickContainer.selectedIndex;

                const float screenX = publishFloatQc ? qcOut.x : _qcState.LastScreen().x;
                const float screenY = publishFloatQc ? qcOut.y : _qcState.LastScreen().y;
                nextOverlay.quickContainer.active = true;
                nextOverlay.quickContainer.docked = !cfg.floatQuickContainer;
                nextOverlay.quickContainer.isWorkbench = isWorkbench;
                nextOverlay.quickContainer.normScreenX = cfg.floatQuickContainer
                    ? std::clamp((screenX - visRect.x1) / stageW, 0.0f, 1.0f)
                    : 0.54f;
                nextOverlay.quickContainer.normScreenY = cfg.floatQuickContainer
                    ? std::clamp((screenY - visRect.y1) / stageH, 0.0f, 1.0f)
                    : 0.45f;
                nextOverlay.quickContainer.edgeFade = cfg.floatQuickContainer
                    ? (publishFloatQc ? qcIn.projectedTarget.alpha : _qcState.LastScreen().alpha)
                    : 1.0f;

                if (cfg.infoCardInQuickContainer && cfg.infoCardPosition != 0 && !nextOverlay.quickContainer.items.empty()) {
                    const int localIdx = std::clamp(
                        nextOverlay.quickContainer.selectedIndex - nextOverlay.quickContainer.windowStartIndex,
                        0,
                        static_cast<int>(nextOverlay.quickContainer.items.size()) - 1);
                    const auto& selItem = nextOverlay.quickContainer.items[static_cast<std::size_t>(localIdx)];

                    const std::uint32_t ownerId = invOwner ? invOwner->GetFormID() : 0u;
                    std::size_t itemHash = std::hash<std::uint32_t>{}(ownerId);
                    itemHash ^= std::hash<std::int32_t>{}(nextOverlay.quickContainer.selectedIndex) + 0x9e3779b9u + (itemHash << 6) + (itemHash >> 2);
                    itemHash ^= std::hash<std::string>{}(selItem.name) + 0x9e3779b9u + (itemHash << 6) + (itemHash >> 2);
                    itemHash ^= std::hash<std::uint32_t>{}(selItem.count) + 0x9e3779b9u + (itemHash << 6) + (itemHash >> 2);
                    if (itemHash == 0) {
                        itemHash = 1;
                    }

                    const std::int64_t nowMs = SteadyNowMs();
                    std::int64_t firstSeen = _qcHoverFirstSeenMs.load(std::memory_order_relaxed);
                    if (itemHash != _qcHoverItemHash.load(std::memory_order_relaxed) || firstSeen == 0) {
                        _qcHoverItemHash.store(itemHash, std::memory_order_relaxed);
                        firstSeen = nowMs;
                        _qcHoverFirstSeenMs.store(firstSeen, std::memory_order_relaxed);
                    }
                    nextOverlay.quickContainer.selectedItemFirstSeenMs = firstSeen;

                    TargetInfo::TargetMetadata selMeta;
                    if (ResolveQuickContainerSelectedItemMeta(invOwner, selItem, nextOverlay.quickContainer.selectedIndex, selMeta)) {
                        nextOverlay.quickContainer.hasSelectedItemMeta = true;
                        nextOverlay.quickContainer.selectedItemMeta = std::move(selMeta);
                    }
                }

                nextOverlay.visible = true;
                nextOverlay.quickContainerVisible = true;
                nextOverlay.updateTimeMs = SteadyNowMs();
            } else {
                InvalidateQuickContainerCache();
            }
        } else {
            InvalidateQuickContainerCache();
        }

        {
            std::lock_guard lock{ _overlayMutex };
            _overlaySnapshot = std::move(nextOverlay);
        }

        return changed;
    }
#endif
}

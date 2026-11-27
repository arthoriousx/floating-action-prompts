#include "HUDController.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <initializer_list>
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
    namespace
    {
        [[nodiscard]] constexpr char ToUpperAsciiChar(char a_ch) noexcept
        {
            return (a_ch >= 'a' && a_ch <= 'z')
                ? static_cast<char>(a_ch - ('a' - 'A'))
                : a_ch;
        }

        [[nodiscard]] bool ContainsAsciiCaseInsensitive(std::string_view a_haystack, std::string_view a_needle) noexcept
        {
            if (a_needle.empty() || a_haystack.size() < a_needle.size()) {
                return false;
            }
            const std::size_t limit = a_haystack.size() - a_needle.size();
            for (std::size_t i = 0; i <= limit; ++i) {
                bool match = true;
                for (std::size_t j = 0; j < a_needle.size(); ++j) {
                    if (ToUpperAsciiChar(a_haystack[i + j]) != ToUpperAsciiChar(a_needle[j])) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return true;
                }
            }
            return false;
        }
    }

    bool IsHealActionVerb(std::string_view a_verb) noexcept
    {
        return ContainsAsciiCaseInsensitive(a_verb, "HEAL") ||
               ContainsAsciiCaseInsensitive(a_verb, "STIMPAK") ||
               ContainsAsciiCaseInsensitive(a_verb, "REVIVE");
    }

    bool IsCrimeActionVerb(std::string_view a_verb) noexcept
    {
        const auto first = a_verb.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return false;
        }
        const auto last = a_verb.find_last_not_of(" \t\r\n");
        const std::string_view trimmed = a_verb.substr(first, last - first + 1);

        auto eqIgnoreCase = [](std::string_view a_lhs, std::string_view a_rhs) noexcept {
            if (a_lhs.size() != a_rhs.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a_lhs.size(); ++i) {
                if (ToUpperAsciiChar(a_lhs[i]) != ToUpperAsciiChar(a_rhs[i])) {
                    return false;
                }
            }
            return true;
        };

        auto startsWithIgnoreCase = [&](std::string_view a_prefix) noexcept {
            return trimmed.size() >= a_prefix.size() &&
                   eqIgnoreCase(trimmed.substr(0, a_prefix.size()), a_prefix);
        };

        return eqIgnoreCase(trimmed, "STEAL") ||
               startsWithIgnoreCase("STEAL ") ||
               eqIgnoreCase(trimmed, "PICKPOCKET") ||
               startsWithIgnoreCase("PICKPOCKET ") ||
               eqIgnoreCase(trimmed, "TRESPASS");
    }

    namespace
    {
        [[nodiscard]] bool VerbMatchesToken(std::string_view a_verb, std::string_view a_token) noexcept
        {
            if (a_token.empty() || a_verb.empty()) {
                return false;
            }
            const auto first = a_verb.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                return false;
            }
            const auto last = a_verb.find_last_not_of(" \t\r\n");
            const std::string_view v = a_verb.substr(first, last - first + 1);
            if (v.size() < a_token.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a_token.size(); ++i) {
                if (ToUpperAsciiChar(v[i]) != ToUpperAsciiChar(a_token[i])) {
                    return false;
                }
            }
            return v.size() == a_token.size() ||
                   v[a_token.size()] == ' ' ||
                   v[a_token.size()] == ':' ||
                   v[a_token.size()] == '(' ||
                   v[a_token.size()] == '-';
        }
    }

    bool IsRangedNpcActionVerb(std::string_view a_verb) noexcept
    {
        static constexpr std::array<std::string_view, 11> kRangedVerbs{
            "INTIMIDAT",
            "PACIF",
            "INCITE",
            "COMMAND",
            "OVERTURE",
            "HOLDUP",
            "HOLD UP",
            "SURRENDER",
            "THREATEN",
            "HACK",
            "ROBB"
        };
        if (VerbMatchesToken(a_verb, "ROB")) {
            return true;
        }
        for (const auto kw : kRangedVerbs) {
            if (ContainsAsciiCaseInsensitive(a_verb, kw)) {
                return true;
            }
        }
        return false;
    }

    bool IsCloseRangeNpcActionVerb(std::string_view a_verb) noexcept
    {
        static constexpr std::array<std::string_view, 13> kCloseVerbs{
            "TALK",
            "PICKPOCKET",
            "STEAL",
            "TRADE",
            "BARTER",
            "SEARCH",
            "LOOT",
            "HEAL",
            "STIMPAK",
            "REVIVE",
            "CANNIBAL",
            "FEED",
            "SANDMAN"
        };
        for (const auto kw : kCloseVerbs) {
            if (ContainsAsciiCaseInsensitive(a_verb, kw)) {
                return true;
            }
        }
        return false;
    }

    bool ShouldSuppressForAiming(
        int a_aimingMode,
        bool a_isAiming,
        bool a_isNpcTarget,
        bool a_isQuickContainer) noexcept
    {
        if (!a_isAiming) {
            return false;
        }
        if (a_aimingMode == 2) {
            return true;
        }
        if (a_aimingMode == 1) {
            return a_isNpcTarget && !a_isQuickContainer;
        }
        return false;
    }

    bool IsAimingWithCooldown(
        bool a_rawAiming,
        std::int64_t a_nowMs,
        std::int64_t& a_lastAimingMs,
        std::int64_t a_cooldownMs) noexcept
    {
        if (a_rawAiming) {
            a_lastAimingMs = a_nowMs;
            return true;
        }
        if (a_cooldownMs > 0 &&
            a_lastAimingMs > 0 &&
            a_nowMs >= a_lastAimingMs &&
            (a_nowMs - a_lastAimingMs) < a_cooldownMs) {
            return true;
        }
        return false;
    }

    Vector3 ComputeWaterPromptAnchor(
        const Vector3& a_playerPos,
        float a_playerYawRad,
        const Vector3& a_camPos,
        const Vector3& a_camForward,
        float a_waterZ,
        float a_maxHorizDist) noexcept
    {
        const float horizMax = std::clamp(
            std::isfinite(a_maxHorizDist) ? a_maxHorizDist : 105.0f,
            45.0f,
            160.0f);
        const float fwdX = std::sin(a_playerYawRad);
        const float fwdY = std::cos(a_playerYawRad);

        float offsetX = fwdX * std::min(72.0f, horizMax);
        float offsetY = fwdY * std::min(72.0f, horizMax);

        if (std::isfinite(a_camForward.x) &&
            std::isfinite(a_camForward.y) &&
            std::isfinite(a_camForward.z) &&
            a_camForward.z < -0.08f) {
            const float t = (a_waterZ - a_camPos.z) / a_camForward.z;
            if (std::isfinite(t) && t > 10.0f && t < 500.0f) {
                const float hx = (a_camPos.x + a_camForward.x * t) - a_playerPos.x;
                const float hy = (a_camPos.y + a_camForward.y * t) - a_playerPos.y;
                const float hLen = std::hypot(hx, hy);
                if (std::isfinite(hLen) && hLen > 1.0f) {
                    const float clampedDist = std::clamp(hLen, 35.0f, horizMax);
                    offsetX = (hx / hLen) * clampedDist;
                    offsetY = (hy / hLen) * clampedDist;
                }
            }
        }

        return {
            a_playerPos.x + offsetX,
            a_playerPos.y + offsetY,
            a_waterZ + 12.0f
        };
    }

    bool FilterNpcPromptActions(
        std::vector<ButtonHintEntry>& a_actions,
        bool a_isNonCompanionLiveNpc,
        float a_distance,
        bool a_limitEnabled,
        float a_maxDistance,
        bool a_keepRangedActions,
        float a_rangedMaxDistance) noexcept
    {
        if (!a_isNonCompanionLiveNpc || !a_limitEnabled) {
            return false;
        }
        const float maxDist = (std::isfinite(a_maxDistance) && a_maxDistance >= 0.0f)
            ? a_maxDistance
            : 185.0f;
        if (std::isfinite(a_distance) && a_distance <= maxDist) {
            return false;
        }
        if (!a_keepRangedActions) {
            a_actions.clear();
            return true;
        }
        const float rangedLimit = (std::isfinite(a_rangedMaxDistance) && a_rangedMaxDistance > 0.0f)
            ? std::max(maxDist, a_rangedMaxDistance)
            : 0.0f;
        if (rangedLimit > 0.0f && (!std::isfinite(a_distance) || a_distance > rangedLimit)) {
            a_actions.clear();
            return true;
        }
        a_actions.erase(
            std::remove_if(a_actions.begin(), a_actions.end(),
                [](const ButtonHintEntry& e) noexcept {
                    return !IsRangedNpcActionVerb(e.label);
                }),
            a_actions.end());
        return a_actions.empty();
    }

    bool StepFeaturedExamineGuard(
        bool& a_waitInitialRelease,
        const FeaturedExamineInput& a_in) noexcept
    {
        if (!a_in.isFeaturedExamineOpen || !a_in.isDismissEvent) {
            return false;
        }
        const std::int64_t graceMs = std::clamp<std::int64_t>(a_in.graceMs, 0, 5000);
        if (graceMs <= 0) {
            a_waitInitialRelease = false;
            return false;
        }
        const std::int64_t ageMs = (a_in.openTimeMs > 0 && a_in.nowMs >= a_in.openTimeMs)
            ? (a_in.nowMs - a_in.openTimeMs)
            : 0;

        if (a_in.isReleased || !a_in.isPressed) {
            if (a_waitInitialRelease) {
                a_waitInitialRelease = false;
                return true;
            }
            if (graceMs > 0 && a_in.openTimeMs > 0 && ageMs < graceMs) {
                return true;
            }
            return false;
        }

        if (a_waitInitialRelease) {
            const std::int64_t recoverMs = std::max<std::int64_t>(graceMs, 150);
            if (a_in.isJustPressed && a_in.openTimeMs > 0 && ageMs >= recoverMs) {
                a_waitInitialRelease = false;
                return false;
            }
            return true;
        }

        if (graceMs > 0 && a_in.openTimeMs > 0 && ageMs < graceMs) {
            return true;
        }
        return false;
    }

    float ScoreThirdPersonCandidate(
        const ThirdPersonAssistCandidate& a_cand,
        float a_maxDist,
        float a_minCosHalfAngle) noexcept
    {
        if (!std::isfinite(a_cand.dist) || !std::isfinite(a_cand.forwardDot) ||
            !std::isfinite(a_maxDist) || a_maxDist <= 1.0f ||
            a_cand.dist < 0.0f || a_cand.dist > a_maxDist) {
            return -1.0f;
        }
        if (a_cand.forwardDot < a_minCosHalfAngle) {
            return -1.0f;
        }

        const float distScore = 1.0f - std::clamp(a_cand.dist / a_maxDist, 0.0f, 1.0f);
        const float angleRange = std::max(0.05f, 1.0f - a_minCosHalfAngle);
        const float angleScore = std::clamp((a_cand.forwardDot - a_minCosHalfAngle) / angleRange, 0.0f, 1.0f);

        float score = 0.0f;
        if (a_cand.hasCrosshairDot && std::isfinite(a_cand.crosshairDot)) {
            const float crosshairScore = std::clamp((a_cand.crosshairDot - 0.15f) / 0.85f, 0.0f, 1.0f);
            score = crosshairScore * 0.52f + distScore * 0.30f + angleScore * 0.18f;
            if (a_cand.isLooseItem) {
                score += 0.10f;
            }
        } else {
            score = distScore * 0.58f + angleScore * 0.42f;
            if (a_cand.isLooseItem) {
                score += 0.14f;
            }
        }
        if (a_cand.isCurrentAssistTarget) {
            score += 0.25f;
        }
        return score;
    }

    bool IsSolidWallCollisionLayer(std::uint8_t a_layer) noexcept
    {
        switch (a_layer) {
        case 1:   // COL_LAYER::kStatic
        case 2:   // COL_LAYER::kAnimStatic
        case 13:  // COL_LAYER::kTerrain
        case 17:  // COL_LAYER::kGround
        case 27:  // COL_LAYER::kInvisibleWall
        case 35:  // COL_LAYER::kCollisionBox
            return true;
        default:
            return false;
        }
    }

    bool IsThirdPersonAssistLosClear(
        bool a_hasHit,
        bool a_hitTargetOrPlayer,
        float a_hitFraction,
        float a_rayDist,
        float a_targetDeltaZ,
        bool a_hitSolidWall,
        bool a_allowWallRecess) noexcept
    {
        if (!a_hasHit || a_hitTargetOrPlayer) {
            return true;
        }
        if (!std::isfinite(a_hitFraction) || !std::isfinite(a_rayDist) || !std::isfinite(a_targetDeltaZ)) {
            return false;
        }
        const float clampedFrac = std::clamp(a_hitFraction, 0.0f, 1.0f);
        const float remainingDist = a_rayDist * (1.0f - clampedFrac);
        if (a_hitSolidWall) {
            return a_allowWallRecess && (remainingDist <= 6.0f);
        }
        if (a_targetDeltaZ < -12.0f) {
            return false;
        }
        return remainingDist <= 4.0f;
    }

    bool IsDisallowedAssistTargetNameOrVerb(
        std::string_view a_displayName,
        std::string_view a_verb,
        bool a_isActivatorForm) noexcept
    {
        auto trimStr = [](std::string_view a_sv) -> std::string {
            const auto first = a_sv.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = a_sv.find_last_not_of(" \t\r\n");
            return std::string(a_sv.substr(first, last - first + 1));
        };
        const std::string upperName = TargetInfo::ToUpperAscii(trimStr(a_displayName));
        if (upperName == "DEFAULT DUMMY" ||
            upperName == "DUMMY" ||
            upperName == "OIL" ||
            upperName == "OIL SLICK" ||
            upperName == "OIL POOL" ||
            upperName == "TRIGGER" ||
            upperName == "MARKER" ||
            upperName == "ACTIVATOR" ||
            upperName == "COLLISION") {
            return true;
        }
        if (a_isActivatorForm && !a_verb.empty()) {
            const std::string upperVerb = TargetInfo::ToUpperAscii(trimStr(a_verb));
            if (upperVerb == "ACTIVATE") {
                return true;
            }
        }
        return false;
    }

    bool ShouldKeepLockedThirdPersonTarget(
        std::uint32_t a_lockedFormId,
        std::int64_t a_lockStartMs,
        std::int64_t a_nowMs,
        std::int64_t a_lockMs,
        bool a_lockedStillValid,
        bool a_crosshairHasTarget) noexcept
    {
        if (a_lockedFormId == 0 || a_lockMs <= 0 || !a_lockedStillValid || a_crosshairHasTarget) {
            return false;
        }
        if (a_nowMs < a_lockStartMs) {
            return false;
        }
        return (a_nowMs - a_lockStartMs) < a_lockMs;
    }

    bool StepPostHoldMenuGuard(
        PostHoldMenuGuardState& a_state,
        const PostHoldMenuGuardInput& a_in) noexcept
    {
        if (!a_state.armed) {
            return false;
        }

        if (!a_in.inMenuMode) {
            if (a_state.menuObserved || (a_in.nowMs - a_state.triggerMs) > a_in.armWindowMs) {
                a_state = {};
                return false;
            }
            if (a_state.waitRelease && a_in.isReleased &&
                (a_in.isMatchingPhysicalKey || a_in.isConfirmFamilyEvent)) {
                a_state.waitRelease = false;
                a_state.releaseMs = a_in.nowMs;
            }
            return false;
        }

        if (!a_state.menuObserved) {
            if ((a_in.nowMs - a_state.triggerMs) > a_in.armWindowMs) {
                a_state = {};
                return false;
            }
            a_state.menuObserved = true;
            a_state.menuOpenMs = a_in.nowMs;
        }

        const bool isTargetInput = a_in.isMatchingPhysicalKey || a_in.isConfirmFamilyEvent;
        if (!isTargetInput) {
            return false;
        }

        if (a_state.waitRelease) {
            if (a_in.isReleased) {
                a_state.waitRelease = false;
                a_state.releaseMs = a_in.nowMs;
            }
            return true;
        }

        const std::int64_t graceAnchor = std::max(a_state.menuOpenMs, a_state.releaseMs);
        if ((a_in.nowMs - graceAnchor) < a_in.postReleaseGraceMs) {
            return true;
        }

        a_state = {};
        return false;
    }

    bool ShouldThirdPersonAssistOverrideCrosshair(
        int a_mode,
        bool a_hasCrosshairTarget,
        bool a_crosshairIsQuickContainer,
        bool a_crosshairIsLooseItem,
        float a_crosshairDist,
        float a_assistMaxDist) noexcept
    {
        if (a_crosshairIsQuickContainer) {
            return false;
        }
        if (!a_hasCrosshairTarget) {
            return true;
        }
        if (a_mode == 1) {
            const float limit = std::max(a_assistMaxDist, 155.0f);
            return !a_crosshairIsLooseItem &&
                   (!std::isfinite(a_crosshairDist) || a_crosshairDist > limit);
        }
        return false;
    }

    bool DoesActionRequireHold(
        std::string_view a_verb,
        bool a_isCrime,
        TargetInfo::ItemKind a_targetKind,
        bool a_isLoadDoor,
        const ConfigData& a_cfg,
        bool a_isWaterTarget) noexcept
    {
        if (!a_cfg.holdToActivateEnabled || a_verb.empty()) {
            return false;
        }

        if (a_cfg.holdDrink &&
            (VerbMatchesToken(a_verb, "DRINK") ||
             (a_isWaterTarget && VerbMatchesToken(a_verb, "ACTIVATE")))) {
            return true;
        }
        if (a_cfg.holdSitSleep &&
            (VerbMatchesToken(a_verb, "SIT") ||
             VerbMatchesToken(a_verb, "SLEEP") ||
             VerbMatchesToken(a_verb, "REST") ||
             VerbMatchesToken(a_verb, "LAY"))) {
            return true;
        }
        if (a_cfg.holdStealPickpocket &&
            (VerbMatchesToken(a_verb, "STEAL") ||
             VerbMatchesToken(a_verb, "PICKPOCKET") ||
             (a_isCrime && a_targetKind != TargetInfo::ItemKind::Door))) {
            return true;
        }
        if (a_cfg.holdEatCannibal &&
            (VerbMatchesToken(a_verb, "EAT") ||
             VerbMatchesToken(a_verb, "CANNIBAL") ||
             VerbMatchesToken(a_verb, "DEVOUR") ||
             VerbMatchesToken(a_verb, "CONSUME"))) {
            return true;
        }
        if (a_cfg.holdCraftPowerArmor) {
            if (VerbMatchesToken(a_verb, "CRAFT") ||
                VerbMatchesToken(a_verb, "COOK") ||
                VerbMatchesToken(a_verb, "WORKSHOP")) {
                return true;
            }
            if (VerbMatchesToken(a_verb, "ENTER") &&
                !a_isLoadDoor &&
                (a_targetKind == TargetInfo::ItemKind::Furniture ||
                 a_targetKind == TargetInfo::ItemKind::Armor ||
                 a_targetKind == TargetInfo::ItemKind::None)) {
                return true;
            }
            if (VerbMatchesToken(a_verb, "USE") &&
                a_targetKind == TargetInfo::ItemKind::Furniture) {
                return true;
            }
        }

        if (!a_cfg.holdCustomVerbs.empty()) {
            std::string_view csv = a_cfg.holdCustomVerbs;
            std::size_t pos = 0;
            while (pos <= csv.size()) {
                const std::size_t comma = csv.find(',', pos);
                std::string_view tok = (comma == std::string_view::npos)
                    ? csv.substr(pos)
                    : csv.substr(pos, comma - pos);
                const auto f = tok.find_first_not_of(" \t\r\n");
                if (f != std::string_view::npos) {
                    const auto l = tok.find_last_not_of(" \t\r\n");
                    tok = tok.substr(f, l - f + 1);
                    if (VerbMatchesToken(a_verb, tok)) {
                        return true;
                    }
                }
                if (comma == std::string_view::npos) {
                    break;
                }
                pos = comma + 1;
            }
        }

        return false;
    }

    HoldToActivateDecision StepHoldToActivate(
        HoldToActivateState& a_state,
        const HoldToActivateInput& a_in,
        std::int64_t a_nowMs,
        std::int64_t a_durationMs,
        std::int64_t a_staleHoldTimeoutMs) noexcept
    {
        if (a_in.isReleased || !a_in.isPressed) {
            const bool wasActiveOrCompleted = a_state.active || a_state.completedWaitingRelease;
            a_state = {};
            return { wasActiveOrCompleted, false, 0.0f };
        }

        if (!a_in.requiresHold) {
            if (a_state.completedWaitingRelease) {
                return { true, false, 0.0f };
            }
            if (a_state.active) {
                a_state.active = false;
                a_state.progress = 0.0f;
            }
            return { false, false, 0.0f };
        }

        if (a_state.active &&
            a_state.lastSeenMs > 0 &&
            a_nowMs > a_state.lastSeenMs &&
            (a_nowMs - a_state.lastSeenMs) > a_staleHoldTimeoutMs) {
            a_state = {};
        }

        if (a_state.completedWaitingRelease) {
            a_state.lastSeenMs = a_nowMs;
            return { true, false, 1.0f };
        }

        if (!a_state.active) {
            if (!a_in.isJustPressed) {
                return { false, false, 0.0f };
            }
            a_state.active = true;
            a_state.completedWaitingRelease = false;
            a_state.targetKey = a_in.targetKey;
            a_state.startMs = a_nowMs;
            a_state.lastSeenMs = a_nowMs;
            a_state.progress = 0.0f;
            if (a_durationMs <= 0) {
                a_state.active = false;
                a_state.completedWaitingRelease = true;
                a_state.progress = 1.0f;
                return { true, true, 1.0f };
            }
            return { true, false, 0.0f };
        }

        if (a_in.targetKey != a_state.targetKey) {
            // Target shifted mid-hold: cancel trigger until user releases & presses again
            a_state.active = false;
            a_state.completedWaitingRelease = true;
            a_state.progress = 0.0f;
            a_state.lastSeenMs = a_nowMs;
            return { true, false, 0.0f };
        }

        a_state.lastSeenMs = a_nowMs;
        const std::int64_t elapsed = std::max<std::int64_t>(0, a_nowMs - a_state.startMs);
        if (elapsed >= a_durationMs || a_durationMs <= 0) {
            a_state.active = false;
            a_state.completedWaitingRelease = true;
            a_state.progress = 1.0f;
            return { true, true, 1.0f };
        }

        a_state.progress = std::clamp(
            static_cast<float>(elapsed) / static_cast<float>(a_durationMs),
            0.0f,
            0.999f);
        return { true, false, a_state.progress };
    }

    SyntheticHoldTapAction StepSyntheticHoldTap(
        int& a_stage,
        const void*& a_queueHead,
        std::int64_t& a_triggerMs,
        const void* a_curQueueHead,
        bool a_isReleased,
        std::int64_t a_nowMs,
        std::int64_t a_timeoutMs) noexcept
    {
        if (a_stage == 0) {
            return SyntheticHoldTapAction::kInactive;
        }

        if (a_stage == 1) {
            if (a_queueHead == a_curQueueHead) {
                if (a_isReleased) {
                    a_stage = 0;
                    a_queueHead = nullptr;
                    a_triggerMs = 0;
                    return SyntheticHoldTapAction::kSwallowAndResetHold;
                }
                return SyntheticHoldTapAction::kPassPress;
            }
            if (a_triggerMs > 0 && a_nowMs > a_triggerMs && (a_nowMs - a_triggerMs) > a_timeoutMs) {
                a_stage = 0;
                a_queueHead = nullptr;
                a_triggerMs = 0;
                return a_isReleased
                    ? SyntheticHoldTapAction::kSwallowAndResetHold
                    : SyntheticHoldTapAction::kSwallow;
            }
            if (a_isReleased) {
                a_stage = 3;
                a_queueHead = a_curQueueHead;
                return SyntheticHoldTapAction::kPassReleaseAndResetHold;
            }
            a_stage = 2;
            a_queueHead = a_curQueueHead;
            return SyntheticHoldTapAction::kPassRelease;
        }

        if (a_stage == 2) {
            // Check current queueHead BEFORE a_isReleased so PC/UI/HUD receive MC's synthetic key-up!
            if (a_queueHead == a_curQueueHead) {
                return SyntheticHoldTapAction::kPassRelease;
            }
            if (a_triggerMs > 0 && a_nowMs > a_triggerMs && (a_nowMs - a_triggerMs) > a_timeoutMs) {
                a_stage = 0;
                a_queueHead = nullptr;
                a_triggerMs = 0;
                return a_isReleased
                    ? SyntheticHoldTapAction::kSwallowAndResetHold
                    : SyntheticHoldTapAction::kSwallow;
            }
            if (a_isReleased) {
                a_stage = 0;
                a_queueHead = nullptr;
                a_triggerMs = 0;
                return SyntheticHoldTapAction::kSwallowAndResetHold;
            }
            return SyntheticHoldTapAction::kSwallow;
        }

        if (a_stage == 3) {
            // Drain natural release across all input receivers on the same queueHead
            if (a_queueHead == a_curQueueHead) {
                return SyntheticHoldTapAction::kPassReleaseAndResetHold;
            }
            a_stage = 0;
            a_queueHead = nullptr;
            a_triggerMs = 0;
            return SyntheticHoldTapAction::kInactive;
        }

        a_stage = 0;
        a_queueHead = nullptr;
        a_triggerMs = 0;
        return SyntheticHoldTapAction::kInactive;
    }

    bool ShouldBlockPostLootActivate(
        std::int64_t a_nowMs,
        std::int64_t a_lastTakeMs,
        std::int64_t a_lastEmptyMs,
        bool a_isJustPressed,
        bool a_isHeld,
        bool a_isAlreadyGrabbing) noexcept
    {
        if (a_isAlreadyGrabbing) {
            return false;
        }
        if (a_lastEmptyMs > 0) {
            const std::int64_t emptyAge = a_nowMs - a_lastEmptyMs;
            if (emptyAge >= 0 && emptyAge <= kPostLootEmptyBlockMs) {
                return true;
            }
        }
        if (a_isHeld && !a_isJustPressed && a_lastTakeMs > 0) {
            const std::int64_t takeAge = a_nowMs - a_lastTakeMs;
            if (takeAge >= 0 && takeAge <= kPostLootGrabBlockMs) {
                return true;
            }
        }
        return false;
    }

    bool UpdateQuickLootCompanionProtection(
        std::int64_t& a_lastQuickLootActiveMs,
        bool a_quickContainerVisible,
        bool a_targetIsLiveCompanion,
        std::int64_t a_nowMs,
        std::int64_t a_graceMs) noexcept
    {
        if (a_quickContainerVisible && !a_targetIsLiveCompanion) {
            a_lastQuickLootActiveMs = a_nowMs;
            return true;
        }
        if (a_targetIsLiveCompanion) {
            return a_lastQuickLootActiveMs > 0 &&
                   (a_nowMs - a_lastQuickLootActiveMs) <= a_graceMs;
        }
        a_lastQuickLootActiveMs = 0;
        return false;
    }

    CompanionFilterDecision EvaluateCompanionFilter(const CompanionFilterInput& a_in) noexcept
    {
        CompanionFilterDecision out{};
        const float vanillaDist =
            (std::isfinite(a_in.vanillaCommandPickDistance) && a_in.vanillaCommandPickDistance >= 0.0f)
                ? a_in.vanillaCommandPickDistance
                : 2000.0f;
        const float vanillaCmdRad =
            (std::isfinite(a_in.vanillaCommandPickRadius) && a_in.vanillaCommandPickRadius >= 0.0f)
                ? a_in.vanillaCommandPickRadius
                : 24.0f;
        const float vanillaActRad =
            (std::isfinite(a_in.vanillaActivatePickRadius) && a_in.vanillaActivatePickRadius >= 0.0f)
                ? a_in.vanillaActivatePickRadius
                : 16.0f;

        if (!a_in.limitEnabled || a_in.inCommandMode) {
            out.suppressTarget = false;
            out.restoreVanillaSettings = true;
            out.desiredCommandPickDistance = vanillaDist;
            out.desiredCommandPickRadius = vanillaCmdRad;
            return out;
        }

        const float normalDist = (std::isfinite(a_in.maxDistance) && a_in.maxDistance >= 0.0f)
            ? a_in.maxDistance
            : 160.0f;
        const float combatDist = (std::isfinite(a_in.combatMaxDistance) && a_in.combatMaxDistance >= 0.0f)
            ? a_in.combatMaxDistance
            : 80.0f;
        const float effectiveMaxDist = a_in.inCombat ? combatDist : normalDist;

        if (a_in.deprioritizeLoot && a_in.quickLootProtected) {
            out.desiredCommandPickDistance = 0.0f;
            out.desiredCommandPickRadius = 0.0f;
        } else {
            out.desiredCommandPickDistance = effectiveMaxDist;
            if (effectiveMaxDist <= 0.001f) {
                out.desiredCommandPickRadius = 0.0f;
            } else if (a_in.deprioritizeLoot) {
                out.desiredCommandPickRadius = std::min(vanillaCmdRad, vanillaActRad);
            } else {
                out.desiredCommandPickRadius = vanillaCmdRad;
            }
        }

        if (a_in.targetIsLiveCompanion && !a_in.targetIsBleedout && !a_in.targetHasHealVerb) {
            if (a_in.deprioritizeLoot && a_in.quickLootProtected) {
                out.suppressTarget = true;
            } else if (effectiveMaxDist <= 0.001f) {
                out.suppressTarget = true;
            } else if (!std::isfinite(a_in.targetDistance) || a_in.targetDistance > effectiveMaxDist) {
                out.suppressTarget = true;
            }
        }

        return out;
    }

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
        if (a_in.suppressTarget && !a_in.quickContainerVisible) {
            _lossFrames = 0;
            _hasPrevScreen = false;
            _hasAnchorTarget = false;
            _modifiedAlpha = true;
            _hidingAlpha = true;

            const bool restorePos = _isFloating && _hasBaseline;
            _isFloating = false;
            if (restorePos) {
                _hasBaseline = false;
                return {
                    HUDAction::kSuppressNative,
                    _baseX,
                    _baseY,
                    0.0f,
                    true,
                    true
                };
            }
            return {
                HUDAction::kSuppressNative,
                0.0f,
                0.0f,
                0.0f,
                false,
                true
            };
        }

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
                _hasBaseline = false;
                return {
                    HUDAction::kRestoreBaseline,
                    _baseX,
                    _baseY,
                    1.0f,
                    true,
                    restoreAlpha
                };
            }

            if (_modifiedAlpha) {
                _modifiedAlpha = false;
                const bool restorePos = _hasBaseline;
                _hasBaseline = false;
                return {
                    HUDAction::kRestoreBaseline,
                    _baseX,
                    _baseY,
                    1.0f,
                    restorePos,
                    true
                };
            }

            if (a_in.inPowerArmor && a_in.powerArmorBypass) {
                _hasBaseline = false;
            }
            return {};
        }

        const bool targetActive = a_in.rolloverVisible && a_in.hasTarget;
        if (!targetActive) {
            const bool keepSuppressed =
                a_in.hideNativeClip && (a_in.nativeClipVisible || a_in.rolloverVisible);

            if (_isFloating && _hasBaseline) {
                if (++_lossFrames <= kLossGraceFrames) {
                    return {};
                }
                _isFloating = false;
                _lossFrames = 0;
                _hasPrevScreen = false;
                _hasAnchorTarget = false;
                if (keepSuppressed) {
                    _modifiedAlpha = true;
                    _hidingAlpha = true;
                    return {
                        HUDAction::kSuppressNative,
                        0.0f,
                        0.0f,
                        0.0f,
                        false,
                        true
                    };
                }
                _hidingAlpha = false;
                const bool restoreAlpha = _modifiedAlpha;
                _modifiedAlpha = false;
                _hasBaseline = false;
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
            if (keepSuppressed) {
                _modifiedAlpha = true;
                _hidingAlpha = true;
                return {
                    HUDAction::kSuppressNative,
                    0.0f,
                    0.0f,
                    0.0f,
                    false,
                    true
                };
            }
            if (_modifiedAlpha) {
                _modifiedAlpha = false;
                _hidingAlpha = false;
                const bool restorePos = _hasBaseline;
                _hasBaseline = false;
                return {
                    HUDAction::kRestoreBaseline,
                    _baseX,
                    _baseY,
                    1.0f,
                    restorePos,
                    true
                };
            }
            _hidingAlpha = false;
            return {};
        }

        _lossFrames = 0;
        if (!_isFloating) {
            if (!_hasBaseline) {
                _baseX = a_in.currentClipX;
                _baseY = a_in.currentClipY;
                _hasBaseline = true;
            }
            _isFloating = true;
            _hasPrevScreen = false;
            _hasAnchorTarget = false;
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

        [[nodiscard]] std::string ToLowerAscii(std::string a_input)
        {
            for (char& ch : a_input) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch + ('a' - 'A'));
                }
            }
            return a_input;
        }

        [[nodiscard]] std::string RawUpperKey(std::string_view a_sv)
        {
            return TargetInfo::ToUpperAscii(TrimWhitespace(a_sv));
        }

        [[nodiscard]] std::string CleanUpperKey(std::string_view a_sv)
        {
            return TargetInfo::ToUpperAscii(TargetInfo::CleanDisplayName(std::string(a_sv), true));
        }

        [[nodiscard]] std::string RawLowerKey(std::string_view a_sv)
        {
            return ToLowerAscii(TrimWhitespace(a_sv));
        }

        [[nodiscard]] std::string CleanLowerKey(std::string_view a_sv)
        {
            return ToLowerAscii(TargetInfo::CleanDisplayName(std::string(a_sv), true));
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

        void EnrichWindowFromSnapshot(
            std::vector<QuickContainerItemEntry>& a_items,
            const CachedInventorySnapshot& a_invSnap)
        {
            if (a_invSnap.kindByCleanUpper.empty() && a_invSnap.conditionByCleanUpper.empty()) {
                return;
            }
            for (auto& item : a_items) {
                if (item.itemKind != TargetInfo::ItemKind::None && item.hasCondition) {
                    continue;
                }
                const std::string key = CleanUpperKey(item.name);
                if (key.empty()) {
                    continue;
                }
                if (item.itemKind == TargetInfo::ItemKind::None) {
                    if (const auto it = a_invSnap.kindByCleanUpper.find(key); it != a_invSnap.kindByCleanUpper.end()) {
                        item.itemKind = it->second;
                    }
                }
                if (!item.hasCondition) {
                    if (const auto cIt = a_invSnap.conditionByCleanUpper.find(key);
                        cIt != a_invSnap.conditionByCleanUpper.end()) {
                        item.hasCondition = true;
                        item.conditionPercent = cIt->second;
                    }
                }
            }
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
                if (!existing.isTemporary &&
                    !cur.isTemporary &&
                    (existing.baseFormID == 0 || cur.baseFormID == 0 || existing.baseFormID == cur.baseFormID) &&
                    existing.isEquipped == cur.isEquipped &&
                    existing.isFavorite == cur.isFavorite &&
                    existing.isLegendary == cur.isLegendary &&
                    existing.isQuestItem == cur.isQuestItem &&
                    existing.quickLootCategory == cur.quickLootCategory &&
                    existing.stackSignature == cur.stackSignature &&
                    existing.name == cur.name) {
                    const std::uint64_t sum =
                        static_cast<std::uint64_t>(existing.count) + cur.count;
                    existing.count = static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(std::numeric_limits<std::uint32_t>::max(), sum));
                    existing.isTaggedForSearch = existing.isTaggedForSearch || cur.isTaggedForSearch;
                    existing.isBetterThanEquipped = existing.isBetterThanEquipped || cur.isBetterThanEquipped;
                    existing.isCaps = existing.isCaps || cur.isCaps;
                    if (existing.baseFormID == 0) {
                        existing.baseFormID = cur.baseFormID;
                    }
                    if (existing.itemKind == TargetInfo::ItemKind::None) {
                        existing.itemKind = cur.itemKind;
                        existing.quickLootCategory = cur.quickLootCategory;
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
        std::int32_t a_prevStart,
        const std::vector<std::uint8_t>* a_prioritizedInv)
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
        std::vector<std::string> invRawLower;
        std::vector<std::string> invCleanLower;
        std::vector<std::uint8_t> invPriority;
        bool anyInvPriority = false;
        if (invExpandable) {
            invRawUpper.reserve(a_inventoryItems.size());
            invCleanUpper.reserve(a_inventoryItems.size());
            invRawLower.reserve(a_inventoryItems.size());
            invCleanLower.reserve(a_inventoryItems.size());
            invPriority.reserve(a_inventoryItems.size());
            for (std::size_t k = 0; k < a_inventoryItems.size(); ++k) {
                const auto& inv = a_inventoryItems[k];
                invRawUpper.push_back(RawUpperKey(inv.name));
                invCleanUpper.push_back(CleanUpperKey(inv.name));
                invRawLower.push_back(RawLowerKey(inv.name));
                invCleanLower.push_back(CleanLowerKey(inv.name));
                const bool cachedPrio =
                    a_prioritizedInv &&
                    k < a_prioritizedInv->size() &&
                    (*a_prioritizedInv)[k] != 0u;
                const bool prio = inv.isLegendary || inv.isTaggedForSearch || inv.isBetterThanEquipped || cachedPrio;
                invPriority.push_back(prio ? std::uint8_t{ 1 } : std::uint8_t{ 0 });
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
                    const auto uBest = static_cast<std::size_t>(bestIdx);
                    invConsumed[uBest] = true;
                    if (realEngine[j].itemKind == TargetInfo::ItemKind::None) {
                        realEngine[j].itemKind = a_inventoryItems[uBest].itemKind;
                    }
                    if (realEngine[j].isLegendary || realEngine[j].isTaggedForSearch || realEngine[j].isBetterThanEquipped) {
                        invPriority[uBest] = 1u;
                    } else if (!a_inventoryItems[uBest].isLegendary &&
                               !a_inventoryItems[uBest].isTaggedForSearch &&
                               !a_inventoryItems[uBest].isBetterThanEquipped) {
                        invPriority[uBest] = 0u;
                    }
                }
            }
            for (const std::uint8_t p : invPriority) {
                if (p != 0u) {
                    anyInvPriority = true;
                    break;
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

        int distinctInvKinds = 0;
        if (invExpandable) {
            std::uint32_t kindMask = 0u;
            for (const auto& inv : a_inventoryItems) {
                const auto kIdx = static_cast<std::uint32_t>(inv.itemKind);
                if (kIdx > 0u && kIdx < 32u) {
                    const std::uint32_t bit = (1u << kIdx);
                    if ((kindMask & bit) == 0u) {
                        kindMask |= bit;
                        ++distinctInvKinds;
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

        // Build lightweight index orderings with zero string copies in comparators:
        // 0 = forward BGSInventoryList, 1 = reversed, 2 = _stricmp raw (invRawLower: '[' < 'a'..'z'),
        // 3 = case-sensitive raw, 4 = _stricmp tag-stripped (invCleanLower),
        // 5 = Value DESC (rawLower tiebreak), 6 = Value DESC (cleanLower tiebreak), 7 = Stack Value DESC,
        // 8 = Legacy Category ASC + Value DESC, 9 = Value/Weight DESC, 10 = Weight DESC,
        // 11 = uppercase raw (invRawUpper), 12 = unequipped-first + rawLower, 13 = Weight ASC,
        // 14 = FO4 QC Category (Key->Book->Holo->Ammo->Aid->Weap->Misc->Armor) + Value DESC,
        // 15 = FO4 QC Category (BGSInventoryList stable tiebreak),
        // 16 = FO4 QC Category + rawLower,
        // 17 = Priority-first + FO4 QC Category + Value DESC,
        // 18 = Priority-first + FO4 QC Category (BGSInventoryList stable tiebreak)
        std::array<std::vector<std::size_t>, 19> orderCandidates;
        orderCandidates[0].resize(nInv);
        for (std::size_t k = 0; k < nInv; ++k) {
            orderCandidates[0][k] = k;
        }
        orderCandidates[1].assign(orderCandidates[0].rbegin(), orderCandidates[0].rend());
        orderCandidates[2] = orderCandidates[0];
        std::stable_sort(orderCandidates[2].begin(), orderCandidates[2].end(),
            [&invRawLower](std::size_t a, std::size_t b) {
                return invRawLower[a] < invRawLower[b];
            });
        orderCandidates[3] = orderCandidates[0];
        std::stable_sort(orderCandidates[3].begin(), orderCandidates[3].end(),
            [&a_inventoryItems](std::size_t a, std::size_t b) {
                return a_inventoryItems[a].name < a_inventoryItems[b].name;
            });
        orderCandidates[4] = orderCandidates[0];
        std::stable_sort(orderCandidates[4].begin(), orderCandidates[4].end(),
            [&invCleanLower](std::size_t a, std::size_t b) {
                return invCleanLower[a] < invCleanLower[b];
            });
        orderCandidates[11] = orderCandidates[0];
        std::stable_sort(orderCandidates[11].begin(), orderCandidates[11].end(),
            [&invRawUpper](std::size_t a, std::size_t b) {
                return invRawUpper[a] < invRawUpper[b];
            });
        orderCandidates[12] = orderCandidates[0];
        std::stable_sort(orderCandidates[12].begin(), orderCandidates[12].end(),
            [&a_inventoryItems, &invRawLower](std::size_t a, std::size_t b) {
                if (a_inventoryItems[a].isEquipped != a_inventoryItems[b].isEquipped) {
                    return !a_inventoryItems[a].isEquipped;
                }
                return invRawLower[a] < invRawLower[b];
            });

        auto fo4QcCategoryRank = [](TargetInfo::ItemKind a_kind) -> int {
            switch (a_kind) {
            case TargetInfo::ItemKind::Key:
                return 0;
            case TargetInfo::ItemKind::BookNote:
                return 1;
            case TargetInfo::ItemKind::Holotape:
                return 2;
            case TargetInfo::ItemKind::Ammo:
                return 3;
            case TargetInfo::ItemKind::Aid:
                return 4;
            case TargetInfo::ItemKind::Weapon:
                return 5;
            case TargetInfo::ItemKind::MiscJunk:
                return 6;
            case TargetInfo::ItemKind::Armor:
                return 7;
            default:
                return 8;
            }
        };

        orderCandidates[15] = orderCandidates[0];
        std::stable_sort(orderCandidates[15].begin(), orderCandidates[15].end(),
            [&a_inventoryItems, &fo4QcCategoryRank](std::size_t a, std::size_t b) {
                return fo4QcCategoryRank(a_inventoryItems[a].itemKind) <
                       fo4QcCategoryRank(a_inventoryItems[b].itemKind);
            });
        orderCandidates[16] = orderCandidates[0];
        std::stable_sort(orderCandidates[16].begin(), orderCandidates[16].end(),
            [&a_inventoryItems, &invRawLower, &fo4QcCategoryRank](std::size_t a, std::size_t b) {
                const int ra = fo4QcCategoryRank(a_inventoryItems[a].itemKind);
                const int rb = fo4QcCategoryRank(a_inventoryItems[b].itemKind);
                if (ra != rb) {
                    return ra < rb;
                }
                return invRawLower[a] < invRawLower[b];
            });
        if (anyInvPriority) {
            orderCandidates[17] = orderCandidates[0];
            std::stable_sort(orderCandidates[17].begin(), orderCandidates[17].end(),
                [&a_inventoryItems, &invRawLower, &invPriority, &fo4QcCategoryRank](std::size_t a, std::size_t b) {
                    if (invPriority[a] != invPriority[b]) {
                        return invPriority[a] > invPriority[b];
                    }
                    const int ra = fo4QcCategoryRank(a_inventoryItems[a].itemKind);
                    const int rb = fo4QcCategoryRank(a_inventoryItems[b].itemKind);
                    if (ra != rb) {
                        return ra < rb;
                    }
                    if (a_inventoryItems[a].value != a_inventoryItems[b].value) {
                        return a_inventoryItems[a].value > a_inventoryItems[b].value;
                    }
                    return invRawLower[a] < invRawLower[b];
                });
            orderCandidates[18] = orderCandidates[0];
            std::stable_sort(orderCandidates[18].begin(), orderCandidates[18].end(),
                [&a_inventoryItems, &invPriority, &fo4QcCategoryRank](std::size_t a, std::size_t b) {
                    if (invPriority[a] != invPriority[b]) {
                        return invPriority[a] > invPriority[b];
                    }
                    return fo4QcCategoryRank(a_inventoryItems[a].itemKind) <
                           fo4QcCategoryRank(a_inventoryItems[b].itemKind);
                });
        }

        bool hasValueOrWeight = false;
        for (const auto& inv : a_inventoryItems) {
            if (inv.value > 0 || inv.weight > 0.0f) {
                hasValueOrWeight = true;
                break;
            }
        }
        if (hasValueOrWeight) {
            auto categoryRank = [](TargetInfo::ItemKind a_kind) -> int {
                switch (a_kind) {
                case TargetInfo::ItemKind::Key:
                    return 0;
                case TargetInfo::ItemKind::Weapon:
                    return 1;
                case TargetInfo::ItemKind::Armor:
                    return 2;
                case TargetInfo::ItemKind::Aid:
                    return 3;
                case TargetInfo::ItemKind::Ammo:
                    return 4;
                case TargetInfo::ItemKind::BookNote:
                    return 5;
                case TargetInfo::ItemKind::Holotape:
                    return 6;
                case TargetInfo::ItemKind::MiscJunk:
                    return 7;
                default:
                    return 8;
                }
            };
            auto valuePerWeightRank = [](const QuickContainerItemEntry& a_e) -> float {
                if (a_e.value == 0) {
                    return 0.0f;
                }
                const float w = (std::isfinite(a_e.weight) && a_e.weight > 0.0f) ? a_e.weight : 0.0f;
                if (w <= 0.001f) {
                    return 1.0e12f + static_cast<float>(a_e.value);
                }
                return static_cast<float>(a_e.value) / w;
            };

            orderCandidates[5] = orderCandidates[0];
            std::stable_sort(orderCandidates[5].begin(), orderCandidates[5].end(),
                [&a_inventoryItems, &invRawLower](std::size_t a, std::size_t b) {
                    if (a_inventoryItems[a].value != a_inventoryItems[b].value) {
                        return a_inventoryItems[a].value > a_inventoryItems[b].value;
                    }
                    return invRawLower[a] < invRawLower[b];
                });
            orderCandidates[6] = orderCandidates[0];
            std::stable_sort(orderCandidates[6].begin(), orderCandidates[6].end(),
                [&a_inventoryItems, &invCleanLower](std::size_t a, std::size_t b) {
                    if (a_inventoryItems[a].value != a_inventoryItems[b].value) {
                        return a_inventoryItems[a].value > a_inventoryItems[b].value;
                    }
                    return invCleanLower[a] < invCleanLower[b];
                });
            orderCandidates[7] = orderCandidates[0];
            std::stable_sort(orderCandidates[7].begin(), orderCandidates[7].end(),
                [&a_inventoryItems, &invRawLower](std::size_t a, std::size_t b) {
                    const std::uint64_t va =
                        static_cast<std::uint64_t>(a_inventoryItems[a].value) * a_inventoryItems[a].count;
                    const std::uint64_t vb =
                        static_cast<std::uint64_t>(a_inventoryItems[b].value) * a_inventoryItems[b].count;
                    if (va != vb) {
                        return va > vb;
                    }
                    return invRawLower[a] < invRawLower[b];
                });
            orderCandidates[8] = orderCandidates[0];
            std::stable_sort(orderCandidates[8].begin(), orderCandidates[8].end(),
                [&a_inventoryItems, &invRawLower, &categoryRank](std::size_t a, std::size_t b) {
                    const int ra = categoryRank(a_inventoryItems[a].itemKind);
                    const int rb = categoryRank(a_inventoryItems[b].itemKind);
                    if (ra != rb) {
                        return ra < rb;
                    }
                    if (a_inventoryItems[a].value != a_inventoryItems[b].value) {
                        return a_inventoryItems[a].value > a_inventoryItems[b].value;
                    }
                    return invRawLower[a] < invRawLower[b];
                });
            orderCandidates[9] = orderCandidates[0];
            std::stable_sort(orderCandidates[9].begin(), orderCandidates[9].end(),
                [&a_inventoryItems, &invRawLower, &valuePerWeightRank](std::size_t a, std::size_t b) {
                    const float ra = valuePerWeightRank(a_inventoryItems[a]);
                    const float rb = valuePerWeightRank(a_inventoryItems[b]);
                    if (ra != rb) {
                        return ra > rb;
                    }
                    if (a_inventoryItems[a].value != a_inventoryItems[b].value) {
                        return a_inventoryItems[a].value > a_inventoryItems[b].value;
                    }
                    return invRawLower[a] < invRawLower[b];
                });
            orderCandidates[10] = orderCandidates[0];
            std::stable_sort(orderCandidates[10].begin(), orderCandidates[10].end(),
                [&a_inventoryItems, &invRawLower](std::size_t a, std::size_t b) {
                    const float rawA = a_inventoryItems[a].weight;
                    const float rawB = a_inventoryItems[b].weight;
                    const float wa = (std::isfinite(rawA) && rawA > 0.0f) ? rawA : 0.0f;
                    const float wb = (std::isfinite(rawB) && rawB > 0.0f) ? rawB : 0.0f;
                    if (wa != wb) {
                        return wa > wb;
                    }
                    return invRawLower[a] < invRawLower[b];
                });
            orderCandidates[13] = orderCandidates[0];
            std::stable_sort(orderCandidates[13].begin(), orderCandidates[13].end(),
                [&a_inventoryItems, &invRawLower](std::size_t a, std::size_t b) {
                    const float rawA = a_inventoryItems[a].weight;
                    const float rawB = a_inventoryItems[b].weight;
                    const float wa = (std::isfinite(rawA) && rawA > 0.0f) ? rawA : 0.0f;
                    const float wb = (std::isfinite(rawB) && rawB > 0.0f) ? rawB : 0.0f;
                    if (wa != wb) {
                        return wa < wb;
                    }
                    return invRawLower[a] < invRawLower[b];
                });
            orderCandidates[14] = orderCandidates[0];
            std::stable_sort(orderCandidates[14].begin(), orderCandidates[14].end(),
                [&a_inventoryItems, &invRawLower, &fo4QcCategoryRank](std::size_t a, std::size_t b) {
                    const int ra = fo4QcCategoryRank(a_inventoryItems[a].itemKind);
                    const int rb = fo4QcCategoryRank(a_inventoryItems[b].itemKind);
                    if (ra != rb) {
                        return ra < rb;
                    }
                    if (a_inventoryItems[a].value != a_inventoryItems[b].value) {
                        return a_inventoryItems[a].value > a_inventoryItems[b].value;
                    }
                    return invRawLower[a] < invRawLower[b];
                });
        }

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

        for (int c = 0; c < static_cast<int>(orderCandidates.size()); ++c) {
            const auto& idxs = orderCandidates[static_cast<std::size_t>(c)];
            if (idxs.size() != nInv) {
                continue;
            }
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
                // Deterministic sorted orderings beat raw/reversed BGSInventoryList on prefix ties
                // (e.g. first 4 items matching both before swap-and-popped entries leak into row 4+)
                if (c >= 2) {
                    totalScore += 2;
                }
                if (distinctInvKinds >= 2) {
                    if (c == 14) {
                        totalScore += 1;
                    } else if (c == 17) {
                        totalScore += 2;
                    }
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
        if (anyInvPriority) {
            for (std::size_t k = 0; k < nInv; ++k) {
                if (invPriority[k] != 0u) {
                    out.matchedPriorityInvIndices.push_back(k);
                }
            }
        }

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
                entry.isBetterThanEquipped = src.isBetterThanEquipped;
                if (entry.itemKind == TargetInfo::ItemKind::None) {
                    entry.itemKind = src.itemKind;
                }
            }
            out.windowItems.push_back(std::move(entry));
        }
        return out;
    }

    const QuickContainerMergeResult& ResolveMergedQuickContainerWindow(
        QuickContainerMergeCache& a_cache,
        std::uint32_t a_ownerFormID,
        const CachedInventorySnapshot& a_invSnap,
        const std::vector<QuickContainerItemEntry>& a_rawWindow,
        std::int32_t a_rawSel,
        int a_maxRows,
        bool a_expandEnabled)
    {
        if (a_ownerFormID == 0 || a_ownerFormID != a_cache.stickyOwnerFormID) {
            a_cache.stickyOwnerFormID = a_ownerFormID;
            a_cache.stickyInvGen = a_invSnap.generation;
            a_cache.lockedOrderIndex = -1;
            a_cache.lastMatchedStart = -1;
            a_cache.stickyFallback = false;
            a_cache.everExpanded = false;
            a_cache.consecutiveMisses = 0;
            a_cache.stickyPriorityKeys.clear();
        } else if (a_invSnap.generation != a_cache.stickyInvGen) {
            a_cache.stickyInvGen = a_invSnap.generation;
            a_cache.stickyFallback = false;
            a_cache.consecutiveMisses = 0;
            if (a_cache.lastMatchedStart >= 0 && !a_invSnap.items.empty()) {
                a_cache.lastMatchedStart = std::min<std::int32_t>(
                    a_cache.lastMatchedStart,
                    static_cast<std::int32_t>(a_invSnap.items.size() - 1));
            } else {
                a_cache.lastMatchedStart = -1;
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
            rawHash ^= std::hash<bool>{}(it.isEquipped) + 0x9e3779b9u + (rawHash << 6) + (rawHash >> 2);
            rawHash ^= std::hash<bool>{}(it.isLegendary) + 0x9e3779b9u + (rawHash << 6) + (rawHash >> 2);
            rawHash ^= std::hash<bool>{}(it.isTaggedForSearch) + 0x9e3779b9u + (rawHash << 6) + (rawHash >> 2);
            rawHash ^= std::hash<bool>{}(it.isBetterThanEquipped) + 0x9e3779b9u + (rawHash << 6) + (rawHash >> 2);
        }

        if (a_ownerFormID != 0 &&
            a_ownerFormID == a_cache.ownerFormID &&
            a_invSnap.generation == a_cache.invGeneration &&
            rawHash == a_cache.rawWindowHash &&
            a_rawSel == a_cache.rawSel &&
            a_maxRows == a_cache.maxRows &&
            a_expandEnabled == a_cache.expandEnabled &&
            (a_cache.consecutiveMisses == 0 || a_cache.consecutiveMisses > 6 || a_cache.stickyFallback)) {
            return a_cache.result;
        }

        QuickContainerMergeResult res;
        if (!a_expandEnabled) {
            res = MergeQuickContainerInventory(a_rawWindow, a_rawSel, {}, 5);
            EnrichWindowFromSnapshot(res.windowItems, a_invSnap);
            a_cache.consecutiveMisses = 0;
        } else {
            const bool skipExpansion = a_invSnap.truncatedByCap || a_cache.stickyFallback;
            std::vector<std::uint8_t> cachedPriority;
            if (!skipExpansion && !a_cache.stickyPriorityKeys.empty() && !a_invSnap.items.empty()) {
                cachedPriority.assign(a_invSnap.items.size(), 0u);
                for (std::size_t k = 0; k < a_invSnap.items.size(); ++k) {
                    const auto& inv = a_invSnap.items[k];
                    const std::string invClean = CleanUpperKey(inv.name);
                    if (invClean.empty()) {
                        continue;
                    }
                    for (const auto& pKey : a_cache.stickyPriorityKeys) {
                        if (inv.stackSignature == pKey.stackSignature &&
                            inv.isEquipped == pKey.isEquipped &&
                            inv.isFavorite == pKey.isFavorite &&
                            inv.itemKind == pKey.itemKind &&
                            invClean == pKey.cleanUpperName) {
                            cachedPriority[k] = 1u;
                            break;
                        }
                    }
                }
            }
            res = skipExpansion
                ? MergeQuickContainerInventory(a_rawWindow, a_rawSel, {}, a_maxRows)
                : MergeQuickContainerInventory(
                    a_rawWindow, a_rawSel, a_invSnap.items, a_maxRows,
                    a_cache.lockedOrderIndex, a_cache.lastMatchedStart,
                    cachedPriority.empty() ? nullptr : &cachedPriority);

            if (skipExpansion) {
                EnrichWindowFromSnapshot(res.windowItems, a_invSnap);
                a_cache.consecutiveMisses = 0;
            } else if (res.expanded) {
                a_cache.everExpanded = true;
                a_cache.consecutiveMisses = 0;
                a_cache.stickyFallback = false;
                a_cache.lockedOrderIndex = res.matchedOrderIndex;
                a_cache.lastMatchedStart = res.matchedStartIndex;
                a_cache.stickyPriorityKeys.clear();
                a_cache.stickyPriorityKeys.reserve(res.matchedPriorityInvIndices.size());
                for (const std::size_t k : res.matchedPriorityInvIndices) {
                    if (k < a_invSnap.items.size()) {
                        const auto& inv = a_invSnap.items[k];
                        a_cache.stickyPriorityKeys.push_back({
                            CleanUpperKey(inv.name),
                            inv.stackSignature,
                            inv.isEquipped,
                            inv.isFavorite,
                            inv.itemKind
                        });
                    }
                }
            } else {
                const bool hadPrevStart = (a_cache.lastMatchedStart >= 0);
                const bool expansionEligible =
                    !a_invSnap.items.empty() &&
                    a_invSnap.items.size() > realCount &&
                    (windowHasEllipsis || (realCount >= 5 && hadPrevStart));
                if (a_cache.everExpanded) {
                    ++a_cache.consecutiveMisses;
                    if (a_cache.consecutiveMisses > 6) {
                        a_cache.lastMatchedStart = -1;
                    }
                    if (a_cache.consecutiveMisses <= 2 &&
                        a_cache.result.expanded &&
                        a_cache.ownerFormID == a_ownerFormID &&
                        a_cache.maxRows == a_maxRows &&
                        a_cache.expandEnabled == a_expandEnabled &&
                        a_invSnap.items.size() > realCount) {
                        a_cache.invGeneration = a_invSnap.generation;
                        a_cache.rawWindowHash = rawHash;
                        a_cache.rawSel = a_rawSel;
                        return a_cache.result;
                    }
                } else if (!expansionEligible) {
                    a_cache.consecutiveMisses = 0;
                    a_cache.lastMatchedStart = -1;
                } else {
                    ++a_cache.consecutiveMisses;
                    a_cache.lastMatchedStart = -1;
                    if (a_cache.consecutiveMisses >= 3) {
                        a_cache.stickyFallback = true;
                    }
                }
                EnrichWindowFromSnapshot(res.windowItems, a_invSnap);
            }
        }

        a_cache.ownerFormID = a_ownerFormID;
        a_cache.invGeneration = a_invSnap.generation;
        a_cache.rawWindowHash = rawHash;
        a_cache.rawSel = a_rawSel;
        a_cache.maxRows = a_maxRows;
        a_cache.expandEnabled = a_expandEnabled;
        a_cache.result = std::move(res);
        return a_cache.result;
    }

    QuickLootSortConfig QuickLootSortConfig::FromConfig(const ConfigData& a_cfg) noexcept
    {
        QuickLootSortConfig out;
        out.sortMode = std::clamp(a_cfg.qcSortMode, 0, 6);
        out.categorySecondarySort = std::clamp(a_cfg.qcCategorySecondarySort, 0, 3);
        out.pinQuestItems = a_cfg.qcPinQuestItems;
        out.pinLegendary = a_cfg.qcPinLegendary;
        out.pinTaggedForSearch = a_cfg.qcPinTaggedForSearch;
        out.pinCaps = a_cfg.qcPinCaps;
        out.categoryOrder = a_cfg.qcCategoryOrder;
        SanitizeQuickLootCategoryOrder(out.categoryOrder);
        return out;
    }

    void SortQuickContainerItems(
        std::vector<QuickContainerItemEntry>& a_items,
        const QuickLootSortConfig& a_cfg)
    {
        if (a_items.size() <= 1 || a_cfg.sortMode == 6) {
            return;
        }

        const int sortMode = std::clamp(a_cfg.sortMode, 0, 5);
        const int secondaryMode = std::clamp(a_cfg.categorySecondarySort, 0, 3);

        auto cleanOrder = a_cfg.categoryOrder;
        SanitizeQuickLootCategoryOrder(cleanOrder);
        std::array<int, kQuickLootCategoryCount> catRank{};
        for (std::size_t r = 0; r < kQuickLootCategoryCount; ++r) {
            catRank[static_cast<std::size_t>(cleanOrder[r])] = static_cast<int>(r);
        }

        struct SortKey
        {
            std::size_t index{ 0 };
            int pinTier{ 4 };
            int categoryRank{ static_cast<int>(kQuickLootCategoryCount) - 1 };
            std::uint32_t value{ 0 };
            float weight{ 0.0f };
            double valuePerWeight{ 0.0 };
            std::string sortNameLower;
        };

        std::vector<SortKey> keys;
        keys.reserve(a_items.size());
        for (std::size_t i = 0; i < a_items.size(); ++i) {
            const auto& e = a_items[i];
            SortKey k;
            k.index = i;
            if (a_cfg.pinQuestItems && e.isQuestItem) {
                k.pinTier = 0;
            } else if (a_cfg.pinLegendary && e.isLegendary) {
                k.pinTier = 1;
            } else if (a_cfg.pinTaggedForSearch && e.isTaggedForSearch) {
                k.pinTier = 2;
            } else if (a_cfg.pinCaps && e.isCaps) {
                k.pinTier = 3;
            } else {
                k.pinTier = 4;
            }

            const int catIdx = static_cast<int>(e.quickLootCategory);
            if (catIdx >= 0 && catIdx < static_cast<int>(kQuickLootCategoryCount)) {
                k.categoryRank = catRank[static_cast<std::size_t>(catIdx)];
            }

            k.value = e.value;
            k.weight = (std::isfinite(e.weight) && e.weight > 0.0f) ? e.weight : 0.0f;
            if (k.value == 0u) {
                k.valuePerWeight = 0.0;
            } else if (k.weight <= 0.0f) {
                k.valuePerWeight = 1e12 + static_cast<double>(k.value);
            } else {
                k.valuePerWeight = static_cast<double>(k.value) / static_cast<double>(k.weight);
            }

            k.sortNameLower = CleanLowerKey(e.name);
            if (k.sortNameLower.empty()) {
                k.sortNameLower = RawLowerKey(e.name);
            }
            keys.push_back(std::move(k));
        }

        auto cmpNameAsc = [&](const SortKey& a, const SortKey& b) -> int {
            if (a.sortNameLower != b.sortNameLower) {
                return (a.sortNameLower < b.sortNameLower) ? -1 : 1;
            }
            const auto& ea = a_items[a.index];
            const auto& eb = a_items[b.index];
            if (ea.name != eb.name) {
                return (ea.name < eb.name) ? -1 : 1;
            }
            return 0;
        };

        auto cmpSecondary = [&](const SortKey& a, const SortKey& b, int mode) -> int {
            switch (mode) {
            case 0: // Value DESC
                if (a.value != b.value) {
                    return (a.value > b.value) ? -1 : 1;
                }
                break;
            case 1: // Value / Weight DESC
                if (a.valuePerWeight != b.valuePerWeight) {
                    return (a.valuePerWeight > b.valuePerWeight) ? -1 : 1;
                }
                if (a.value != b.value) {
                    return (a.value > b.value) ? -1 : 1;
                }
                break;
            case 2: // Alphabetical A-Z
                if (const int nc = cmpNameAsc(a, b); nc != 0) {
                    return nc;
                }
                break;
            case 3: // Weight ASC
                if (a.weight != b.weight) {
                    return (a.weight < b.weight) ? -1 : 1;
                }
                break;
            default:
                break;
            }
            return 0;
        };

        std::stable_sort(keys.begin(), keys.end(), [&](const SortKey& a, const SortKey& b) {
            if (a.pinTier != b.pinTier) {
                return a.pinTier < b.pinTier;
            }

            switch (sortMode) {
            case 0: // Category Priority
                if (a.categoryRank != b.categoryRank) {
                    return a.categoryRank < b.categoryRank;
                }
                if (const int sc = cmpSecondary(a, b, secondaryMode); sc != 0) {
                    return sc < 0;
                }
                break;
            case 1: // Value DESC
                if (a.value != b.value) {
                    return a.value > b.value;
                }
                break;
            case 2: // Value / Weight DESC
                if (a.valuePerWeight != b.valuePerWeight) {
                    return a.valuePerWeight > b.valuePerWeight;
                }
                if (a.value != b.value) {
                    return a.value > b.value;
                }
                break;
            case 3: // Alphabetical A-Z
                if (const int nc = cmpNameAsc(a, b); nc != 0) {
                    return nc < 0;
                }
                break;
            case 4: // Weight ASC
                if (a.weight != b.weight) {
                    return a.weight < b.weight;
                }
                break;
            case 5: // Weight DESC
                if (a.weight != b.weight) {
                    return a.weight > b.weight;
                }
                break;
            default:
                break;
            }

            if (const int nc = cmpNameAsc(a, b); nc != 0) {
                return nc < 0;
            }
            const auto& ea = a_items[a.index];
            const auto& eb = a_items[b.index];
            if (ea.count != eb.count) {
                return ea.count > eb.count;
            }
            if (a.value != b.value) {
                return a.value > b.value;
            }
            if (ea.baseFormID != eb.baseFormID) {
                return ea.baseFormID < eb.baseFormID;
            }
            if (ea.stackSignature != eb.stackSignature) {
                return ea.stackSignature < eb.stackSignature;
            }
            if (ea.isEquipped != eb.isEquipped) {
                return !ea.isEquipped;
            }
            if (ea.isTemporary != eb.isTemporary) {
                return !ea.isTemporary;
            }
            return a.index < b.index;
        });

        std::vector<QuickContainerItemEntry> sorted;
        sorted.reserve(a_items.size());
        for (const auto& k : keys) {
            sorted.push_back(std::move(a_items[k.index]));
        }
        a_items = std::move(sorted);
    }

    std::int32_t StepCustomQuickLootSelection(
        std::int32_t a_currentIndex,
        std::int32_t a_delta,
        std::int32_t a_itemCount) noexcept
    {
        if (a_itemCount <= 0) {
            return 0;
        }
        const std::int64_t next = static_cast<std::int64_t>(a_currentIndex) + static_cast<std::int64_t>(a_delta);
        if (next <= 0) {
            return 0;
        }
        const std::int64_t maxIdx = static_cast<std::int64_t>(a_itemCount) - 1;
        if (next >= maxIdx) {
            return static_cast<std::int32_t>(maxIdx);
        }
        return static_cast<std::int32_t>(next);
    }

    QuickLootTakeInputDecision StepQuickLootTakeLatch(
        QuickLootTakeLatchState& a_state,
        QuickLootTakePassState& a_pass,
        const QuickLootTakeInput& a_in,
        std::int64_t a_nowMs,
        std::int64_t a_debounceMs,
        std::int64_t a_staleLatchTimeoutMs) noexcept
    {
        const bool eventHasKey = (a_in.device != 0xFFFFFFFFu);
        const bool matchesBoundKey =
            a_state.hasBoundKey &&
            eventHasKey &&
            a_in.device == a_state.boundDevice &&
            a_in.idCode == a_state.boundIdCode;

        if (a_in.isReleased || !a_in.isPressed) {
            if (a_pass.unlatchedThisPass &&
                a_in.isTakeFamily &&
                (a_pass.unlatchedDevice == 0xFFFFFFFFu || !eventHasKey ||
                 (a_in.device == a_pass.unlatchedDevice && a_in.idCode == a_pass.unlatchedIdCode))) {
                return { false, true };
            }
            if (a_state.latched && (matchesBoundKey || (!a_state.hasBoundKey && a_in.isTakeFamily))) {
                a_state.latched = false;
                a_state.hasBoundKey = false;
                a_state.boundDevice = 0xFFFFFFFFu;
                a_state.boundIdCode = 0xFFFFFFFFu;
                a_state.lastReleaseMs = a_nowMs;
                a_pass.unlatchedThisPass = true;
                a_pass.unlatchedDevice = a_in.device;
                a_pass.unlatchedIdCode = a_in.idCode;
                return { false, a_in.isTakeFamily };
            }
            if (!a_state.latched &&
                a_in.isTakeFamily &&
                a_debounceMs > 0 &&
                a_state.lastReleaseMs > 0 &&
                a_nowMs >= a_state.lastReleaseMs &&
                (a_nowMs - a_state.lastReleaseMs) < a_debounceMs) {
                a_state.lastReleaseMs = a_nowMs;
            }
            return { false, false };
        }

        if (a_state.latched &&
            a_in.isJustPressed &&
            a_nowMs > a_state.latchSetMs &&
            (a_nowMs - a_state.latchSetMs) >= a_staleLatchTimeoutMs) {
            // Recover cleanly only if a fresh press arrives after losing key-up.
            a_state.latched = false;
            a_state.hasBoundKey = false;
            a_state.boundDevice = 0xFFFFFFFFu;
            a_state.boundIdCode = 0xFFFFFFFFu;
        }

        if (a_state.latched) {
            if (a_in.isTakeFamily || matchesBoundKey) {
                // Heartbeat keeps a long physical hold latched indefinitely across lag spikes.
                a_state.latchSetMs = a_nowMs;
                return { false, a_in.isTakeFamily };
            }
            return { false, false };
        }

        if (a_in.isJustPressed) {
            if (a_in.canTake && !a_pass.acceptedTakeThisPass) {
                a_state.latched = true;
                a_state.hasBoundKey = eventHasKey;
                a_state.boundDevice = a_in.device;
                a_state.boundIdCode = a_in.idCode;
                a_state.latchSetMs = a_nowMs;
                const bool tooSoonAfterRelease =
                    a_debounceMs > 0 &&
                    a_state.lastReleaseMs > 0 &&
                    a_nowMs >= a_state.lastReleaseMs &&
                    (a_nowMs - a_state.lastReleaseMs) < a_debounceMs;
                const bool tooSoonAfterPress =
                    a_debounceMs > 0 &&
                    a_state.lastTakePressMs > 0 &&
                    a_nowMs >= a_state.lastTakePressMs &&
                    (a_nowMs - a_state.lastTakePressMs) < a_debounceMs;
                if (tooSoonAfterRelease || tooSoonAfterPress) {
                    return { false, true };
                }
                a_state.lastTakePressMs = a_nowMs;
                a_pass.acceptedTakeThisPass = true;
                return { true, true };
            }
            if (a_in.suppressTakeFamilyPress && a_in.isTakeFamily) {
                return { false, true };
            }
            return { false, false };
        }

        return { false, false };
    }

    SuppressedReadyDecision StepSuppressedSecondaryReady(
        SuppressedReadyState& a_state,
        const SuppressedReadyInput& a_in,
        std::int64_t a_nowMs,
        float a_holsterHoldSecs,
        std::int64_t a_staleHoldTimeoutMs) noexcept
    {
        if (a_in.isQcTransfer) {
            const bool swallow = (a_in.blockCompanionReady || a_state.held || a_in.alreadyHandledThisPass);
            return { swallow, false, SuppressedReadyAction::kNone };
        }

        if (!a_in.isSecondaryActivate && !a_in.isReadyWeapon) {
            return { false, false, SuppressedReadyAction::kNone };
        }

        const bool shouldBlockSecondary =
            a_in.isSecondaryActivate &&
            (a_in.blockCompanionReady || a_in.blockOrphanSecondary);
        const bool shouldBlockReady =
            a_in.isReadyWeapon &&
            (a_in.blockCompanionReady || a_in.passHasBlockedSecondary);

        if (a_in.alreadyHandledThisPass) {
            return { true, false, SuppressedReadyAction::kNone };
        }

        if (a_state.held &&
            a_in.isJustPressed &&
            a_nowMs > a_state.startMs &&
            (a_nowMs - a_state.startMs) > a_staleHoldTimeoutMs) {
            a_state.held = false;
            a_state.holstered = false;
        }

        const bool shouldStartHold =
            a_in.isJustPressed &&
            (shouldBlockSecondary || (shouldBlockReady && a_in.canCustomReadyAction));

        if (shouldStartHold) {
            if (a_in.canCustomReadyAction) {
                a_state.held = true;
                a_state.holstered = false;
                a_state.startMs = a_nowMs;
            }
            return { true, true, SuppressedReadyAction::kNone };
        }

        if (a_state.held) {
            if (a_in.isReleased) {
                const bool alreadyHolstered = a_state.holstered;
                a_state.held = false;
                a_state.holstered = false;
                SuppressedReadyAction act = SuppressedReadyAction::kNone;
                if (!alreadyHolstered && a_in.canCustomReadyAction && a_in.weaponDrawn) {
                    act = SuppressedReadyAction::kReload;
                }
                return { true, true, act };
            }

            if (a_in.isPressed) {
                a_state.startMs = a_nowMs;
                if (!a_state.holstered && a_in.heldDownSecs >= a_holsterHoldSecs) {
                    a_state.holstered = true;
                    if (a_in.canCustomReadyAction) {
                        return {
                            true,
                            true,
                            a_in.weaponDrawn
                                ? SuppressedReadyAction::kSheath
                                : SuppressedReadyAction::kDraw
                        };
                    }
                }
                return { true, true, SuppressedReadyAction::kNone };
            }
        }

        if (shouldBlockSecondary) {
            return { true, false, SuppressedReadyAction::kNone };
        }

        return { false, false, SuppressedReadyAction::kNone };
    }

    QuickLootCategory ClassifyWeaponQuickLootCategory(
        std::uint8_t a_weaponType,
        bool a_hasGrenadeOrMineKw,
        bool a_hasHeavyKw,
        bool a_hasExplosiveKw,
        bool a_hasMeleeKw,
        bool a_hasPistolKw,
        bool a_hasRifleOrShotgunKw) noexcept
    {
        // WEAPON_TYPE: 0..6 = HandToHand / Melee1H / Melee2H / Staff, 10 = Grenade, 11 = Mine.
        if (a_weaponType == 10u || a_weaponType == 11u || a_hasGrenadeOrMineKw) {
            return QuickLootCategory::Explosive;
        }
        // Check HeavyGun before Explosive so Fat Man / Missile Launcher land in Heavy Weapons.
        if (a_hasHeavyKw) {
            return QuickLootCategory::HeavyWeapon;
        }
        if (a_hasExplosiveKw) {
            return QuickLootCategory::Explosive;
        }
        if (a_weaponType <= 6u || a_hasMeleeKw) {
            return QuickLootCategory::Melee;
        }
        if (a_hasPistolKw) {
            return QuickLootCategory::Pistol;
        }
        if (a_hasRifleOrShotgunKw) {
            return QuickLootCategory::Rifle;
        }
        return QuickLootCategory::Rifle;
    }

    QuickLootCategory ClassifyArmorQuickLootCategory(
        bool a_hasPowerArmorKw,
        bool a_hasArmorKw,
        bool a_hasClothingKw,
        std::uint16_t a_armorRating,
        bool a_hasExtraResist) noexcept
    {
        if (a_hasPowerArmorKw) {
            return QuickLootCategory::PowerArmor;
        }
        if (a_hasClothingKw && !a_hasArmorKw && a_armorRating == 0u && !a_hasExtraResist) {
            return QuickLootCategory::Clothing;
        }
        if (a_hasArmorKw || a_armorRating > 0u || a_hasExtraResist) {
            return QuickLootCategory::Armor;
        }
        return QuickLootCategory::Clothing;
    }

    QuickLootCategory ClassifyAidQuickLootCategory(
        bool a_isMedicineFlag,
        bool a_isFoodFlag,
        bool a_hasMedicineKw,
        bool a_hasChemKw,
        bool a_hasDrinkKw,
        bool a_hasFoodKw,
        bool a_hasAddiction) noexcept
    {
        if (a_isMedicineFlag || a_hasMedicineKw) {
            return QuickLootCategory::Medicine;
        }
        // Check drinks before addiction so Alcohol (which has an addiction effect) stays Drink.
        if (a_hasDrinkKw) {
            return QuickLootCategory::Drink;
        }
        if (a_hasChemKw) {
            return QuickLootCategory::Chem;
        }
        if (a_hasFoodKw || a_isFoodFlag) {
            return QuickLootCategory::Food;
        }
        if (a_hasAddiction) {
            return QuickLootCategory::Chem;
        }
        return QuickLootCategory::Medicine;
    }

    bool IsQuickContainerWorkbenchTarget(
        TargetInfo::ItemKind a_targetKind,
        bool a_engineValid,
        bool a_isPowerArmorMode,
        bool a_isCraftingOrWorkshopMode) noexcept
    {
        if (a_engineValid && a_isPowerArmorMode) {
            return false;
        }
        return (a_targetKind == TargetInfo::ItemKind::Furniture) ||
               (a_engineValid && a_isCraftingOrWorkshopMode);
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
            bool rolloverRawVisible{ false };

            bool hasQuickContainer{ false };
            RE::Scaleform::GFx::Value qcParent;
            RE::Scaleform::GFx::Value quickContainer;
            bool qcParentIsRoot{ false };
            bool quickContainerVisible{ false };
            bool quickContainerRawVisible{ false };
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

        [[nodiscard]] bool IsRawClipVisible(const RE::Scaleform::GFx::Value& a_clip)
        {
            if (!a_clip.IsObject()) {
                return false;
            }
            RE::Scaleform::GFx::Value visVal;
            return a_clip.GetMember("visible"sv, &visVal) && visVal.IsBoolean() && visVal.GetBoolean();
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
                    a_out.rolloverRawVisible = IsRawClipVisible(rollover);
                }
            }

            if (!a_out.hasQuickContainer) {
                RE::Scaleform::GFx::Value qc;
                if (a_container.GetMember("QuickContainerWidget_mc"sv, &qc) && qc.IsDisplayObject()) {
                    a_out.qcParent = a_container;
                    a_out.quickContainer = qc;
                    a_out.hasQuickContainer = true;
                    a_out.qcParentIsRoot = a_isRootContainer;
                    a_out.quickContainerRawVisible = IsRawClipVisible(qc);
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
            bool a_requireMatchingTarget = false,
            std::string_view a_sfHeader = {},
            std::string_view a_sfFirstItem = {},
            bool a_allowContentMatch = false)
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

            const bool exactMatch =
                a_targetRef != nullptr &&
                ((contPtr && contPtr.get() == a_targetRef) ||
                 (invPtr && invPtr.get() == a_targetRef));
            const bool linkedWorkbench =
                a_targetRef != nullptr &&
                !exactMatch &&
                (qc.mode == RE::QuickContainerMode::kCrafting ||
                 qc.mode == RE::QuickContainerMode::kWorkshop) &&
                AnchorManager::ClassifyReference(a_targetRef) == AnchorManager::TargetCategory::kFurnitureTerminal;

            bool contentMatch = false;
            if (a_allowContentMatch && !exactMatch && !linkedWorkbench) {
                const std::string_view engHeaderSv = qc.containerName.empty()
                    ? std::string_view{}
                    : std::string_view{ qc.containerName.c_str(), qc.containerName.size() };
                const std::string engHeaderKey = CleanUpperKey(engHeaderSv);
                const std::string sfHeaderKey = CleanUpperKey(a_sfHeader);
                const bool headersBothPresent = !engHeaderKey.empty() && !sfHeaderKey.empty();
                const bool headerExactMatch = headersBothPresent && (engHeaderKey == sfHeaderKey);
                const bool headersCompatible =
                    !headersBothPresent ||
                    headerExactMatch ||
                    (engHeaderKey.find(sfHeaderKey) != std::string::npos) ||
                    (sfHeaderKey.find(engHeaderKey) != std::string::npos);

                std::string_view engFirstItem;
                for (const auto& it : qc.itemData) {
                    if (!it.itemName.empty()) {
                        const std::string_view sv{ it.itemName.c_str(), it.itemName.size() };
                        if (sv != "..."sv && sv != "\xE2\x80\xA6"sv) {
                            engFirstItem = sv;
                            break;
                        }
                    }
                }
                const bool engHasItem = !engFirstItem.empty();
                const bool sfHasItem = !a_sfFirstItem.empty();
                if (engHasItem && sfHasItem) {
                    contentMatch = DoItemNamesMatch(engFirstItem, a_sfFirstItem) && headersCompatible;
                } else if (!engHasItem && !sfHasItem) {
                    contentMatch = headerExactMatch;
                } else {
                    contentMatch = false;
                }
            }

            if (!contPtr && !invPtr && !(contentMatch && !qc.itemData.empty())) {
                return;
            }

            if (!a_targetRef) {
                if (a_requireMatchingTarget && !contentMatch) {
                    return;
                }
            } else if (!exactMatch && !linkedWorkbench && !contentMatch) {
                return;
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
                entry.isBetterThanEquipped = it.isBetterThanEquippedItem;
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
            a_out.isBetterThanEquipped = TryReadBoolMember(a_itemObj, "isBetterThanEquippedItem"sv, ""sv, false);

            RE::Scaleform::GFx::Value eqVal;
            float eqFloat = 0.0f;
            if (a_itemObj.GetMember("equipState"sv, &eqVal) && TryReadNumber(eqVal, eqFloat)) {
                a_out.isEquipped = (eqFloat > 0.5f);
            }
            return true;
        }

        void TryReadScaleformQuickContainerIdentity(
            const RE::Scaleform::GFx::Value& a_qcClip,
            std::string& a_outHeader,
            std::string& a_outFirstItem)
        {
            a_outHeader.clear();
            a_outFirstItem.clear();

            if (!a_qcClip.IsObject()) {
                return;
            }

            if (!TryReadStringMember(a_qcClip, "containerName"sv, ""sv, a_outHeader)) {
                RE::Scaleform::GFx::Value bracket;
                RE::Scaleform::GFx::Value nameMc;
                if (a_qcClip.GetMember("ListHeaderAndBracket_mc"sv, &bracket) && bracket.IsObject() &&
                    bracket.GetMember("ContainerName_mc"sv, &nameMc) && nameMc.IsObject()) {
                    (void)TryReadTextField(nameMc, "textField_tf"sv, a_outHeader);
                }
            }

            auto tryAcceptIdentityItem = [&](std::string_view a_raw) -> bool {
                std::string trimmed = TrimWhitespace(a_raw);
                if (trimmed.empty() || trimmed == "(Item)"sv) {
                    return false;
                }
                QuickContainerItemEntry probe;
                probe.name = trimmed;
                if (IsEllipsisItem(probe)) {
                    return false;
                }
                a_outFirstItem = std::move(trimmed);
                return true;
            };

            auto scanArrayFirstReal = [&](const RE::Scaleform::GFx::Value& a_arr) -> bool {
                if (!a_arr.IsObject()) {
                    return false;
                }
                RE::Scaleform::GFx::Value lenVal;
                float lenFloat = 0.0f;
                if (!a_arr.GetMember("length"sv, &lenVal) || !TryReadNumber(lenVal, lenFloat) ||
                    !std::isfinite(lenFloat) || lenFloat < 0.5f) {
                    return false;
                }
                const int n = static_cast<int>(std::clamp(lenFloat, 0.0f, 8.0f));
                for (int i = 0; i < n; ++i) {
                    const std::string k = std::to_string(i);
                    RE::Scaleform::GFx::Value itemObj;
                    std::string itemName;
                    if (a_arr.GetMember(k.c_str(), &itemObj) && itemObj.IsObject() &&
                        TryReadStringMember(itemObj, "text"sv, "itemName"sv, itemName) &&
                        tryAcceptIdentityItem(itemName)) {
                        return true;
                    }
                }
                return false;
            };

            RE::Scaleform::GFx::Value arr;
            if (a_qcClip.GetMember("ItemDataA"sv, &arr) && scanArrayFirstReal(arr)) {
                return;
            }

            RE::Scaleform::GFx::Value listMc;
            if (a_qcClip.GetMember("ListItems_mc"sv, &listMc) && listMc.IsObject() &&
                listMc.GetMember("entryList"sv, &arr) && scanArrayFirstReal(arr)) {
                return;
            }

            RE::Scaleform::GFx::Value clipsArr;
            if (a_qcClip.GetMember("ItemClipsA"sv, &clipsArr) && clipsArr.IsObject()) {
                RE::Scaleform::GFx::Value lenVal;
                float lenFloat = 0.0f;
                if (clipsArr.GetMember("length"sv, &lenVal) && TryReadNumber(lenVal, lenFloat) &&
                    std::isfinite(lenFloat)) {
                    const int nClips = static_cast<int>(std::clamp(lenFloat, 0.0f, 8.0f));
                    for (int i = 0; i < nClips; ++i) {
                        const std::string k = std::to_string(i);
                        RE::Scaleform::GFx::Value clipObj;
                        if (!clipsArr.GetMember(k.c_str(), &clipObj) || !clipObj.IsObject() ||
                            !IsClipVisible(clipObj, false)) {
                            continue;
                        }
                        RE::Scaleform::GFx::Value dataVal;
                        std::string itemName;
                        if (clipObj.GetMember("data"sv, &dataVal) && dataVal.IsObject() &&
                            TryReadStringMember(dataVal, "text"sv, "itemName"sv, itemName) &&
                            tryAcceptIdentityItem(itemName)) {
                            return;
                        }
                        std::string tfName;
                        if (TryReadTextField(clipObj, "ItemName_tf"sv, tfName) &&
                            tryAcceptIdentityItem(tfName)) {
                            return;
                        }
                    }
                }
            }
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

        [[nodiscard]] const RE::TBO_InstanceData* ResolveItemInstanceData(
            const RE::TESBoundObject* a_obj,
            RE::ExtraDataList* a_stackExtra)
        {
            if (!a_obj) {
                return nullptr;
            }
            if (a_stackExtra) {
                if (auto* instExtra = a_stackExtra->GetByType<RE::ExtraInstanceData>()) {
                    if (instExtra->data && (!instExtra->base || instExtra->base == a_obj)) {
                        return instExtra->data.get();
                    }
                }
            }
            return a_obj->GetBaseInstanceData();
        }

        [[nodiscard]] bool ResolveInventoryStackDisplayName(
            RE::BGSInventoryItem& a_invItem,
            std::uint32_t a_stackIdx,
            RE::ExtraDataList* a_stackExtra,
            const std::string& a_baseName,
            bool a_hasBaseName,
            bool a_allowInstancedFullName,
            std::string& a_outName)
        {
            a_outName = a_baseName;
            bool hasExplicitDisplayName = a_hasBaseName;
            if (a_stackExtra) {
                auto* textExtra = a_stackExtra->GetByType<RE::ExtraTextDisplayData>();
                const char* overrideRaw = (textExtra && !textExtra->displayName.empty())
                    ? textExtra->displayName.c_str()
                    : nullptr;
                if (overrideRaw && !TrimWhitespace(overrideRaw).empty()) {
                    a_outName = overrideRaw;
                    hasExplicitDisplayName = true;
                } else if (a_hasBaseName) {
                    const char* full = a_invItem.GetDisplayFullName(a_stackIdx);
                    if (full && full[0] != '\0') {
                        a_outName = full;
                    }
                }
            } else if (a_hasBaseName && a_allowInstancedFullName) {
                const char* full = a_invItem.GetDisplayFullName(a_stackIdx);
                if (full && full[0] != '\0') {
                    a_outName = full;
                }
            }

            const std::string trimmedDisplay = TrimWhitespace(a_outName);
            return hasExplicitDisplayName &&
                   !trimmedDisplay.empty() &&
                   !TargetInfo::CleanDisplayName(trimmedDisplay, true).empty();
        }

        void ReadQuickContainerHeader(
            const RE::Scaleform::GFx::Value& a_qcClip,
            const std::string& a_fallbackHeader,
            const EngineQuickContainerSnapshot& a_engineSnap,
            QuickContainerOverlay& a_out)
        {
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
        }

        void ReadQuickContainerButtons(
            const RE::Scaleform::GFx::Value& a_qcClip,
            const EngineQuickContainerSnapshot& a_engineSnap,
            QuickContainerOverlay& a_out)
        {
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

        [[nodiscard]] std::uintptr_t ResolveVtableAddressSafe(REL::ID a_id)
        {
            const auto resolved = REL::IDDatabase::get().resolve(a_id);
            if (!resolved.rva || *resolved.rva == 0) {
                return 0;
            }
            return REL::Module::get().base() + *resolved.rva;
        }

        [[nodiscard]] bool IsWeapInstanceData(const RE::TBO_InstanceData* a_ptr)
        {
            if (!a_ptr) {
                return false;
            }
            static const std::uintptr_t weapInstVtbl =
                ResolveVtableAddressSafe(RE::TESObjectWEAP::InstanceData::VTABLE[0]);
            static const std::uintptr_t weapDataVtbl =
                ResolveVtableAddressSafe(RE::TESObjectWEAP::Data::VTABLE[0]);
            const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(a_ptr);
            return (weapInstVtbl != 0 && vtbl == weapInstVtbl) ||
                   (weapDataVtbl != 0 && vtbl == weapDataVtbl);
        }

        [[nodiscard]] bool IsArmoInstanceData(const RE::TBO_InstanceData* a_ptr)
        {
            if (!a_ptr) {
                return false;
            }
            static const std::uintptr_t armoInstVtbl =
                ResolveVtableAddressSafe(RE::TESObjectARMO::InstanceData::VTABLE[0]);
            const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(a_ptr);
            return armoInstVtbl != 0 && vtbl == armoInstVtbl;
        }

        struct CachedQuickLootKeywords
        {
            RE::BGSKeyword* weapGrenade{ nullptr };
            RE::BGSKeyword* weapMine{ nullptr };
            RE::BGSKeyword* weapThrown{ nullptr };
            RE::BGSKeyword* weapHeavy{ nullptr };
            RE::BGSKeyword* weapExplosive{ nullptr };
            RE::BGSKeyword* weapMinigun{ nullptr };
            RE::BGSKeyword* weapMissile{ nullptr };
            RE::BGSKeyword* weapFatman{ nullptr };
            RE::BGSKeyword* weapFlamer{ nullptr };
            RE::BGSKeyword* weapGatlingLaser{ nullptr };
            RE::BGSKeyword* weapBroadsider{ nullptr };
            RE::BGSKeyword* weapJunkJet{ nullptr };
            RE::BGSKeyword* weapCryolater{ nullptr };
            RE::BGSKeyword* weapMelee1H{ nullptr };
            RE::BGSKeyword* weapMelee2H{ nullptr };
            RE::BGSKeyword* weapUnarmed{ nullptr };
            RE::BGSKeyword* weapHandToHand{ nullptr };
            RE::BGSKeyword* weapRipper{ nullptr };
            RE::BGSKeyword* weapShishkebab{ nullptr };
            RE::BGSKeyword* weapPistol{ nullptr };
            RE::BGSKeyword* weapRifle{ nullptr };
            RE::BGSKeyword* weapShotgun{ nullptr };
            RE::BGSKeyword* weapAssaultRifle{ nullptr };
            RE::BGSKeyword* weapSniper{ nullptr };
            RE::BGSKeyword* weapAlienBlaster{ nullptr };
            RE::BGSKeyword* weapFlareGun{ nullptr };
            RE::BGSKeyword* weapGammaGun{ nullptr };

            RE::BGSKeyword* armoPower{ nullptr };
            RE::BGSKeyword* objArmor{ nullptr };
            RE::BGSKeyword* armoHelmet{ nullptr };
            RE::BGSKeyword* armoHat{ nullptr };

            RE::BGSKeyword* objStimpak{ nullptr };
            RE::BGSKeyword* objChem{ nullptr };
            RE::BGSKeyword* caChemBad{ nullptr };
            RE::BGSKeyword* objDrink{ nullptr };
            RE::BGSKeyword* objWater{ nullptr };
            RE::BGSKeyword* objNukaCola{ nullptr };
            RE::BGSKeyword* objAlcohol{ nullptr };
            RE::BGSKeyword* objCaffeinated{ nullptr };
            RE::BGSKeyword* objExtraCaffeinated{ nullptr };
            RE::BGSKeyword* caDrink{ nullptr };
            RE::BGSKeyword* objFood{ nullptr };
        };

        [[nodiscard]] const CachedQuickLootKeywords& GetCachedQuickLootKeywords()
        {
            static const CachedQuickLootKeywords s_kw = []() {
                auto findKw = [](const char* a_editorID) -> RE::BGSKeyword* {
                    return RE::TESForm::GetFormByEditorID<RE::BGSKeyword>(RE::BSFixedString(a_editorID));
                };
                CachedQuickLootKeywords k{};
                k.weapGrenade = findKw("WeaponTypeGrenade");
                k.weapMine = findKw("WeaponTypeMine");
                k.weapThrown = findKw("WeaponTypeThrown");
                k.weapHeavy = findKw("WeaponTypeHeavyGun");
                k.weapExplosive = findKw("WeaponTypeExplosive");
                k.weapMinigun = findKw("WeaponTypeMinigun");
                k.weapMissile = findKw("WeaponTypeMissileLauncher");
                k.weapFatman = findKw("WeaponTypeFatman");
                k.weapFlamer = findKw("WeaponTypeFlamer");
                k.weapGatlingLaser = findKw("WeaponTypeGatlingLaser");
                k.weapBroadsider = findKw("WeaponTypeBroadsider");
                k.weapJunkJet = findKw("WeaponTypeJunkJet");
                k.weapCryolater = findKw("WeaponTypeCryolater");
                k.weapMelee1H = findKw("WeaponTypeMelee1H");
                k.weapMelee2H = findKw("WeaponTypeMelee2H");
                k.weapUnarmed = findKw("WeaponTypeUnarmed");
                k.weapHandToHand = findKw("WeaponTypeHandToHand");
                k.weapRipper = findKw("WeaponTypeRipper");
                k.weapShishkebab = findKw("WeaponTypeShishkebab");
                k.weapPistol = findKw("WeaponTypePistol");
                k.weapRifle = findKw("WeaponTypeRifle");
                k.weapShotgun = findKw("WeaponTypeShotgun");
                k.weapAssaultRifle = findKw("WeaponTypeAssaultRifle");
                k.weapSniper = findKw("WeaponTypeSniper");
                k.weapAlienBlaster = findKw("WeaponTypeAlienBlaster");
                k.weapFlareGun = findKw("WeaponTypeFlareGun");
                k.weapGammaGun = findKw("WeaponTypeGammaGun");

                k.armoPower = findKw("ArmorTypePower");
                k.objArmor = findKw("ObjectTypeArmor");
                k.armoHelmet = findKw("ArmorTypeHelmet");
                k.armoHat = findKw("ArmorTypeHat");

                k.objStimpak = findKw("ObjectTypeStimpak");
                k.objChem = findKw("ObjectTypeChem");
                k.caChemBad = findKw("CA_ObjType_ChemBad");
                k.objDrink = findKw("ObjectTypeDrink");
                k.objWater = findKw("ObjectTypeWater");
                k.objNukaCola = findKw("ObjectTypeNukaCola");
                k.objAlcohol = findKw("ObjectTypeAlcohol");
                k.objCaffeinated = findKw("ObjectTypeCaffeinated");
                k.objExtraCaffeinated = findKw("ObjectTypeExtraCaffeinated");
                k.caDrink = findKw("CA_ObjType_Drink");
                k.objFood = findKw("ObjectTypeFood");
                return k;
            }();
            return s_kw;
        }

        [[nodiscard]] bool HasKeywordPtr(
            const RE::BGSKeywordForm* a_baseKw,
            const RE::TBO_InstanceData* a_instData,
            const RE::BGSKeyword* a_kw) noexcept
        {
            return a_baseKw && a_kw && a_baseKw->HasKeyword(a_kw, a_instData);
        }

        [[nodiscard]] bool KeywordListContainsAny(
            const RE::BGSKeywordForm* a_activeKw,
            std::initializer_list<std::string_view> a_needles) noexcept
        {
            if (!a_activeKw || !a_activeKw->keywords) {
                return false;
            }
            const std::uint32_t count = a_activeKw->GetNumKeywords();
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto optKw = a_activeKw->GetKeywordAt(i);
                if (!optKw.has_value() || !optKw.value()) {
                    continue;
                }
                const char* rawId = optKw.value()->formEditorID.c_str();
                if (!rawId || rawId[0] == '\0') {
                    continue;
                }
                const std::string_view edid(rawId);
                for (const auto needle : a_needles) {
                    if (ContainsAsciiCaseInsensitive(edid, needle)) {
                        return true;
                    }
                }
            }
            return false;
        }

        [[nodiscard]] QuickLootCategory ClassifyQuickLootCategory(
            const RE::TESBoundObject* a_obj,
            const RE::TBO_InstanceData* a_instData) noexcept
        {
            if (!a_obj) {
                return QuickLootCategory::Misc;
            }

            switch (a_obj->GetFormType()) {
            case RE::ENUM_FORM_ID::kKEYM:
                return QuickLootCategory::Key;
            case RE::ENUM_FORM_ID::kAMMO:
                return QuickLootCategory::Ammo;
            case RE::ENUM_FORM_ID::kALCH: {
                const auto* alch = a_obj->As<RE::AlchemyItem>();
                const auto& kw = GetCachedQuickLootKeywords();
                const RE::BGSKeywordForm* baseKw = alch ? static_cast<const RE::BGSKeywordForm*>(alch) : nullptr;
                const std::uint32_t flags = alch ? alch->data.flags : 0u;
                const bool isMedicineFlag = (flags & 0x00010000u) != 0u;
                const bool isFoodFlag = (flags & 0x00000002u) != 0u;
                const bool hasAddiction = (alch != nullptr) &&
                    (alch->data.addictionItem != nullptr || alch->data.addictionChance > 0.0f);

                bool hasMedicineKw = HasKeywordPtr(baseKw, a_instData, kw.objStimpak);
                bool hasDrinkKw =
                    HasKeywordPtr(baseKw, a_instData, kw.objDrink) ||
                    HasKeywordPtr(baseKw, a_instData, kw.objWater) ||
                    HasKeywordPtr(baseKw, a_instData, kw.objNukaCola) ||
                    HasKeywordPtr(baseKw, a_instData, kw.objAlcohol) ||
                    HasKeywordPtr(baseKw, a_instData, kw.objCaffeinated) ||
                    HasKeywordPtr(baseKw, a_instData, kw.objExtraCaffeinated) ||
                    HasKeywordPtr(baseKw, a_instData, kw.caDrink);
                bool hasChemKw =
                    HasKeywordPtr(baseKw, a_instData, kw.objChem) ||
                    HasKeywordPtr(baseKw, a_instData, kw.caChemBad);
                bool hasFoodKw = HasKeywordPtr(baseKw, a_instData, kw.objFood);

                if (!isMedicineFlag && !hasMedicineKw && !hasDrinkKw && !hasChemKw && !hasFoodKw && baseKw) {
                    hasMedicineKw = KeywordListContainsAny(baseKw, { "Stimpak", "Medicine", "Medkit", "FirstAid", "Antibiotic" });
                    hasDrinkKw = KeywordListContainsAny(baseKw, { "Drink", "Beverage", "Water", "Alcohol", "NukaCola", "Beer", "Liquor", "Soda" });
                    hasChemKw = KeywordListContainsAny(baseKw, { "Chem", "Drug", "Narcotic" });
                    hasFoodKw = KeywordListContainsAny(baseKw, { "Food", "Meal", "Meat", "Fruit", "Vegetable" });
                }

                return ClassifyAidQuickLootCategory(
                    isMedicineFlag,
                    isFoodFlag,
                    hasMedicineKw,
                    hasChemKw,
                    hasDrinkKw,
                    hasFoodKw,
                    hasAddiction);
            }
            case RE::ENUM_FORM_ID::kWEAP: {
                const auto* weap = a_obj->As<RE::TESObjectWEAP>();
                if (!weap) {
                    return QuickLootCategory::Rifle;
                }
                const auto* weapInst = IsWeapInstanceData(a_instData)
                    ? static_cast<const RE::TESObjectWEAP::InstanceData*>(a_instData)
                    : &weap->weaponData;
                const std::uint8_t wType = weapInst ? static_cast<std::uint8_t>(weapInst->type.get()) : 9u;
                const auto& kw = GetCachedQuickLootKeywords();
                const RE::BGSKeywordForm* baseKw = static_cast<const RE::BGSKeywordForm*>(weap);
                const RE::BGSKeywordForm* activeKw = (weapInst && weapInst->keywords) ? weapInst->keywords : baseKw;

                bool hasGrenadeOrMineKw =
                    HasKeywordPtr(baseKw, a_instData, kw.weapGrenade) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapMine) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapThrown);
                bool hasHeavyKw =
                    HasKeywordPtr(baseKw, a_instData, kw.weapHeavy) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapMinigun) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapMissile) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapFatman) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapFlamer) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapGatlingLaser) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapBroadsider) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapJunkJet) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapCryolater);
                bool hasExplosiveKw = HasKeywordPtr(baseKw, a_instData, kw.weapExplosive);
                bool hasMeleeKw =
                    HasKeywordPtr(baseKw, a_instData, kw.weapMelee1H) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapMelee2H) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapUnarmed) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapHandToHand) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapRipper) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapShishkebab);
                bool hasPistolKw =
                    HasKeywordPtr(baseKw, a_instData, kw.weapPistol) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapAlienBlaster) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapFlareGun) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapGammaGun);
                bool hasRifleKw =
                    HasKeywordPtr(baseKw, a_instData, kw.weapRifle) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapShotgun) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapAssaultRifle) ||
                    HasKeywordPtr(baseKw, a_instData, kw.weapSniper);

                if (!hasGrenadeOrMineKw && !hasHeavyKw && !hasExplosiveKw &&
                    !hasMeleeKw && !hasPistolKw && !hasRifleKw && activeKw) {
                    hasGrenadeOrMineKw = KeywordListContainsAny(activeKw, { "Grenade", "Mine", "Thrown" });
                    hasHeavyKw = KeywordListContainsAny(activeKw, { "HeavyGun", "HeavyWeapon", "Minigun", "Launcher", "Fatman", "Flamer", "Gatling" });
                    hasExplosiveKw = KeywordListContainsAny(activeKw, { "Explosive" });
                    hasMeleeKw = KeywordListContainsAny(activeKw, { "Melee", "Unarmed", "HandToHand", "Sword", "Blade" });
                    hasPistolKw = KeywordListContainsAny(activeKw, { "Pistol", "Handgun", "Sidearm", "Revolver" });
                    hasRifleKw = KeywordListContainsAny(activeKw, { "Rifle", "Shotgun", "Carbine", "Sniper", "Submachine", "SMG" });
                }

                return ClassifyWeaponQuickLootCategory(
                    wType,
                    hasGrenadeOrMineKw,
                    hasHeavyKw,
                    hasExplosiveKw,
                    hasMeleeKw,
                    hasPistolKw,
                    hasRifleKw);
            }
            case RE::ENUM_FORM_ID::kARMO: {
                const auto* armo = a_obj->As<RE::TESObjectARMO>();
                if (!armo) {
                    return QuickLootCategory::Armor;
                }
                const auto* armoInst = IsArmoInstanceData(a_instData)
                    ? static_cast<const RE::TESObjectARMO::InstanceData*>(a_instData)
                    : &armo->data;
                const std::uint16_t rating = armoInst ? armoInst->rating : armo->data.rating;
                const auto* dmgTypes = (armoInst && armoInst->damageTypes) ? armoInst->damageTypes : armo->data.damageTypes;
                const bool hasExtraResist = (dmgTypes != nullptr && !dmgTypes->empty());

                const auto& kw = GetCachedQuickLootKeywords();
                const RE::BGSKeywordForm* baseKw = static_cast<const RE::BGSKeywordForm*>(armo);
                const RE::BGSKeywordForm* activeKw = (armoInst && armoInst->keywords) ? armoInst->keywords : baseKw;

                bool hasPowerArmorKw = HasKeywordPtr(baseKw, a_instData, kw.armoPower);
                bool hasArmorKw =
                    HasKeywordPtr(baseKw, a_instData, kw.objArmor) ||
                    HasKeywordPtr(baseKw, a_instData, kw.armoHelmet);
                bool hasClothingKw = HasKeywordPtr(baseKw, a_instData, kw.armoHat);

                if (!hasPowerArmorKw && activeKw) {
                    hasPowerArmorKw = KeywordListContainsAny(activeKw, { "PowerArmor", "ArmorTypePower" });
                }
                if (!hasArmorKw && activeKw) {
                    hasArmorKw = KeywordListContainsAny(activeKw, { "ObjectTypeArmor", "ArmorTypeHelmet", "ArmorBody", "ArmorLimb" });
                }
                if (!hasClothingKw && activeKw) {
                    hasClothingKw = KeywordListContainsAny(activeKw, { "Clothing", "Outfit", "Apparel", "Hat", "Glasses", "Underarmor" });
                }

                return ClassifyArmorQuickLootCategory(
                    hasPowerArmorKw,
                    hasArmorKw,
                    hasClothingKw,
                    rating,
                    hasExtraResist);
            }
            case RE::ENUM_FORM_ID::kOMOD:
                return QuickLootCategory::Mod;
            case RE::ENUM_FORM_ID::kBOOK:
                return QuickLootCategory::BookNote;
            case RE::ENUM_FORM_ID::kNOTE:
                return QuickLootCategory::Holotape;
            case RE::ENUM_FORM_ID::kMISC: {
                const auto* misc = a_obj->As<RE::TESObjectMISC>();
                if (misc && misc->IsLooseMod()) {
                    return QuickLootCategory::Mod;
                }
                if (misc && misc->componentData && !misc->componentData->empty()) {
                    return QuickLootCategory::Junk;
                }
                return QuickLootCategory::Misc;
            }
            default:
                return QuickLootCategory::Misc;
            }
        }

        [[nodiscard]] bool IsObjectTaggedForSearch(
            const RE::TESBoundObject* a_obj,
            RE::FavoritesManager* a_favMgr) noexcept
        {
            if (!a_obj || !a_favMgr || a_favMgr->favoritedComponents.empty()) {
                return false;
            }
            if (const auto* misc = a_obj->As<RE::TESObjectMISC>(); misc && misc->componentData) {
                for (const auto& comp : *misc->componentData) {
                    if (comp.first) {
                        if (const auto* compBound = comp.first->As<RE::TESBoundObject>();
                            compBound && a_favMgr->favoritedComponents.contains(compBound)) {
                            return true;
                        }
                    }
                }
            } else if (a_obj->GetFormType() == RE::ENUM_FORM_ID::kCMPO) {
                return a_favMgr->favoritedComponents.contains(a_obj);
            }
            return false;
        }

        [[nodiscard]] bool IsStackQuestObject(RE::ExtraDataList* a_stackExtra) noexcept
        {
            if (!a_stackExtra) {
                return false;
            }
            if (auto* aliasArr = a_stackExtra->GetByType<RE::ExtraAliasInstanceArray>()) {
                RE::BSAutoReadLock aliasLock{ aliasArr->aliasArrayLock };
                for (const auto& inst : aliasArr->aliasArray) {
                    if (inst.alias && inst.alias->IsQuestObject()) {
                        return true;
                    }
                }
            }
            return false;
        }

        void ReadQuickContainerData(
            Controller& a_controller,
            std::uint32_t a_ownerFormID,
            const RE::Scaleform::GFx::Value& a_qcClip,
            const std::string& a_fallbackHeader,
            const EngineQuickContainerSnapshot& a_engineSnap,
            const CachedInventorySnapshot& a_invSnap,
            int a_maxVisibleRows,
            bool a_expandInventoryList,
            int a_prevSelectedIdx,
            QuickContainerOverlay& a_out)
        {
            // 1. Header title
            ReadQuickContainerHeader(a_qcClip, a_fallbackHeader, a_engineSnap, a_out);

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

                        bool isSel =
                            TryReadBoolMember(clipObj, "selected"sv, "_selected"sv, false) ||
                            TryReadBoolMember(clipObj, "Selected"sv, ""sv, false);
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
                          TryReadPositiveIndex(a_qcClip, "selectedClipIndex"sv, sfNumericSel) ||
                          TryReadPositiveIndex(a_qcClip, "SelectedIndex"sv, sfNumericSel) ||
                          TryReadPositiveIndex(a_qcClip, "SelectedClipIndex"sv, sfNumericSel) ||
                          TryReadPositiveIndex(a_qcClip, "iSelectedClipIndex"sv, sfNumericSel))) {
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
                    } else if (sfNumericSel >= 0 && !(sfNumericSel == 0 && activeClipSlot > 0)) {
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
                        EnrichWindowFromSnapshot(a_out.items, a_invSnap);
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
                            EnrichWindowFromSnapshot(a_out.items, a_invSnap);
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
            ReadQuickContainerButtons(a_qcClip, a_engineSnap, a_out);
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

        inline constexpr std::uint32_t kCurrentCompanionFactionFormID = 0x00023C01u;

        [[nodiscard]] bool IsRefDisabled(const RE::TESObjectREFR* a_ref) noexcept
        {
            return a_ref != nullptr && (a_ref->GetFormFlags() & (1u << 11)) != 0u;
        }

        [[nodiscard]] bool IsActorDeadOrDying(const RE::Actor* a_actor) noexcept
        {
            if (!a_actor) {
                return false;
            }
            if (a_actor->IsDead(true)) {
                return true;
            }
            const auto life = static_cast<RE::ACTOR_LIFE_STATE>(a_actor->lifeState);
            return life == RE::ACTOR_LIFE_STATE::kDead || life == RE::ACTOR_LIFE_STATE::kDying;
        }

        [[nodiscard]] bool IsActorBleedout(const RE::Actor* a_actor) noexcept
        {
            if (!a_actor) {
                return false;
            }
            const auto life = static_cast<RE::ACTOR_LIFE_STATE>(a_actor->lifeState);
            if (life == RE::ACTOR_LIFE_STATE::kBleedout || life == RE::ACTOR_LIFE_STATE::kEssentialDown) {
                return true;
            }
            return a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kInBleedoutAnimation);
        }

        [[nodiscard]] bool IsCompanionActor(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
        {
            if (!a_actor || !a_player || a_actor == a_player) {
                return false;
            }
            if (a_actor->GetHostileToActor(a_player)) {
                return false;
            }
            static RE::TESFaction* s_companionFaction = nullptr;
            if (!s_companionFaction) {
                s_companionFaction = RE::TESForm::GetFormByID<RE::TESFaction>(kCurrentCompanionFactionFormID);
            }
            if (s_companionFaction && a_actor->IsInFaction(s_companionFaction)) {
                return true;
            }
            if (a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kCanDoFavor) ||
                a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kIsCommandedActor)) {
                return true;
            }
            return a_actor->IsFollowing();
        }

        inline void SwallowButtonEvent(RE::InputEvent* a_ev, RE::ButtonEvent* a_btn) noexcept
        {
            if (a_ev) {
                a_ev->handled = RE::InputEvent::HANDLED_RESULT::kStop;
            }
            if (a_btn) {
                a_btn->disabled = true;
            }
        }

        [[nodiscard]] bool IsEventStoppedOrDisabled(const RE::InputEvent* a_event) noexcept
        {
            if (!a_event) {
                return false;
            }
            if (a_event->handled.get() == RE::InputEvent::HANDLED_RESULT::kStop) {
                return true;
            }
            if (const auto* btn = a_event->As<RE::ButtonEvent>()) {
                return btn->QDisabled();
            }
            return false;
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
        EnsureActivateHook();
        EnsureMenuControlsHook();
        EnsurePlayerControlsHooks();
        EnsureUIInputHook();
        EnsureHUDDataModelHook();
    }

    void Controller::EnsureMenuControlsHook()
    {
        if (_menuControlsInputHooked.load(std::memory_order_acquire)) {
            return;
        }

        auto* mc = RE::MenuControls::GetSingleton();
        if (!mc) {
            return;
        }

        if (!_menuControlsInputHooked.exchange(true)) {
            auto* receiver = static_cast<RE::BSInputEventReceiver*>(mc);
            const auto vtableAddr = *reinterpret_cast<const std::uintptr_t*>(receiver);
            const auto origSlot = reinterpret_cast<const std::uintptr_t*>(vtableAddr)[0];
            _origMenuControlsPerformInputProcessing.store(origSlot, std::memory_order_release);
            (void)REL::Relocation<std::uintptr_t>{ vtableAddr }.write_vfunc(0, &Hook_MenuControlsPerformInputProcessing);
            logger::info("Hooked MenuControls::PerformInputProcessing at vtable {:X}", vtableAddr);
        }
    }

    void Controller::EnsureUIInputHook()
    {
        if (_uiInputHooked.load(std::memory_order_acquire)) {
            return;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return;
        }

        if (!_uiInputHooked.exchange(true)) {
            auto* receiver = static_cast<RE::BSInputEventReceiver*>(ui);
            const auto vtableAddr = *reinterpret_cast<const std::uintptr_t*>(receiver);
            const auto origSlot = reinterpret_cast<const std::uintptr_t*>(vtableAddr)[0];
            _origUIPerformInputProcessing.store(origSlot, std::memory_order_release);
            (void)REL::Relocation<std::uintptr_t>{ vtableAddr }.write_vfunc(0, &Hook_UIPerformInputProcessing);
            logger::info("Hooked UI::PerformInputProcessing at vtable {:X}", vtableAddr);
        }
    }

    void Controller::EnsureHUDDataModelHook()
    {
        if (_hudDataModelInputHooked.load(std::memory_order_acquire)) {
            return;
        }

        const auto resolved = REL::IDDatabase::get().resolve(RE::VTABLE::HUDDataModel[0]);
        if (!resolved.rva || *resolved.rva == 0) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                logger::warn("Could not resolve HUDDataModel vtable; skipping input hook");
            }
            return;
        }

        const auto vtableAddr = REL::Module::get().base() + *resolved.rva;
        if (vtableAddr == 0) {
            return;
        }

        if (!_hudDataModelInputHooked.exchange(true)) {
            const auto origSlot = *reinterpret_cast<const std::uintptr_t*>(vtableAddr);
            _origHUDDataModelPerformInputProcessing.store(origSlot, std::memory_order_release);
            (void)REL::Relocation<std::uintptr_t>{ vtableAddr }.write_vfunc(0, &Hook_HUDDataModelPerformInputProcessing);
            logger::info("Hooked HUDDataModel::PerformInputProcessing at vtable {:X}", vtableAddr);
        }
    }

    void Controller::EnsurePlayerControlsHooks()
    {
        if (_playerControlsInputHooked.load(std::memory_order_acquire) &&
            _readyWeaponHooked.load(std::memory_order_acquire)) {
            return;
        }

        auto* pc = RE::PlayerControls::GetSingleton();
        if (!pc) {
            return;
        }

        if (!_playerControlsInputHooked.exchange(true)) {
            // PlayerControls inherits BSInputEventReceiver privately at offset 0x0.
            const auto vtableAddr = *reinterpret_cast<const std::uintptr_t*>(pc);
            const auto origSlot = reinterpret_cast<const std::uintptr_t*>(vtableAddr)[0];
            _origPlayerControlsPerformInputProcessing.store(origSlot, std::memory_order_release);
            (void)REL::Relocation<std::uintptr_t>{ vtableAddr }.write_vfunc(0, &Hook_PlayerControlsPerformInputProcessing);
            logger::info("Hooked PlayerControls::PerformInputProcessing at vtable {:X}", vtableAddr);
        }

        if (pc->readyWeaponHandler && !_readyWeaponHooked.exchange(true)) {
            auto* user = reinterpret_cast<RE::BSInputEventUser*>(pc->readyWeaponHandler);
            bool isHeldState = false;
            for (const auto* h : pc->heldStateHandlers) {
                if (reinterpret_cast<const void*>(h) == reinterpret_cast<const void*>(user)) {
                    isHeldState = true;
                    break;
                }
            }
            _readyWeaponIsHeldStateHandler.store(isHeldState, std::memory_order_release);

            const auto vtableAddr = *reinterpret_cast<const std::uintptr_t*>(pc->readyWeaponHandler);
            const auto origSlot = reinterpret_cast<const std::uintptr_t*>(vtableAddr)[1];
            _origReadyWeaponShouldHandleEvent.store(origSlot, std::memory_order_release);
            (void)REL::Relocation<std::uintptr_t>{ vtableAddr }.write_vfunc(1, &Hook_ReadyWeaponShouldHandleEvent);
            logger::info(
                "Hooked ReadyWeaponHandler::ShouldHandleEvent at vtable {:X} (heldState={})",
                vtableAddr,
                isHeldState);
        }
    }

    void Controller::LogSuppressedReadyDiag(
        const char* a_sourceTag,
        const char* a_reason,
        std::string_view a_userEvent,
        bool a_blockCompReady,
        bool a_blockOrphanSec,
        bool a_weaponDrawn,
        int a_actionResult)
    {
        auto& ctrl = GetSingleton();
        const std::uint32_t count = ctrl._diagReadyBlockLogCount.load(std::memory_order_relaxed);
        if (count >= 24u) {
            return;
        }
        const std::int64_t nowMs = SteadyNowMs();
        const bool isActionLog = (a_reason != nullptr && std::string_view(a_reason) == "doAction"sv);
        auto& rateAtom = isActionLog ? ctrl._diagReadyLastActionLogMs : ctrl._diagReadyLastLogMs;
        const std::int64_t minIntervalMs = isActionLog ? 100 : 500;
        const std::int64_t prevMs = rateAtom.load(std::memory_order_relaxed);
        if (prevMs > 0 && (nowMs - prevMs) < minIntervalMs) {
            return;
        }
        rateAtom.store(nowMs, std::memory_order_relaxed);
        ctrl._diagReadyBlockLogCount.fetch_add(1, std::memory_order_relaxed);
        logger::info(
            "DiagReady[{}] {}: ev='{}' compReady={} orphanSec={} drawn={} res={}",
            a_sourceTag ? a_sourceTag : "?",
            a_reason ? a_reason : "?",
            a_userEvent,
            a_blockCompReady,
            a_blockOrphanSec,
            a_weaponDrawn,
            a_actionResult);
    }

    void Controller::ExecuteReadyWeaponAction(SuppressedReadyAction a_action, const char* a_sourceTag)
    {
        if (a_action == SuppressedReadyAction::kNone) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* pc = RE::PlayerControls::GetSingleton();
        const bool canCustomReadyAction =
            (player != nullptr && pc != nullptr && !pc->blockPlayerInput &&
             pc->readyWeaponHandler != nullptr &&
             reinterpret_cast<const RE::BSInputEventUser*>(pc->readyWeaponHandler)->inputEventHandlingEnabled);
        if (!canCustomReadyAction) {
            return;
        }

        RE::DEFAULT_OBJECT defAction = RE::DEFAULT_OBJECT::kActionDraw;
        if (a_action == SuppressedReadyAction::kReload) {
            defAction = RE::DEFAULT_OBJECT::kActionReload;
        } else if (a_action == SuppressedReadyAction::kSheath) {
            defAction = RE::DEFAULT_OBJECT::kActionSheath;
        }
        const bool ok = pc->DoAction(defAction, RE::ActionInput::ACTIONPRIORITY::kTry);
        LogSuppressedReadyDiag(
            a_sourceTag,
            "doAction",
            (a_action == SuppressedReadyAction::kReload) ? "Reload"sv :
                ((a_action == SuppressedReadyAction::kSheath) ? "Sheath"sv : "Draw"sv),
            GetSingleton()._suppressCompanionReady.load(std::memory_order_acquire),
            GetSingleton()._blockOrphanSecondaryActivate.load(std::memory_order_acquire),
            player->GetWeaponMagicDrawn(),
            ok ? 1 : 0);
    }

    SuppressedReadyAction Controller::ProcessInputEventQueue(
        const RE::InputEvent* a_queueHead,
        const char* a_sourceTag)
    {
        auto* ui = RE::UI::GetSingleton();
        auto& ctrl = GetSingleton();
        const bool inMenuMode = (ui && ui->GetMenuModeCount() > 0);
        const bool blockingWindowOpen = MenuFrameworkUI::IsAnyBlockingWindowOpen();
        SuppressedReadyAction pendingAction = SuppressedReadyAction::kNone;

        if (inMenuMode) {
            const bool anyLatchSet =
                ctrl._suppressedReadyHeld.load(std::memory_order_acquire) ||
                ctrl._suppressedReadyHolstered.load(std::memory_order_acquire) ||
                ctrl._customQcTakeLatched.load(std::memory_order_acquire) ||
                ctrl._tpAssistActivateLatched.load(std::memory_order_acquire) ||
                ctrl._holdPrimaryActive.load(std::memory_order_acquire) ||
                ctrl._holdPrimaryCompleted.load(std::memory_order_acquire) ||
                ctrl._holdSecondaryActive.load(std::memory_order_acquire) ||
                ctrl._holdSecondaryCompleted.load(std::memory_order_acquire) ||
                ctrl._holdWaterTapStage.load(std::memory_order_acquire) != 0 ||
                ctrl._holdSecTapStage.load(std::memory_order_acquire) != 0 ||
                ctrl._holdSecQueueHead.load(std::memory_order_acquire) != nullptr;
            if (anyLatchSet) {
                ctrl._inputResetEpoch.fetch_add(1, std::memory_order_acq_rel);
                ctrl._customQcLastTakePressMs.store(0, std::memory_order_relaxed);
                ctrl._customQcLastReleaseMs.store(0, std::memory_order_relaxed);
                ctrl._customQcLatchSetMs.store(0, std::memory_order_relaxed);
                ctrl._customQcTakeDevice.store(0xFFFFFFFFu, std::memory_order_relaxed);
                ctrl._customQcTakeIdCode.store(0xFFFFFFFFu, std::memory_order_relaxed);
                ctrl._suppressedReadyStartMs.store(0, std::memory_order_relaxed);
                ctrl._suppressedReadyHolstered.store(false, std::memory_order_release);
                ctrl._suppressedReadyHeld.store(false, std::memory_order_release);
                ctrl._customQcTakeLatched.store(false, std::memory_order_release);
                ctrl._tpAssistActivateLatched.store(false, std::memory_order_release);
                ctrl._holdPrimaryActive.store(false, std::memory_order_release);
                ctrl._holdPrimaryCompleted.store(false, std::memory_order_release);
                ctrl._holdPrimaryProgress.store(0.0f, std::memory_order_release);
                ctrl._holdSecondaryActive.store(false, std::memory_order_release);
                ctrl._holdSecondaryCompleted.store(false, std::memory_order_release);
                ctrl._holdSecondaryProgress.store(0.0f, std::memory_order_release);
                ctrl._holdWaterTapStage.store(0, std::memory_order_release);
                ctrl._holdWaterTapTriggerMs.store(0, std::memory_order_relaxed);
                ctrl._holdWaterQueueHead.store(nullptr, std::memory_order_release);
                ctrl._holdSecTapStage.store(0, std::memory_order_release);
                ctrl._holdSecTapTriggerMs.store(0, std::memory_order_relaxed);
                ctrl._holdSecQueueHead.store(nullptr, std::memory_order_release);
                ctrl._inputResetEpoch.fetch_add(1, std::memory_order_acq_rel);
            }

            if (a_queueHead != nullptr && ctrl._postHoldGuardArmed.load(std::memory_order_acquire)) {
                const std::int64_t nowMs = SteadyNowMs();
                PostHoldMenuGuardState guardState{};
                guardState.armed = true;
                guardState.menuObserved = ctrl._postHoldGuardMenuObserved.load(std::memory_order_acquire);
                guardState.waitRelease = ctrl._postHoldGuardWaitRelease.load(std::memory_order_acquire);
                guardState.triggerMs = ctrl._postHoldGuardTriggerMs.load(std::memory_order_relaxed);
                guardState.menuOpenMs = ctrl._postHoldGuardMenuOpenMs.load(std::memory_order_relaxed);
                guardState.releaseMs = ctrl._postHoldGuardReleaseMs.load(std::memory_order_relaxed);
                guardState.boundDevice = ctrl._postHoldGuardDevice.load(std::memory_order_relaxed);
                guardState.boundIdCode = ctrl._postHoldGuardIdCode.load(std::memory_order_relaxed);

                for (auto* ev = const_cast<RE::InputEvent*>(a_queueHead); ev; ev = ev->next) {
                    if (ev->handled.get() == RE::InputEvent::HANDLED_RESULT::kStop) {
                        continue;
                    }
                    auto* btn = ev->As<RE::ButtonEvent>();
                    if (!btn || btn->QDisabled()) {
                        continue;
                    }
                    const auto& ue = btn->QUserEvent();
                    const auto dev = static_cast<std::uint32_t>(btn->device.get());
                    const auto idCode = btn->QIDCode();
                    const bool matchesPhysical =
                        (guardState.boundDevice != 0xFFFFFFFFu &&
                         dev == guardState.boundDevice &&
                         idCode == guardState.boundIdCode);
                    const bool isConfirmFamily =
                        (ue == "Accept"sv || ue == "Activate"sv || ue == "SpecialActivate"sv ||
                         ue == "QCTakeItem"sv || ue == "Confirm"sv || ue == "SecondaryActivate"sv ||
                         ue == "QCOpenTransferMenu"sv || ue == "ReadyWeapon"sv ||
                         ue == "XButton"sv || ue == "AButton"sv);
                    PostHoldMenuGuardInput gIn{};
                    gIn.inMenuMode = true;
                    gIn.isMatchingPhysicalKey = matchesPhysical;
                    gIn.isConfirmFamilyEvent = isConfirmFamily;
                    gIn.isPressed = (btn->QAnalogValue() != 0.0f);
                    gIn.isReleased = (btn->QAnalogValue() == 0.0f);
                    gIn.isJustPressed = btn->QJustPressed();
                    gIn.nowMs = nowMs;
                    if (StepPostHoldMenuGuard(guardState, gIn)) {
                        SwallowButtonEvent(ev, btn);
                    }
                    if (!guardState.armed) {
                        break;
                    }
                }

                if (!guardState.menuObserved && guardState.armed) {
                    PostHoldMenuGuardInput pingIn{};
                    pingIn.inMenuMode = true;
                    pingIn.nowMs = nowMs;
                    (void)StepPostHoldMenuGuard(guardState, pingIn);
                }

                ctrl._postHoldGuardMenuObserved.store(guardState.menuObserved, std::memory_order_release);
                ctrl._postHoldGuardWaitRelease.store(guardState.waitRelease, std::memory_order_release);
                ctrl._postHoldGuardMenuOpenMs.store(guardState.menuOpenMs, std::memory_order_relaxed);
                ctrl._postHoldGuardReleaseMs.store(guardState.releaseMs, std::memory_order_relaxed);
                ctrl._postHoldGuardArmed.store(guardState.armed, std::memory_order_release);
            }

            if (a_queueHead != nullptr && ui->GetMenuOpen("ExamineMenu"sv)) {
                auto exMenu = ui->GetMenu<RE::ExamineMenu>();
                const std::uint8_t rawFeatured = exMenu
                    ? *reinterpret_cast<const std::uint8_t*>(&exMenu->inspectingFeaturedItem)
                    : 0u;
                if (exMenu && rawFeatured == 1u) {
                    const std::int64_t nowMs = SteadyNowMs();
                    std::int64_t openMs = ctrl._featuredExamineOpenMs.load(std::memory_order_acquire);
                    if (openMs == 0) {
                        openMs = nowMs;
                        ctrl._featuredExamineOpenMs.store(openMs, std::memory_order_release);
                    }
                    const std::int64_t graceMs = ctrl._featuredExamineGraceMs.load(std::memory_order_relaxed);
                    bool waitRel = ctrl._featuredExamineWaitRelease.load(std::memory_order_acquire);

                    for (auto* ev = const_cast<RE::InputEvent*>(a_queueHead); ev; ev = ev->next) {
                        if (ev->handled.get() == RE::InputEvent::HANDLED_RESULT::kStop) {
                            continue;
                        }
                        auto* btn = ev->As<RE::ButtonEvent>();
                        if (!btn || btn->QDisabled()) {
                            continue;
                        }
                        const auto& ue = btn->QUserEvent();
                        const bool isDismiss =
                            (ue == "Accept"sv || ue == "Cancel"sv || ue == "Activate"sv ||
                             ue == "QCTakeItem"sv || ue == "SpecialActivate"sv ||
                             ue == "Back"sv || ue == "Close"sv || ue == "TweenMenu"sv);
                        if (!isDismiss) {
                            continue;
                        }
                        FeaturedExamineInput fin{};
                        fin.isFeaturedExamineOpen = true;
                        fin.isDismissEvent = true;
                        fin.isPressed = (btn->QAnalogValue() != 0.0f);
                        fin.isReleased = (btn->QAnalogValue() == 0.0f);
                        fin.isJustPressed = btn->QJustPressed();
                        fin.openTimeMs = openMs;
                        fin.nowMs = nowMs;
                        fin.graceMs = graceMs;
                        if (StepFeaturedExamineGuard(waitRel, fin)) {
                            SwallowButtonEvent(ev, btn);
                        }
                    }
                    ctrl._featuredExamineWaitRelease.store(waitRel, std::memory_order_release);
                }
            }
            return pendingAction;
        }

        if (blockingWindowOpen) {
            // While an ImGui overlay (F1) is open without pausing FO4, process physical key-ups only.
            if (a_queueHead != nullptr) {
                const std::uint64_t startEpoch = ctrl._inputResetEpoch.load(std::memory_order_acquire);
                bool latched = ctrl._customQcTakeLatched.load(std::memory_order_acquire);
                std::uint32_t boundDev = ctrl._customQcTakeDevice.load(std::memory_order_relaxed);
                std::uint32_t boundId = ctrl._customQcTakeIdCode.load(std::memory_order_relaxed);
                std::int64_t lastRelMs = ctrl._customQcLastReleaseMs.load(std::memory_order_relaxed);
                bool readyHeld = ctrl._suppressedReadyHeld.load(std::memory_order_acquire);
                bool dirtyTake = false;
                bool dirtyReady = false;
                bool dirtyHoldPri = false;
                bool dirtyHoldSec = false;

                for (const auto* ev = a_queueHead; ev; ev = ev->next) {
                    const auto* btn = ev->As<RE::ButtonEvent>();
                    if (!btn || btn->QDisabled() || btn->QAnalogValue() != 0.0f) {
                        continue;
                    }
                    const auto& userEvent = btn->QUserEvent();
                    const auto dev = static_cast<std::uint32_t>(btn->device.get());
                    const auto idCode = btn->QIDCode();
                    const bool isTakeFamily =
                        (userEvent == "QCTakeItem"sv || userEvent == "Activate"sv || userEvent == "SpecialActivate"sv);
                    if (latched && ((boundDev != 0xFFFFFFFFu && dev == boundDev && idCode == boundId) ||
                                    (boundDev == 0xFFFFFFFFu && isTakeFamily))) {
                        latched = false;
                        boundDev = 0xFFFFFFFFu;
                        boundId = 0xFFFFFFFFu;
                        lastRelMs = SteadyNowMs();
                        dirtyTake = true;
                    }
                    if (readyHeld &&
                        (userEvent == "SecondaryActivate"sv || userEvent == "ReadyWeapon"sv)) {
                        readyHeld = false;
                        dirtyReady = true;
                    }
                    if (isTakeFamily) {
                        dirtyHoldPri = true;
                    }
                    if (userEvent == "SecondaryActivate"sv || userEvent == "QCOpenTransferMenu"sv ||
                        userEvent == "ReadyWeapon"sv) {
                        dirtyHoldSec = true;
                    }
                }
                if ((dirtyTake || dirtyReady || dirtyHoldPri || dirtyHoldSec) &&
                    (startEpoch & 1u) == 0u &&
                    ctrl._inputResetEpoch.load(std::memory_order_acquire) == startEpoch) {
                    if (dirtyTake) {
                        ctrl._customQcTakeDevice.store(boundDev, std::memory_order_relaxed);
                        ctrl._customQcTakeIdCode.store(boundId, std::memory_order_relaxed);
                        ctrl._customQcLastReleaseMs.store(lastRelMs, std::memory_order_relaxed);
                        ctrl._customQcTakeLatched.store(latched, std::memory_order_release);
                    }
                    if (dirtyReady) {
                        ctrl._suppressedReadyHolstered.store(false, std::memory_order_release);
                        ctrl._suppressedReadyHeld.store(false, std::memory_order_release);
                    }
                    if (dirtyHoldPri) {
                        ctrl._holdPrimaryActive.store(false, std::memory_order_release);
                        ctrl._holdPrimaryCompleted.store(false, std::memory_order_release);
                        ctrl._holdPrimaryProgress.store(0.0f, std::memory_order_release);
                        ctrl._holdWaterTapStage.store(0, std::memory_order_release);
                        ctrl._holdWaterQueueHead.store(nullptr, std::memory_order_release);
                        ctrl._holdWaterTapTriggerMs.store(0, std::memory_order_relaxed);
                    }
                    if (dirtyHoldSec) {
                        ctrl._holdSecondaryActive.store(false, std::memory_order_release);
                        ctrl._holdSecondaryCompleted.store(false, std::memory_order_release);
                        ctrl._holdSecondaryProgress.store(0.0f, std::memory_order_release);
                        ctrl._holdSecTapStage.store(0, std::memory_order_release);
                        ctrl._holdSecQueueHead.store(nullptr, std::memory_order_release);
                        ctrl._holdSecTapTriggerMs.store(0, std::memory_order_relaxed);
                    }
                }
            }
            return pendingAction;
        }

        if (!ui || a_queueHead == nullptr) {
            return pendingAction;
        }

        const std::int64_t nowMs = SteadyNowMs();
        const std::uint64_t startEpoch = ctrl._inputResetEpoch.load(std::memory_order_acquire);
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* pc = RE::PlayerControls::GetSingleton();
        const bool isAlreadyGrabbing = (player && static_cast<bool>(player->grabbedObject));
        const bool inCommandModeNow = (player && static_cast<bool>(player->actorDoingPlayerCommand));
        const bool inWorkshopNow = ui->GetMenuOpen("WorkshopMenu"sv);
        const bool canCustomReadyAction =
            (player != nullptr && pc != nullptr && !pc->blockPlayerInput &&
             pc->readyWeaponHandler != nullptr &&
             reinterpret_cast<const RE::BSInputEventUser*>(pc->readyWeaponHandler)->inputEventHandlingEnabled);
        const bool blockCompReady = ctrl._suppressCompanionReady.load(std::memory_order_acquire);
        const bool blockCompActivate = ctrl._suppressCompanionActivate.load(std::memory_order_acquire);
        const bool blockOrphanSecondary =
            ctrl._blockOrphanSecondaryActivate.load(std::memory_order_acquire) &&
            !inCommandModeNow &&
            !inWorkshopNow;
        const bool customActive = ctrl._customQcActive.load(std::memory_order_acquire);
        const bool customTakeEnabled = ctrl._customQcTakeEnabled.load(std::memory_order_acquire);
        const std::int64_t takeDebounceMs = ctrl._customQcTakeDebounceMs.load(std::memory_order_relaxed);

        bool passHasBlockedSecondary = false;
        for (const auto* scanEv = a_queueHead; scanEv; scanEv = scanEv->next) {
            if (scanEv->handled.get() == RE::InputEvent::HANDLED_RESULT::kStop) {
                continue;
            }
            const auto* scanBtn = scanEv->As<RE::ButtonEvent>();
            if (!scanBtn || scanBtn->QDisabled()) {
                continue;
            }
            if (scanBtn->QUserEvent() == "SecondaryActivate"sv &&
                (blockCompReady || blockOrphanSecondary)) {
                passHasBlockedSecondary = true;
                break;
            }
        }

        SuppressedReadyState readyState{};
        readyState.held = ctrl._suppressedReadyHeld.load(std::memory_order_acquire);
        readyState.holstered = ctrl._suppressedReadyHolstered.load(std::memory_order_acquire);
        readyState.startMs = ctrl._suppressedReadyStartMs.load(std::memory_order_relaxed);
        const bool wasReadyHeldBefore = readyState.held;
        const bool weaponDrawn = (player != nullptr && player->GetWeaponMagicDrawn());
        bool handledSuppressedReadyThisPass = false;
        bool touchedReadyState = false;

        QuickLootTakeLatchState takeLatch{};
        takeLatch.latched = ctrl._customQcTakeLatched.load(std::memory_order_acquire);
        takeLatch.boundDevice = ctrl._customQcTakeDevice.load(std::memory_order_relaxed);
        takeLatch.boundIdCode = ctrl._customQcTakeIdCode.load(std::memory_order_relaxed);
        takeLatch.hasBoundKey = (takeLatch.boundDevice != 0xFFFFFFFFu);
        takeLatch.lastTakePressMs = ctrl._customQcLastTakePressMs.load(std::memory_order_relaxed);
        takeLatch.lastReleaseMs = ctrl._customQcLastReleaseMs.load(std::memory_order_relaxed);
        takeLatch.latchSetMs = ctrl._customQcLatchSetMs.load(std::memory_order_relaxed);
        QuickLootTakePassState takePass{};
        bool touchedTakeLatch = false;

        const bool holdPrimaryReq = ctrl._holdPrimaryRequired.load(std::memory_order_acquire);
        const bool holdSecondaryReq = ctrl._holdSecondaryRequired.load(std::memory_order_acquire);
        const bool holdUseSyntheticTap = ctrl._holdPrimaryUseSyntheticTap.load(std::memory_order_acquire);
        const bool holdInQc = ctrl._holdInQuickContainer.load(std::memory_order_acquire);
        const std::uint64_t holdTgtKey = ctrl._holdTargetKey.load(std::memory_order_acquire);
        const std::int64_t holdDurMs = ctrl._holdDurationMs.load(std::memory_order_relaxed);
        const bool customTakeOwnsPrimary = customActive && customTakeEnabled;
        const bool tpAssistOn = ctrl._tpAssistActive.load(std::memory_order_acquire);
        bool tpLatched = ctrl._tpAssistActivateLatched.load(std::memory_order_acquire);
        bool touchedTpLatch = false;

        HoldToActivateState holdPrimaryState{};
        holdPrimaryState.active = ctrl._holdPrimaryActive.load(std::memory_order_acquire);
        holdPrimaryState.completedWaitingRelease = ctrl._holdPrimaryCompleted.load(std::memory_order_acquire);
        holdPrimaryState.targetKey = ctrl._holdPrimaryActiveKey.load(std::memory_order_relaxed);
        holdPrimaryState.startMs = ctrl._holdPrimaryStartMs.load(std::memory_order_relaxed);
        holdPrimaryState.lastSeenMs = ctrl._holdPrimaryLastSeenMs.load(std::memory_order_relaxed);
        holdPrimaryState.progress = ctrl._holdPrimaryProgress.load(std::memory_order_relaxed);
        bool touchedHoldPrimary = false;
        bool steppedPrimaryHoldThisPass = false;
        HoldToActivateDecision primaryPassDec{};
        bool steppedPrimaryTapThisPass = false;
        SyntheticHoldTapAction primaryTapPassAct = SyntheticHoldTapAction::kInactive;

        HoldToActivateState holdSecondaryState{};
        holdSecondaryState.active = ctrl._holdSecondaryActive.load(std::memory_order_acquire);
        holdSecondaryState.completedWaitingRelease = ctrl._holdSecondaryCompleted.load(std::memory_order_acquire);
        holdSecondaryState.targetKey = ctrl._holdSecondaryActiveKey.load(std::memory_order_relaxed);
        holdSecondaryState.startMs = ctrl._holdSecondaryStartMs.load(std::memory_order_relaxed);
        holdSecondaryState.lastSeenMs = ctrl._holdSecondaryLastSeenMs.load(std::memory_order_relaxed);
        holdSecondaryState.progress = ctrl._holdSecondaryProgress.load(std::memory_order_relaxed);
        bool touchedHoldSecondary = false;
        bool steppedSecondaryHoldThisPass = false;
        HoldToActivateDecision secondaryPassDec{};
        bool steppedSecondaryTapThisPass = false;
        SyntheticHoldTapAction secTapPassAct = SyntheticHoldTapAction::kInactive;

        for (auto* ev = const_cast<RE::InputEvent*>(a_queueHead); ev; ev = ev->next) {
            if (ev->handled.get() == RE::InputEvent::HANDLED_RESULT::kStop) {
                continue;
            }
            auto* btn = ev->As<RE::ButtonEvent>();
            if (!btn || btn->QDisabled()) {
                continue;
            }

            const auto& userEvent = btn->QUserEvent();

            // 1. Block companion R (SecondaryActivate / ReadyWeapon / QCOpenTransferMenu) and orphan SecondaryActivate
            if (userEvent == "SecondaryActivate"sv ||
                userEvent == "ReadyWeapon"sv ||
                userEvent == "QCOpenTransferMenu"sv) {
                SuppressedReadyInput readyIn{};
                readyIn.isSecondaryActivate = (userEvent == "SecondaryActivate"sv);
                readyIn.isReadyWeapon = (userEvent == "ReadyWeapon"sv);
                readyIn.isQcTransfer = (userEvent == "QCOpenTransferMenu"sv);
                readyIn.isJustPressed = btn->QJustPressed();
                readyIn.isPressed = (btn->QAnalogValue() != 0.0f);
                readyIn.isReleased = (btn->QAnalogValue() == 0.0f);
                readyIn.heldDownSecs = btn->QHeldDownSecs();
                readyIn.blockCompanionReady = blockCompReady;
                readyIn.blockOrphanSecondary = blockOrphanSecondary;
                readyIn.passHasBlockedSecondary = passHasBlockedSecondary;
                readyIn.canCustomReadyAction = canCustomReadyAction;
                readyIn.weaponDrawn = weaponDrawn;
                readyIn.alreadyHandledThisPass = handledSuppressedReadyThisPass;

                const auto dec = StepSuppressedSecondaryReady(readyState, readyIn, nowMs);
                if (dec.consumedPass) {
                    handledSuppressedReadyThisPass = true;
                    touchedReadyState = true;
                }
                if (!wasReadyHeldBefore && readyState.held && readyIn.isJustPressed) {
                    LogSuppressedReadyDiag(
                        a_sourceTag,
                        "blockedStart",
                        userEvent,
                        blockCompReady,
                        blockOrphanSecondary,
                        weaponDrawn);
                }
                if (dec.action != SuppressedReadyAction::kNone && pendingAction == SuppressedReadyAction::kNone) {
                    pendingAction = dec.action;
                }
                if (dec.swallowEvent) {
                    SwallowButtonEvent(ev, btn);
                    continue;
                }
            }

            // 2. Block suppressed companion SpecialActivate press/hold (preserving active physics grabs and releases)
            if (userEvent == "SpecialActivate"sv && blockCompActivate && !isAlreadyGrabbing && btn->QAnalogValue() != 0.0f) {
                SwallowButtonEvent(ev, btn);
                continue;
            }

            // 3. Custom QuickLoot single-item-per-physical-press latch & release-chatter debounce
            const auto dev = static_cast<std::uint32_t>(btn->device.get());
            const auto idCode = btn->QIDCode();
            const bool matchesLatchedPhysicalKey =
                takeLatch.hasBoundKey && dev == takeLatch.boundDevice && idCode == takeLatch.boundIdCode;
            const bool isTakeFamily =
                (userEvent == "QCTakeItem"sv || userEvent == "Activate"sv || userEvent == "SpecialActivate"sv);

            if (isTakeFamily || (matchesLatchedPhysicalKey && btn->QAnalogValue() == 0.0f)) {
                QuickLootTakeInput takeIn{};
                takeIn.device = dev;
                takeIn.idCode = idCode;
                takeIn.isTakeFamily = isTakeFamily;
                takeIn.isJustPressed = btn->QJustPressed();
                takeIn.isPressed = (btn->QAnalogValue() != 0.0f);
                takeIn.isReleased = (btn->QAnalogValue() == 0.0f);
                takeIn.canTake = customActive && customTakeEnabled && !isAlreadyGrabbing && (userEvent == "QCTakeItem"sv);
                takeIn.suppressTakeFamilyPress = customActive && customTakeEnabled && !isAlreadyGrabbing;

                const auto takeDec = StepQuickLootTakeLatch(takeLatch, takePass, takeIn, nowMs, takeDebounceMs);
                touchedTakeLatch = true;
                if (takeDec.acceptTake) {
                    ctrl._customQcTakeRequested.store(true, std::memory_order_release);
                }
                if (takeDec.swallowEvent && isTakeFamily) {
                    SwallowButtonEvent(ev, btn);
                    continue;
                }
            }

            if (userEvent == "QCTakeItem"sv && customActive && customTakeEnabled && !isAlreadyGrabbing) {
                SwallowButtonEvent(ev, btn);
                continue;
            }

            // 4. Prevent accidental corpse/object grab or post-empty activation right after fast looting
            const bool holdPrimaryEngaged =
                holdPrimaryReq ||
                holdPrimaryState.active ||
                holdPrimaryState.completedWaitingRelease ||
                (ctrl._holdWaterTapStage.load(std::memory_order_acquire) != 0) ||
                steppedPrimaryTapThisPass;
            if (!customActive && !holdPrimaryEngaged && (userEvent == "Activate"sv || userEvent == "SpecialActivate"sv)) {
                const bool isJust = btn->QJustPressed();
                const bool isHeld = (btn->QAnalogValue() != 0.0f);
                if (isHeld &&
                    ShouldBlockPostLootActivate(
                        nowMs,
                        ctrl._lastQuickLootTakeMs.load(std::memory_order_acquire),
                        ctrl._lastQuickLootEmptyMs.load(std::memory_order_acquire),
                        isJust,
                        isHeld,
                        isAlreadyGrabbing)) {
                    SwallowButtonEvent(ev, btn);
                    continue;
                }
            }

            // 4b. Hold-to-Activate protection for risky/unwanted primary actions (Rollover, World Water, Workbenches, Power Armor, Steal Containers)
            if (!customTakeOwnsPrimary && !isAlreadyGrabbing &&
                (userEvent == "Activate"sv || userEvent == "SpecialActivate"sv || (holdInQc && userEvent == "QCTakeItem"sv))) {
                const bool isJust = btn->QJustPressed();
                const bool isHeld = (btn->QAnalogValue() != 0.0f);
                const bool isRel = (btn->QAnalogValue() == 0.0f);

                int tapStage = ctrl._holdWaterTapStage.load(std::memory_order_acquire);
                const void* tapQH = ctrl._holdWaterQueueHead.load(std::memory_order_acquire);
                std::int64_t tapMs = ctrl._holdWaterTapTriggerMs.load(std::memory_order_relaxed);

                if (userEvent == "SpecialActivate"sv) {
                    if (tapStage != 0 || holdPrimaryReq || holdPrimaryState.active || holdPrimaryState.completedWaitingRelease) {
                        SwallowButtonEvent(ev, btn);
                        continue;
                    }
                } else {
                    if (tapStage != 0 || steppedPrimaryTapThisPass) {
                        if (!steppedPrimaryTapThisPass) {
                            primaryTapPassAct = StepSyntheticHoldTap(tapStage, tapQH, tapMs, a_queueHead, isRel, nowMs, 1500);
                            ctrl._holdWaterTapStage.store(tapStage, std::memory_order_release);
                            ctrl._holdWaterQueueHead.store(tapQH, std::memory_order_release);
                            ctrl._holdWaterTapTriggerMs.store(tapMs, std::memory_order_relaxed);
                            steppedPrimaryTapThisPass = true;
                        }
                        switch (primaryTapPassAct) {
                        case SyntheticHoldTapAction::kPassPress:
                            btn->heldDownSecs = 0.0f;
                            continue;
                        case SyntheticHoldTapAction::kPassRelease:
                            btn->value = 0.0f;
                            btn->heldDownSecs = 0.05f;
                            continue;
                        case SyntheticHoldTapAction::kPassReleaseAndResetHold:
                            holdPrimaryState = {};
                            touchedHoldPrimary = true;
                            continue;
                        case SyntheticHoldTapAction::kSwallowAndResetHold:
                            holdPrimaryState = {};
                            touchedHoldPrimary = true;
                            SwallowButtonEvent(ev, btn);
                            continue;
                        case SyntheticHoldTapAction::kSwallow:
                            SwallowButtonEvent(ev, btn);
                            continue;
                        case SyntheticHoldTapAction::kInactive:
                        default:
                            break;
                        }
                    }

                    if (holdPrimaryReq || holdPrimaryState.active || holdPrimaryState.completedWaitingRelease) {
                        if (!steppedPrimaryHoldThisPass) {
                            HoldToActivateInput hIn{};
                            hIn.requiresHold = holdPrimaryReq;
                            hIn.targetKey = holdTgtKey;
                            hIn.isJustPressed = isJust;
                            hIn.isPressed = isHeld;
                            hIn.isReleased = isRel;
                            primaryPassDec = StepHoldToActivate(holdPrimaryState, hIn, nowMs, holdDurMs);
                            steppedPrimaryHoldThisPass = true;
                            touchedHoldPrimary = true;
                        }
                        if (primaryPassDec.triggerNow) {
                            ctrl._postHoldGuardTriggerMs.store(nowMs, std::memory_order_relaxed);
                            ctrl._postHoldGuardMenuOpenMs.store(0, std::memory_order_relaxed);
                            ctrl._postHoldGuardReleaseMs.store(0, std::memory_order_relaxed);
                            ctrl._postHoldGuardDevice.store(dev, std::memory_order_relaxed);
                            ctrl._postHoldGuardIdCode.store(idCode, std::memory_order_relaxed);
                            ctrl._postHoldGuardMenuObserved.store(false, std::memory_order_release);
                            ctrl._postHoldGuardWaitRelease.store(true, std::memory_order_release);
                            ctrl._postHoldGuardArmed.store(true, std::memory_order_release);
                            if (holdUseSyntheticTap) {
                                if (!steppedPrimaryTapThisPass) {
                                    ctrl._holdWaterTapStage.store(1, std::memory_order_release);
                                    ctrl._holdWaterTapTriggerMs.store(nowMs, std::memory_order_relaxed);
                                    ctrl._holdWaterQueueHead.store(a_queueHead, std::memory_order_release);
                                    steppedPrimaryTapThisPass = true;
                                    primaryTapPassAct = SyntheticHoldTapAction::kPassPress;
                                }
                                btn->heldDownSecs = 0.0f;
                                continue;
                            }
                            ctrl._holdPrimaryActivateRequested.store(true, std::memory_order_release);
                            SwallowButtonEvent(ev, btn);
                            continue;
                        }
                        if (primaryPassDec.swallowEvent) {
                            SwallowButtonEvent(ev, btn);
                            continue;
                        }
                    }
                }
            }

            // 4c. Hold-to-Activate protection for SecondaryActivate / QCOpenTransferMenu (and block ReadyWeapon while active)
            if (!isAlreadyGrabbing &&
                (userEvent == "SecondaryActivate"sv || (holdInQc && userEvent == "QCOpenTransferMenu"sv) || userEvent == "ReadyWeapon"sv) &&
                (holdSecondaryReq || holdSecondaryState.active || holdSecondaryState.completedWaitingRelease ||
                 ctrl._holdSecTapStage.load(std::memory_order_acquire) != 0 || steppedSecondaryTapThisPass)) {
                if (userEvent == "ReadyWeapon"sv) {
                    const bool rwRel = (btn->QAnalogValue() == 0.0f);
                    const bool rwJust = btn->QJustPressed();
                    if (!holdSecondaryReq && (rwRel || rwJust)) {
                        ctrl._holdSecTapStage.store(0, std::memory_order_release);
                        ctrl._holdSecQueueHead.store(nullptr, std::memory_order_release);
                        ctrl._holdSecTapTriggerMs.store(0, std::memory_order_relaxed);
                        holdSecondaryState = {};
                        touchedHoldSecondary = true;
                        if (rwJust) {
                            continue;
                        }
                    }
                    SwallowButtonEvent(ev, btn);
                    continue;
                }
                const bool isPressed = (btn->QAnalogValue() != 0.0f);
                const bool isRel = !isPressed;

                int secTapStage = ctrl._holdSecTapStage.load(std::memory_order_acquire);
                const void* secTapQH = ctrl._holdSecQueueHead.load(std::memory_order_acquire);
                std::int64_t secTapMs = ctrl._holdSecTapTriggerMs.load(std::memory_order_relaxed);

                if (secTapStage != 0 || steppedSecondaryTapThisPass) {
                    if (!steppedSecondaryTapThisPass) {
                        secTapPassAct = StepSyntheticHoldTap(secTapStage, secTapQH, secTapMs, a_queueHead, isRel, nowMs, 1500);
                        ctrl._holdSecTapStage.store(secTapStage, std::memory_order_release);
                        ctrl._holdSecQueueHead.store(secTapQH, std::memory_order_release);
                        ctrl._holdSecTapTriggerMs.store(secTapMs, std::memory_order_relaxed);
                        steppedSecondaryTapThisPass = true;
                    }
                    switch (secTapPassAct) {
                    case SyntheticHoldTapAction::kPassPress:
                        btn->heldDownSecs = 0.0f;
                        continue;
                    case SyntheticHoldTapAction::kPassRelease:
                        btn->value = 0.0f;
                        btn->heldDownSecs = 0.05f;
                        continue;
                    case SyntheticHoldTapAction::kPassReleaseAndResetHold:
                        holdSecondaryState = {};
                        touchedHoldSecondary = true;
                        continue;
                    case SyntheticHoldTapAction::kSwallowAndResetHold:
                        holdSecondaryState = {};
                        touchedHoldSecondary = true;
                        SwallowButtonEvent(ev, btn);
                        continue;
                    case SyntheticHoldTapAction::kSwallow:
                        SwallowButtonEvent(ev, btn);
                        continue;
                    case SyntheticHoldTapAction::kInactive:
                    default:
                        break;
                    }
                }

                if (holdSecondaryReq || holdSecondaryState.active || holdSecondaryState.completedWaitingRelease) {
                    if (!steppedSecondaryHoldThisPass) {
                        HoldToActivateInput hIn{};
                        hIn.requiresHold = holdSecondaryReq;
                        hIn.targetKey = holdTgtKey;
                        hIn.isJustPressed = btn->QJustPressed();
                        hIn.isPressed = isPressed;
                        hIn.isReleased = isRel;
                        secondaryPassDec = StepHoldToActivate(holdSecondaryState, hIn, nowMs, holdDurMs);
                        steppedSecondaryHoldThisPass = true;
                        touchedHoldSecondary = true;
                    }
                    if (secondaryPassDec.triggerNow) {
                        ctrl._postHoldGuardTriggerMs.store(nowMs, std::memory_order_relaxed);
                        ctrl._postHoldGuardMenuOpenMs.store(0, std::memory_order_relaxed);
                        ctrl._postHoldGuardReleaseMs.store(0, std::memory_order_relaxed);
                        ctrl._postHoldGuardDevice.store(dev, std::memory_order_relaxed);
                        ctrl._postHoldGuardIdCode.store(idCode, std::memory_order_relaxed);
                        ctrl._postHoldGuardMenuObserved.store(false, std::memory_order_release);
                        ctrl._postHoldGuardWaitRelease.store(true, std::memory_order_release);
                        ctrl._postHoldGuardArmed.store(true, std::memory_order_release);
                        if (!steppedSecondaryTapThisPass) {
                            ctrl._holdSecTapStage.store(1, std::memory_order_release);
                            ctrl._holdSecTapTriggerMs.store(nowMs, std::memory_order_relaxed);
                            ctrl._holdSecQueueHead.store(a_queueHead, std::memory_order_release);
                            steppedSecondaryTapThisPass = true;
                            secTapPassAct = SyntheticHoldTapAction::kPassPress;
                        }
                        btn->heldDownSecs = 0.0f;
                        continue;
                    }
                    if (secondaryPassDec.swallowEvent) {
                        SwallowButtonEvent(ev, btn);
                        continue;
                    }
                }
            }

            // 4d. Third-person proximity assist activation (non-hold path)
            if (!customActive && !isAlreadyGrabbing &&
                (userEvent == "Activate"sv || userEvent == "SpecialActivate"sv) &&
                (tpAssistOn || tpLatched) && !holdPrimaryReq) {
                if (btn->QAnalogValue() == 0.0f) {
                    if (tpLatched) {
                        tpLatched = false;
                        touchedTpLatch = true;
                        SwallowButtonEvent(ev, btn);
                        continue;
                    }
                } else if (tpAssistOn) {
                    if (btn->QJustPressed() && !tpLatched) {
                        tpLatched = true;
                        touchedTpLatch = true;
                        ctrl._tpAssistActivateRequested.store(true, std::memory_order_release);
                    }
                    SwallowButtonEvent(ev, btn);
                    continue;
                }
            }

            if (!customActive) {
                continue;
            }

            // 5. Custom QuickLoot scroll wheel / D-pad handling
            if (userEvent == "QuickContainerUp"sv || userEvent == "QuickContainerDown"sv) {
                const std::int32_t dir = (userEvent == "QuickContainerUp"sv) ? -1 : 1;
                SwallowButtonEvent(ev, btn);
                if (btn->QJustPressed()) {
                    ctrl._customQcPendingScrollDelta.fetch_add(dir, std::memory_order_acq_rel);
                    ctrl._customQcLastRepeatMs.store(nowMs, std::memory_order_relaxed);
                } else if (btn->QAnalogValue() != 0.0f && btn->QHeldDownSecs() >= 0.35f) {
                    const std::int64_t lastRep = ctrl._customQcLastRepeatMs.load(std::memory_order_relaxed);
                    if ((nowMs - lastRep) >= 80) {
                        ctrl._customQcPendingScrollDelta.fetch_add(dir, std::memory_order_acq_rel);
                        ctrl._customQcLastRepeatMs.store(nowMs, std::memory_order_relaxed);
                    }
                }
            }
        }

        if ((startEpoch & 1u) == 0u &&
            ctrl._inputResetEpoch.load(std::memory_order_acquire) == startEpoch &&
            ctrl._postHoldGuardArmed.load(std::memory_order_acquire)) {
            PostHoldMenuGuardState guardState{};
            guardState.armed = true;
            guardState.menuObserved = ctrl._postHoldGuardMenuObserved.load(std::memory_order_acquire);
            guardState.waitRelease = ctrl._postHoldGuardWaitRelease.load(std::memory_order_acquire);
            guardState.triggerMs = ctrl._postHoldGuardTriggerMs.load(std::memory_order_relaxed);
            guardState.menuOpenMs = ctrl._postHoldGuardMenuOpenMs.load(std::memory_order_relaxed);
            guardState.releaseMs = ctrl._postHoldGuardReleaseMs.load(std::memory_order_relaxed);
            guardState.boundDevice = ctrl._postHoldGuardDevice.load(std::memory_order_relaxed);
            guardState.boundIdCode = ctrl._postHoldGuardIdCode.load(std::memory_order_relaxed);

            bool updated = false;
            if (primaryTapPassAct != SyntheticHoldTapAction::kPassRelease &&
                secTapPassAct != SyntheticHoldTapAction::kPassRelease) {
                for (const auto* ev = a_queueHead; ev; ev = ev->next) {
                    const auto* btn = ev->As<RE::ButtonEvent>();
                    if (!btn || btn->QDisabled() || btn->QAnalogValue() != 0.0f) {
                        continue;
                    }
                    const auto dev = static_cast<std::uint32_t>(btn->device.get());
                    const auto idCode = btn->QIDCode();
                    const auto& ue = btn->QUserEvent();
                    PostHoldMenuGuardInput gIn{};
                    gIn.inMenuMode = false;
                    gIn.isMatchingPhysicalKey =
                        (guardState.boundDevice != 0xFFFFFFFFu &&
                         dev == guardState.boundDevice &&
                         idCode == guardState.boundIdCode);
                    gIn.isConfirmFamilyEvent =
                        (ue == "Activate"sv || ue == "SpecialActivate"sv || ue == "QCTakeItem"sv ||
                         ue == "SecondaryActivate"sv || ue == "QCOpenTransferMenu"sv);
                    gIn.isReleased = true;
                    gIn.nowMs = nowMs;
                    (void)StepPostHoldMenuGuard(guardState, gIn);
                    updated = true;
                    if (!guardState.armed) {
                        break;
                    }
                }
            }
            if (!updated) {
                PostHoldMenuGuardInput pingIn{};
                pingIn.inMenuMode = false;
                pingIn.nowMs = nowMs;
                (void)StepPostHoldMenuGuard(guardState, pingIn);
            }
            ctrl._postHoldGuardWaitRelease.store(guardState.waitRelease, std::memory_order_release);
            ctrl._postHoldGuardReleaseMs.store(guardState.releaseMs, std::memory_order_relaxed);
            ctrl._postHoldGuardArmed.store(guardState.armed, std::memory_order_release);
        }

        if ((startEpoch & 1u) == 0u &&
            ctrl._inputResetEpoch.load(std::memory_order_acquire) == startEpoch) {
            if (touchedReadyState) {
                ctrl._suppressedReadyStartMs.store(readyState.startMs, std::memory_order_relaxed);
                ctrl._suppressedReadyHolstered.store(readyState.holstered, std::memory_order_release);
                ctrl._suppressedReadyHeld.store(readyState.held, std::memory_order_release);
            }
            if (touchedTakeLatch) {
                ctrl._customQcLastTakePressMs.store(takeLatch.lastTakePressMs, std::memory_order_relaxed);
                ctrl._customQcLastReleaseMs.store(takeLatch.lastReleaseMs, std::memory_order_relaxed);
                ctrl._customQcLatchSetMs.store(takeLatch.latchSetMs, std::memory_order_relaxed);
                ctrl._customQcTakeDevice.store(takeLatch.boundDevice, std::memory_order_relaxed);
                ctrl._customQcTakeIdCode.store(takeLatch.boundIdCode, std::memory_order_relaxed);
                ctrl._customQcTakeLatched.store(takeLatch.latched, std::memory_order_release);
            }
            if (touchedTpLatch) {
                ctrl._tpAssistActivateLatched.store(tpLatched, std::memory_order_release);
            }
            if (touchedHoldPrimary) {
                ctrl._holdPrimaryActiveKey.store(holdPrimaryState.targetKey, std::memory_order_relaxed);
                ctrl._holdPrimaryStartMs.store(holdPrimaryState.startMs, std::memory_order_relaxed);
                ctrl._holdPrimaryLastSeenMs.store(holdPrimaryState.lastSeenMs, std::memory_order_relaxed);
                ctrl._holdPrimaryProgress.store(holdPrimaryState.progress, std::memory_order_release);
                ctrl._holdPrimaryCompleted.store(holdPrimaryState.completedWaitingRelease, std::memory_order_release);
                ctrl._holdPrimaryActive.store(holdPrimaryState.active, std::memory_order_release);
            }
            if (touchedHoldSecondary) {
                ctrl._holdSecondaryActiveKey.store(holdSecondaryState.targetKey, std::memory_order_relaxed);
                ctrl._holdSecondaryStartMs.store(holdSecondaryState.startMs, std::memory_order_relaxed);
                ctrl._holdSecondaryLastSeenMs.store(holdSecondaryState.lastSeenMs, std::memory_order_relaxed);
                ctrl._holdSecondaryProgress.store(holdSecondaryState.progress, std::memory_order_release);
                ctrl._holdSecondaryCompleted.store(holdSecondaryState.completedWaitingRelease, std::memory_order_release);
                ctrl._holdSecondaryActive.store(holdSecondaryState.active, std::memory_order_release);
            }
        }

        return pendingAction;
    }

    void Controller::Hook_MenuControlsPerformInputProcessing(
        RE::BSInputEventReceiver* a_this,
        const RE::InputEvent* a_queueHead)
    {
        using Fn = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
        const SuppressedReadyAction pendingAction = ProcessInputEventQueue(a_queueHead, "MC");

        const auto fnAddr = _origMenuControlsPerformInputProcessing.load(std::memory_order_acquire);
        if (fnAddr) {
            reinterpret_cast<Fn>(fnAddr)(a_this, a_queueHead);
        }

        ExecuteReadyWeaponAction(pendingAction, "MC");
    }

    void Controller::Hook_PlayerControlsPerformInputProcessing(
        RE::BSInputEventReceiver* a_this,
        const RE::InputEvent* a_queueHead)
    {
        using Fn = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
        const SuppressedReadyAction pendingAction = ProcessInputEventQueue(a_queueHead, "PC");

        const auto fnAddr = _origPlayerControlsPerformInputProcessing.load(std::memory_order_acquire);
        if (fnAddr) {
            reinterpret_cast<Fn>(fnAddr)(a_this, a_queueHead);
        }

        ExecuteReadyWeaponAction(pendingAction, "PC");
    }

    void Controller::Hook_UIPerformInputProcessing(
        RE::BSInputEventReceiver* a_this,
        const RE::InputEvent* a_queueHead)
    {
        using Fn = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
        const SuppressedReadyAction pendingAction = ProcessInputEventQueue(a_queueHead, "UI");

        const auto fnAddr = _origUIPerformInputProcessing.load(std::memory_order_acquire);
        if (fnAddr) {
            reinterpret_cast<Fn>(fnAddr)(a_this, a_queueHead);
        }

        ExecuteReadyWeaponAction(pendingAction, "UI");
    }

    void Controller::Hook_HUDDataModelPerformInputProcessing(
        RE::BSInputEventReceiver* a_this,
        const RE::InputEvent* a_queueHead)
    {
        using Fn = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
        const SuppressedReadyAction pendingAction = ProcessInputEventQueue(a_queueHead, "HUD");

        const auto fnAddr = _origHUDDataModelPerformInputProcessing.load(std::memory_order_acquire);
        if (fnAddr) {
            reinterpret_cast<Fn>(fnAddr)(a_this, a_queueHead);
        }

        ExecuteReadyWeaponAction(pendingAction, "HUD");
    }

    void Controller::EnsureActivateHook()
    {
        if (_activateHooked.load(std::memory_order_acquire)) {
            return;
        }

        auto* pc = RE::PlayerControls::GetSingleton();
        if (!pc || !pc->activateHandler) {
            return;
        }

        if (!_activateHooked.exchange(true)) {
            auto* user = reinterpret_cast<RE::BSInputEventUser*>(pc->activateHandler);
            bool isHeldState = false;
            for (const auto* h : pc->heldStateHandlers) {
                if (reinterpret_cast<const void*>(h) == reinterpret_cast<const void*>(user)) {
                    isHeldState = true;
                    break;
                }
            }
            _activateIsHeldStateHandler.store(isHeldState, std::memory_order_release);

            const auto vtableAddr = *reinterpret_cast<const std::uintptr_t*>(pc->activateHandler);
            const auto origSlot = reinterpret_cast<const std::uintptr_t*>(vtableAddr)[1];
            _origActivateShouldHandleEvent.store(origSlot, std::memory_order_release);
            (void)REL::Relocation<std::uintptr_t>{ vtableAddr }.write_vfunc(1, &Hook_ActivateShouldHandleEvent);
            logger::info(
                "Hooked ActivateHandler::ShouldHandleEvent at vtable {:X} (heldState={})",
                vtableAddr,
                isHeldState);
        }
    }

    bool Controller::ShouldBlockCompanionActivation() const noexcept
    {
        return _suppressCompanionActivate.load(std::memory_order_acquire);
    }

    bool Controller::Hook_ReadyWeaponShouldHandleEvent(RE::BSInputEventUser* a_this, const RE::InputEvent* a_event)
    {
        if (IsEventStoppedOrDisabled(a_event)) {
            return false;
        }

        using Fn = bool (*)(RE::BSInputEventUser*, const RE::InputEvent*);
        const auto fnAddr = _origReadyWeaponShouldHandleEvent.load(std::memory_order_acquire);
        const bool orig = fnAddr ? reinterpret_cast<Fn>(fnAddr)(a_this, a_event) : false;
        if (!orig) {
            return false;
        }

        auto& ctrl = GetSingleton();
        if (a_event) {
            if (const auto* btn = a_event->As<RE::ButtonEvent>()) {
                const auto& evName = btn->QUserEvent();
                const bool blockCompReady = ctrl._suppressCompanionReady.load(std::memory_order_acquire);
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* ui = RE::UI::GetSingleton();
                const bool inCommandModeNow = (player && static_cast<bool>(player->actorDoingPlayerCommand));
                const bool inWorkshopNow = (ui && ui->GetMenuOpen("WorkshopMenu"sv));
                const bool blockOrphanSec =
                    ctrl._blockOrphanSecondaryActivate.load(std::memory_order_acquire) &&
                    !inCommandModeNow &&
                    !inWorkshopNow;

                if (evName == "SecondaryActivate"sv && (blockCompReady || blockOrphanSec)) {
                    LogSuppressedReadyDiag(
                        "RW",
                        "shouldHandleBlocked",
                        evName,
                        blockCompReady,
                        blockOrphanSec,
                        player && player->GetWeaponMagicDrawn());
                    return false;
                }

                if (evName == "ReadyWeapon"sv &&
                    (ctrl._holdSecondaryRequired.load(std::memory_order_acquire) ||
                     ctrl._holdSecondaryActive.load(std::memory_order_acquire) ||
                     ctrl._holdSecondaryCompleted.load(std::memory_order_acquire) ||
                     ctrl._holdSecTapStage.load(std::memory_order_acquire) != 0)) {
                    return false;
                }

                if (evName == "ReadyWeapon"sv && blockCompReady) {
                    const bool heldActive =
                        ctrl._readyWeaponIsHeldStateHandler.load(std::memory_order_acquire) &&
                        a_this &&
                        static_cast<const RE::HeldStateHandler*>(a_this)->heldStateActive;
                    if (!heldActive) {
                        LogSuppressedReadyDiag(
                            "RW",
                            "shouldHandleBlocked",
                            evName,
                            blockCompReady,
                            blockOrphanSec,
                            player && player->GetWeaponMagicDrawn());
                        return false;
                    }
                }
            }
        }

        return true;
    }

    bool Controller::Hook_ActivateShouldHandleEvent(RE::BSInputEventUser* a_this, const RE::InputEvent* a_event)
    {
        if (IsEventStoppedOrDisabled(a_event)) {
            return false;
        }

        using Fn = bool (*)(RE::BSInputEventUser*, const RE::InputEvent*);
        const auto fnAddr = _origActivateShouldHandleEvent.load(std::memory_order_acquire);
        const bool orig = fnAddr ? reinterpret_cast<Fn>(fnAddr)(a_this, a_event) : false;
        if (!orig) {
            return false;
        }

        auto& ctrl = GetSingleton();
        if (a_event) {
            if (const auto* btn = a_event->As<RE::ButtonEvent>()) {
                if (btn->QUserEvent() == "QCTakeItem"sv &&
                    ctrl._customQcActive.load(std::memory_order_acquire) &&
                    ctrl._customQcTakeEnabled.load(std::memory_order_acquire)) {
                    return false;
                }
            }
        }

        if (ctrl._activateIsHeldStateHandler.load(std::memory_order_acquire) && a_this) {
            const auto* held = static_cast<const RE::HeldStateHandler*>(a_this);
            if (held->heldStateActive) {
                return true;
            }
        }

        if (a_event) {
            if (const auto* btn = a_event->As<RE::ButtonEvent>()) {
                if (btn->QUserEvent() == "SecondaryActivate"sv) {
                    const bool blockCompReady = ctrl._suppressCompanionReady.load(std::memory_order_acquire);
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    auto* ui = RE::UI::GetSingleton();
                    const bool inCommandModeNow = (player && static_cast<bool>(player->actorDoingPlayerCommand));
                    const bool inWorkshopNow = (ui && ui->GetMenuOpen("WorkshopMenu"sv));
                    const bool blockOrphanSec =
                        ctrl._blockOrphanSecondaryActivate.load(std::memory_order_acquire) &&
                        !inCommandModeNow &&
                        !inWorkshopNow;
                    if (blockCompReady || blockOrphanSec) {
                        LogSuppressedReadyDiag(
                            "ACT",
                            "shouldHandleBlocked",
                            btn->QUserEvent(),
                            blockCompReady,
                            blockOrphanSec,
                            player && player->GetWeaponMagicDrawn());
                        return false;
                    }
                }
            }
        }

        if (ctrl.ShouldBlockCompanionActivation()) {
            return false;
        }

        if (a_event) {
            if (const auto* btn = a_event->As<RE::ButtonEvent>()) {
                const auto& evName = btn->QUserEvent();
                const bool customTakeOwnsPrimary =
                    ctrl._customQcActive.load(std::memory_order_acquire) &&
                    ctrl._customQcTakeEnabled.load(std::memory_order_acquire);
                const bool holdInQc = ctrl._holdInQuickContainer.load(std::memory_order_acquire);
                if (evName == "SecondaryActivate"sv || (holdInQc && evName == "QCOpenTransferMenu"sv)) {
                    if (ctrl._holdSecTapStage.load(std::memory_order_acquire) == 0 &&
                        (ctrl._holdSecondaryRequired.load(std::memory_order_acquire) ||
                         ctrl._holdSecondaryActive.load(std::memory_order_acquire) ||
                         ctrl._holdSecondaryCompleted.load(std::memory_order_acquire))) {
                        return false;
                    }
                }
                if (evName == "Activate"sv || evName == "SpecialActivate"sv ||
                    (!customTakeOwnsPrimary && holdInQc && evName == "QCTakeItem"sv)) {
                    if (evName != "QCTakeItem"sv && ctrl._customQcTakeLatched.load(std::memory_order_acquire)) {
                        return false;
                    }
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    const bool isAlreadyGrabbing = (player && static_cast<bool>(player->grabbedObject));
                    if (ctrl._holdWaterTapStage.load(std::memory_order_acquire) == 0 &&
                        !isAlreadyGrabbing &&
                        (ctrl._tpAssistActive.load(std::memory_order_acquire) ||
                         ctrl._tpAssistActivateLatched.load(std::memory_order_acquire) ||
                         ctrl._holdPrimaryRequired.load(std::memory_order_acquire) ||
                         ctrl._holdPrimaryActive.load(std::memory_order_acquire) ||
                         ctrl._holdPrimaryCompleted.load(std::memory_order_acquire))) {
                        return false;
                    }
                    if (evName != "QCTakeItem"sv &&
                        ShouldBlockPostLootActivate(
                            SteadyNowMs(),
                            ctrl._lastQuickLootTakeMs.load(std::memory_order_acquire),
                            ctrl._lastQuickLootEmptyMs.load(std::memory_order_acquire),
                            btn->QJustPressed(),
                            btn->QAnalogValue() != 0.0f,
                            isAlreadyGrabbing)) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    void Controller::ResolveCompanionEngineSettings()
    {
        if (_companionSettingsResolved) {
            return;
        }

        if (auto* gsc = RE::GameSettingCollection::GetSingleton()) {
            for (const auto& entry : gsc->settings) {
                if (entry.second &&
                    entry.second->GetType() == RE::Setting::SETTING_TYPE::kFloat &&
                    entry.second->GetKey() == "fCommandRequestPickDistance"sv) {
                    _gmstCommandPickDist = entry.second;
                    _origCommandPickDist = entry.second->GetFloat();
                    _hasOrigCommandPickDist = std::isfinite(_origCommandPickDist);
                    break;
                }
            }
        }

        if (auto* ini = RE::INISettingCollection::GetSingleton()) {
            if (auto* cmdRad = ini->GetSetting("fCommandActivatePickRadius:Interface"sv);
                cmdRad && cmdRad->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
                _iniCommandPickRadius = cmdRad;
                _origCommandPickRadius = cmdRad->GetFloat();
                _hasOrigCommandPickRadius = std::isfinite(_origCommandPickRadius);
            }
            if (auto* actRad = ini->GetSetting("fActivatePickRadius:Interface"sv);
                actRad && actRad->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
                _iniActivatePickRadius = actRad;
                _origActivatePickRadius = actRad->GetFloat();
                _hasOrigActivatePickRadius = std::isfinite(_origActivatePickRadius);
            }
        }

        if (_gmstCommandPickDist || _iniCommandPickRadius) {
            _companionSettingsResolved = true;
            logger::info(
                "Resolved companion pick settings: fCommandRequestPickDistance={} ({:.1f}), fCommandActivatePickRadius={} ({:.1f}), fActivatePickRadius={} ({:.1f})",
                _gmstCommandPickDist != nullptr,
                _origCommandPickDist,
                _iniCommandPickRadius != nullptr,
                _origCommandPickRadius,
                _iniActivatePickRadius != nullptr,
                _origActivatePickRadius);
        }
    }

    void Controller::ApplyCompanionEngineSettings(float a_dist, float a_radius)
    {
        if (_gmstCommandPickDist && _hasOrigCommandPickDist && std::isfinite(a_dist)) {
            if (std::fabs(_gmstCommandPickDist->GetFloat() - a_dist) > 0.01f) {
                _gmstCommandPickDist->SetFloat(a_dist);
            }
        }
        if (_iniCommandPickRadius && _hasOrigCommandPickRadius && std::isfinite(a_radius)) {
            if (std::fabs(_iniCommandPickRadius->GetFloat() - a_radius) > 0.01f) {
                _iniCommandPickRadius->SetFloat(a_radius);
            }
        }
    }

    void Controller::RestoreCompanionEngineSettings()
    {
        if (_gmstCommandPickDist && _hasOrigCommandPickDist) {
            if (std::fabs(_gmstCommandPickDist->GetFloat() - _origCommandPickDist) > 0.01f) {
                _gmstCommandPickDist->SetFloat(_origCommandPickDist);
            }
        }
        if (_iniCommandPickRadius && _hasOrigCommandPickRadius) {
            if (std::fabs(_iniCommandPickRadius->GetFloat() - _origCommandPickRadius) > 0.01f) {
                _iniCommandPickRadius->SetFloat(_origCommandPickRadius);
            }
        }
    }

    void Controller::ResolveGrabDelaySetting()
    {
        if (_grabSettingResolved) {
            return;
        }

        if (auto* ini = RE::INISettingCollection::GetSingleton()) {
            if (auto* zDelay = ini->GetSetting("fZKeyDelay:Controls"sv);
                zDelay && zDelay->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
                _iniGrabHoldDelay = zDelay;
                _origGrabHoldDelay = zDelay->GetFloat();
                _hasOrigGrabHoldDelay = std::isfinite(_origGrabHoldDelay);
            }
        }

        if (!_iniGrabHoldDelay) {
            if (auto* gsc = RE::GameSettingCollection::GetSingleton()) {
                for (const auto& entry : gsc->settings) {
                    if (entry.second &&
                        entry.second->GetType() == RE::Setting::SETTING_TYPE::kFloat &&
                        entry.second->GetKey() == "fZKeyDelay"sv) {
                        _iniGrabHoldDelay = entry.second;
                        _origGrabHoldDelay = entry.second->GetFloat();
                        _hasOrigGrabHoldDelay = std::isfinite(_origGrabHoldDelay);
                        break;
                    }
                }
            }
        }

        _grabSettingResolved = true;
        logger::info(
            "Resolved grab hold delay setting: found={} orig={:.2f}",
            _iniGrabHoldDelay != nullptr,
            _origGrabHoldDelay);
    }

    void Controller::ApplyGrabDelaySetting(float a_delaySec)
    {
        if (_iniGrabHoldDelay && _hasOrigGrabHoldDelay && std::isfinite(a_delaySec)) {
            const float clamped = std::clamp(a_delaySec, 0.30f, 2.00f);
            if (std::fabs(_iniGrabHoldDelay->GetFloat() - clamped) > 0.005f) {
                _iniGrabHoldDelay->SetFloat(clamped);
            }
        }
    }

    void Controller::RestoreGrabDelaySetting()
    {
        if (_iniGrabHoldDelay && _hasOrigGrabHoldDelay) {
            if (std::fabs(_iniGrabHoldDelay->GetFloat() - _origGrabHoldDelay) > 0.005f) {
                _iniGrabHoldDelay->SetFloat(_origGrabHoldDelay);
            }
        }
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
        _cachedInvSnapshot.conditionByCleanUpper.clear();
        _cachedMerge = {};
        _customQcActive.store(false, std::memory_order_release);
        _customQcTakeEnabled.store(false, std::memory_order_release);
        _customQcPendingScrollDelta.store(0, std::memory_order_release);
        _customQcTakeRequested.store(false, std::memory_order_release);
        _customQcOwnerFormID = 0;
        _customQcSelectedIndex = 0;
        _customQcSortedItems.clear();
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
            _inputResetEpoch.fetch_add(1, std::memory_order_acq_rel);
            _customQcLastTakePressMs.store(0, std::memory_order_relaxed);
            _customQcLastReleaseMs.store(0, std::memory_order_relaxed);
            _customQcLatchSetMs.store(0, std::memory_order_relaxed);
            _customQcTakeDevice.store(0xFFFFFFFFu, std::memory_order_relaxed);
            _customQcTakeIdCode.store(0xFFFFFFFFu, std::memory_order_relaxed);
            _suppressedReadyStartMs.store(0, std::memory_order_relaxed);
            _suppressedReadyHolstered.store(false, std::memory_order_release);
            _suppressedReadyHeld.store(false, std::memory_order_release);
            _customQcTakeLatched.store(false, std::memory_order_release);
            _blockOrphanSecondaryActivate.store(false, std::memory_order_release);
            _tpAssistActive.store(false, std::memory_order_release);
            _tpAssistActivateRequested.store(false, std::memory_order_release);
            _tpAssistActivateLatched.store(false, std::memory_order_release);
            _holdPrimaryRequired.store(false, std::memory_order_release);
            _holdSecondaryRequired.store(false, std::memory_order_release);
            _holdPrimaryUseSyntheticTap.store(false, std::memory_order_release);
            _holdInQuickContainer.store(false, std::memory_order_release);
            _holdTargetKey.store(0, std::memory_order_release);
            _holdPrimaryProgress.store(0.0f, std::memory_order_release);
            _holdSecondaryProgress.store(0.0f, std::memory_order_release);
            _holdPrimaryActivateRequested.store(false, std::memory_order_release);
            _holdPrimaryActive.store(false, std::memory_order_release);
            _holdPrimaryCompleted.store(false, std::memory_order_release);
            _holdSecondaryActive.store(false, std::memory_order_release);
            _holdSecondaryCompleted.store(false, std::memory_order_release);
            _holdWaterTapStage.store(0, std::memory_order_release);
            _holdWaterTapTriggerMs.store(0, std::memory_order_relaxed);
            _holdWaterQueueHead.store(nullptr, std::memory_order_release);
            _holdSecTapStage.store(0, std::memory_order_release);
            _holdSecTapTriggerMs.store(0, std::memory_order_relaxed);
            _holdSecQueueHead.store(nullptr, std::memory_order_release);
            _tpAssistLockedFormID = 0;
            _tpAssistLockStartMs = 0;
            _postHoldGuardArmed.store(false, std::memory_order_release);
            _postHoldGuardMenuObserved.store(false, std::memory_order_release);
            _postHoldGuardWaitRelease.store(false, std::memory_order_release);
            _postHoldGuardTriggerMs.store(0, std::memory_order_relaxed);
            _postHoldGuardMenuOpenMs.store(0, std::memory_order_relaxed);
            _postHoldGuardReleaseMs.store(0, std::memory_order_relaxed);
            _postHoldGuardDevice.store(0xFFFFFFFFu, std::memory_order_relaxed);
            _postHoldGuardIdCode.store(0xFFFFFFFFu, std::memory_order_relaxed);
            _featuredExamineOpenMs.store(0, std::memory_order_release);
            _featuredExamineWaitRelease.store(false, std::memory_order_release);
            _inputResetEpoch.fetch_add(1, std::memory_order_acq_rel);
            _state.Reset();
            _qcState.Reset();
            _cachedTargetFormID = 0;
            _cachedTargetTimeMs = 0;
            _cachedMetadata = {};
            _hasCachedAnchor = false;
            _cachedAnchorFormID = 0;
            _cachedAnchorRoot3D = nullptr;
            _cachedAnchorRefScale = 0;
            _lastQuickLootActiveMs = 0;
            _tpAssistRefHandle.reset();
            _tpAssistLastScanMs = 0;
            _lastAimingMs.store(0, std::memory_order_release);
            RestoreCompanionEngineSettings();
            RestoreGrabDelaySetting();
            InvalidateQuickContainerCacheInternal();
            return;
        }
        if (_pendingQcInvalidate.exchange(false, std::memory_order_acq_rel)) {
            InvalidateQuickContainerCacheInternal();
        }
    }

    void Controller::ClearOverlaySnapshot()
    {
        _suppressCompanionActivate.store(false, std::memory_order_release);
        _suppressCompanionReady.store(false, std::memory_order_release);
        _blockOrphanSecondaryActivate.store(false, std::memory_order_release);
        _suppressedReadyStartMs.store(0, std::memory_order_relaxed);
        _suppressedReadyHolstered.store(false, std::memory_order_release);
        _suppressedReadyHeld.store(false, std::memory_order_release);
        _customQcActive.store(false, std::memory_order_release);
        _customQcTakeEnabled.store(false, std::memory_order_release);
        _customQcPendingScrollDelta.store(0, std::memory_order_release);
        _customQcTakeRequested.store(false, std::memory_order_release);
        _tpAssistActive.store(false, std::memory_order_release);
        _tpAssistActivateRequested.store(false, std::memory_order_release);
        _holdPrimaryRequired.store(false, std::memory_order_release);
        _holdSecondaryRequired.store(false, std::memory_order_release);
        _holdPrimaryUseSyntheticTap.store(false, std::memory_order_release);
        _holdInQuickContainer.store(false, std::memory_order_release);
        _holdTargetKey.store(0, std::memory_order_release);
        _lastAimingMs.store(0, std::memory_order_release);
        _holdPrimaryActivateRequested.store(false, std::memory_order_release);
        _holdPrimaryProgress.store(0.0f, std::memory_order_release);
        _holdSecondaryProgress.store(0.0f, std::memory_order_release);
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

    float Controller::GetLiveHoldProgress(std::size_t a_actionIdx) const noexcept
    {
        if (a_actionIdx == 0) {
            return std::clamp(_holdPrimaryProgress.load(std::memory_order_acquire), 0.0f, 1.0f);
        }
        if (a_actionIdx == 1) {
            return std::clamp(_holdSecondaryProgress.load(std::memory_order_acquire), 0.0f, 1.0f);
        }
        return 0.0f;
    }

    void Controller::ResetState()
    {
        _pendingReset.store(true, std::memory_order_release);
        _inputResetEpoch.fetch_add(1, std::memory_order_acq_rel);
        _customQcLastTakePressMs.store(0, std::memory_order_relaxed);
        _customQcLastReleaseMs.store(0, std::memory_order_relaxed);
        _customQcLatchSetMs.store(0, std::memory_order_relaxed);
        _customQcTakeDevice.store(0xFFFFFFFFu, std::memory_order_relaxed);
        _customQcTakeIdCode.store(0xFFFFFFFFu, std::memory_order_relaxed);
        _customQcTakeLatched.store(false, std::memory_order_release);
        _tpAssistActivateLatched.store(false, std::memory_order_release);
        _holdPrimaryActive.store(false, std::memory_order_release);
        _holdPrimaryCompleted.store(false, std::memory_order_release);
        _holdSecondaryActive.store(false, std::memory_order_release);
        _holdSecondaryCompleted.store(false, std::memory_order_release);
        _holdWaterTapStage.store(0, std::memory_order_release);
        _holdWaterTapTriggerMs.store(0, std::memory_order_relaxed);
        _holdWaterQueueHead.store(nullptr, std::memory_order_release);
        _holdSecTapStage.store(0, std::memory_order_release);
        _holdSecTapTriggerMs.store(0, std::memory_order_relaxed);
        _holdSecQueueHead.store(nullptr, std::memory_order_release);
        _postHoldGuardArmed.store(false, std::memory_order_release);
        _featuredExamineOpenMs.store(0, std::memory_order_release);
        _featuredExamineWaitRelease.store(false, std::memory_order_release);
        ClearOverlaySnapshot();
        _inputResetEpoch.fetch_add(1, std::memory_order_acq_rel);
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
            a_item.stackSignature == _cachedQcItemStackSig &&
            a_item.hasCondition == _cachedQcItemHasCondition &&
            (!a_item.hasCondition || std::fabs(a_item.conditionPercent - _cachedQcItemConditionPercent) < 0.25f) &&
            a_item.name == _cachedQcItemName &&
            (nowMs - _cachedQcItemTimeMs) < 200) {
            a_outMeta = _cachedQcItemMeta;
            return true;
        }

        const int earlyExitScore =
            ((a_item.baseFormID != 0) ? 100 : 0) +
            ((a_item.stackSignature != 0) ? 390 : 350);
        const std::string targetCleanUpper = CleanUpperKey(a_item.name);
        RE::TESBoundObject* bestObj = nullptr;
        RE::BSTSmartPointer<RE::ExtraDataList> bestExtra;
        std::string bestRawName;

        {
            RE::BSAutoWriteLock lock{ a_invOwner->inventoryList->rwLock };
            int bestScore = 0;

            for (const auto& invItem : a_invOwner->inventoryList->data) {
                if (!invItem.object || invItem.object->IsDeleted()) {
                    continue;
                }
                const std::string_view baseSv = RE::TESFullName::GetFullName(*invItem.object);
                const std::string baseName = baseSv.empty() ? std::string{} : std::string(baseSv);
                const bool hasBaseName = !TrimWhitespace(baseName).empty();
                const auto objFormType = invItem.object->GetFormType();
                const bool allowInstancedFullName =
                    (objFormType == RE::ENUM_FORM_ID::kWEAP || objFormType == RE::ENUM_FORM_ID::kARMO);

                std::uint32_t stackIdx = 0;
                for (auto* stack = invItem.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIdx) {
                    if (stack->GetCount() == 0) {
                        continue;
                    }
                    RE::ExtraDataList* stackExtra = stack->extra.get();
                    const RE::TBO_InstanceData* instData = ResolveItemInstanceData(invItem.object, stackExtra);
                    if (!invItem.object->GetPlayable(instData)) {
                        continue;
                    }
                    std::string rawName;
                    if (!ResolveInventoryStackDisplayName(
                            const_cast<RE::BGSInventoryItem&>(invItem),
                            stackIdx,
                            stackExtra,
                            baseName,
                            hasBaseName,
                            allowInstancedFullName,
                            rawName)) {
                        continue;
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
                        if (a_item.baseFormID != 0 && invItem.object->GetFormID() == a_item.baseFormID) {
                            score += 100;
                        }
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
            _cachedQcItemStackSig = a_item.stackSignature;
            _cachedQcItemHasCondition = a_item.hasCondition;
            _cachedQcItemConditionPercent = a_item.conditionPercent;
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

    const CachedInventorySnapshot& Controller::CollectQuickContainerInventory(
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
            auto* favMgr = RE::FavoritesManager::GetSingleton();
            RE::BSAutoWriteLock lock{ a_invOwner->inventoryList->rwLock };
            const std::size_t liveListSize = a_invOwner->inventoryList->data.size();
            std::size_t liveStackCount = 0;
            std::size_t liveEquippedCount = 0;
            std::uint64_t liveCountSum = 0;
            std::size_t probedItems = 0;
            for (const auto& invItem : a_invOwner->inventoryList->data) {
                if (++probedItems > 2048u) {
                    break;
                }
                for (auto* st = invItem.stackData.get(); st; st = st->nextStack.get()) {
                    ++liveStackCount;
                    if (st->IsEquipped()) {
                        ++liveEquippedCount;
                    }
                    liveCountSum += st->GetCount();
                }
            }

            if (ownerFormID != 0 &&
                ownerFormID == _cachedInvSnapshot.ownerFormID &&
                a_skipEquipped == _cachedInvSnapshot.skipEquipped &&
                liveListSize == _cachedInvSnapshot.rawListSize &&
                liveStackCount == _cachedInvSnapshot.rawStackCount &&
                liveEquippedCount == _cachedInvSnapshot.rawEquippedStackCount &&
                liveCountSum == _cachedInvSnapshot.rawTotalCountSum &&
                (nowMs - _cachedInvSnapshot.timestampMs) < 250) {
                return _cachedInvSnapshot;
            }

            nextSnap.rawListSize = liveListSize;
            nextSnap.rawStackCount = liveStackCount;
            nextSnap.rawEquippedStackCount = liveEquippedCount;
            nextSnap.rawTotalCountSum = liveCountSum;
            nextSnap.items.reserve(std::min(liveListSize, kMaxExpandedInventoryItems));

            for (const auto& invItem : a_invOwner->inventoryList->data) {
                if (!invItem.object || invItem.object->IsDeleted()) {
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
                const bool hasBaseName = !TrimWhitespace(baseName).empty();
                const std::string cleanBaseKey = hasBaseName ? CleanUpperKey(baseName) : std::string{};

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

                const std::uint32_t baseFormID = invItem.object->GetFormID();
                const bool taggedForSearch = IsObjectTaggedForSearch(invItem.object, favMgr);
                const bool isCaps = (baseFormID == 0x0000000Fu);

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
                    if (stack->flags.all(RE::BGSInventoryItem::Stack::Flag::kEquipStateLocked)) {
                        continue;
                    }

                    RE::ExtraDataList* stackExtra = stack->extra.get();
                    const RE::TBO_InstanceData* instData = ResolveItemInstanceData(invItem.object, stackExtra);
                    if (!invItem.object->GetPlayable(instData)) {
                        continue;
                    }

                    const QuickLootCategory qcCategory = ClassifyQuickLootCategory(invItem.object, instData);

                    const bool favorite = (stackExtra && stackExtra->HasType(RE::EXTRA_DATA_TYPE::kFavorite));
                    const bool allowInstancedFullName =
                        (kind == TargetInfo::ItemKind::Weapon || kind == TargetInfo::ItemKind::Armor);
                    std::string displayName;
                    if (!ResolveInventoryStackDisplayName(
                            const_cast<RE::BGSInventoryItem&>(invItem),
                            stackIdx,
                            stackExtra,
                            baseName,
                            hasBaseName,
                            allowInstancedFullName,
                            displayName)) {
                        continue;
                    }

                    const std::string cleanKey = CleanUpperKey(displayName);
                    if (!cleanKey.empty() && nextSnap.kindByCleanUpper.size() < 2048) {
                        nextSnap.kindByCleanUpper.try_emplace(cleanKey, kind);
                    }

                    bool legendary = false;
                    const std::uint64_t stackSig = ComputeStackSignature(stackExtra, stackIdx, legendary);
                    const bool questItem = IsStackQuestObject(stackExtra);
                    const bool rawTempFlag = stack->flags.all(RE::BGSInventoryItem::Stack::Flag::kTemporary);
                    bool isDroppedWorldProxy = false;
                    if (rawTempFlag && stackExtra) {
                        if (auto* refHandles = stackExtra->GetByType<RE::ExtraReferenceHandles>()) {
                            auto origPtr = refHandles->originalRef ? refHandles->originalRef.get() : RE::NiPointer<RE::TESObjectREFR>{};
                            if (!origPtr || origPtr->IsDeleted() || IsRefDisabled(origPtr.get())) {
                                continue; // Skip orphaned/deleted world proxy stack that cannot be picked up
                            }
                            isDroppedWorldProxy = true;
                        }
                    }

                    const auto condRes = TargetInfo::ResolveObjectCondition(invItem.object, stackExtra, kind, instData);
                    if (condRes.hasCondition && !cleanKey.empty() && nextSnap.conditionByCleanUpper.size() < 2048) {
                        nextSnap.conditionByCleanUpper.try_emplace(cleanKey, condRes.conditionPercent);
                    }

                    const std::int32_t rawValue = RE::TESValueForm::GetFormValue(invItem.object, instData);
                    const std::uint32_t itemValue = (rawValue > 0) ? static_cast<std::uint32_t>(rawValue) : 0u;
                    const float rawWeight = RE::TESWeightForm::GetFormWeight(invItem.object, instData);
                    const float itemWeight = (std::isfinite(rawWeight) && rawWeight > 0.0f) ? rawWeight : 0.0f;

                    QuickContainerItemEntry entry;
                    entry.name = std::move(displayName);
                    entry.count = cnt;
                    entry.isLegendary = legendary;
                    entry.isFavorite = favorite;
                    entry.isTaggedForSearch = taggedForSearch;
                    entry.isEquipped = equipped;
                    entry.itemKind = kind;
                    entry.stackSignature = stackSig;
                    entry.value = itemValue;
                    entry.weight = itemWeight;
                    entry.baseFormID = baseFormID;
                    entry.quickLootCategory = qcCategory;
                    entry.isQuestItem = questItem;
                    entry.isCaps = isCaps;
                    entry.isTemporary = isDroppedWorldProxy;
                    entry.hasCondition = condRes.hasCondition;
                    entry.conditionPercent = condRes.conditionPercent;
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

    std::uint64_t Controller::CountBoundObjectInInventory(
        RE::TESObjectREFR* a_invOwner,
        std::uint32_t a_formID) const
    {
        if (!a_invOwner || !a_invOwner->inventoryList || a_formID == 0) {
            return 0;
        }
        std::uint64_t total = 0;
        RE::BSAutoWriteLock lock{ a_invOwner->inventoryList->rwLock };
        for (const auto& invItem : a_invOwner->inventoryList->data) {
            if (!invItem.object || invItem.object->IsDeleted() ||
                invItem.object->GetFormID() != a_formID) {
                continue;
            }
            for (auto* stack = invItem.stackData.get(); stack; stack = stack->nextStack.get()) {
                total += stack->GetCount();
            }
        }
        return total;
    }

    bool Controller::ExecuteCustomQuickLootTake(
        RE::TESObjectREFR* a_invOwner,
        RE::PlayerCharacter* a_player,
        RE::QuickContainerMode a_mode,
        const QuickContainerItemEntry& a_target)
    {
        if (!a_invOwner || !a_invOwner->inventoryList || !a_player || a_target.name.empty()) {
            return false;
        }

        const std::string targetCleanUpper = CleanUpperKey(a_target.name);
        RE::TESBoundObject* boundObj = nullptr;
        std::uint32_t boundFormID = 0;
        RE::NiPointer<RE::TESObjectREFR> droppedRef;
        RE::ObjectRefHandle droppedOrigHandle{};
        std::vector<std::uint32_t> stackIndices;
        std::vector<std::pair<std::uint32_t, const RE::TBO_InstanceData*>> equippedStacks;
        std::uint64_t totalCount = 0;

        auto collectTargetStacks = [&](bool a_allowAnyEquipState) {
            boundObj = nullptr;
            boundFormID = 0;
            droppedRef.reset();
            droppedOrigHandle.reset();
            stackIndices.clear();
            equippedStacks.clear();
            totalCount = 0;

            RE::BSAutoWriteLock lock{ a_invOwner->inventoryList->rwLock };

            for (const auto& invItem : a_invOwner->inventoryList->data) {
                if (!invItem.object || invItem.object->IsDeleted()) {
                    continue;
                }
                const std::uint32_t itemFormID = invItem.object->GetFormID();
                if (a_target.baseFormID != 0 && itemFormID != a_target.baseFormID) {
                    continue;
                }

                const std::string_view baseSv = RE::TESFullName::GetFullName(*invItem.object);
                const std::string baseName = baseSv.empty() ? std::string{} : std::string(baseSv);
                const bool hasBaseName = !TrimWhitespace(baseName).empty();
                const auto objFormType = invItem.object->GetFormType();
                const bool allowInstancedFullName =
                    (objFormType == RE::ENUM_FORM_ID::kWEAP || objFormType == RE::ENUM_FORM_ID::kARMO);

                std::vector<std::uint32_t> candidateIndices;
                std::vector<std::pair<std::uint32_t, const RE::TBO_InstanceData*>> candidateEquipped;
                std::uint64_t candidateTotal = 0;
                RE::NiPointer<RE::TESObjectREFR> candidateDroppedRef;
                RE::ObjectRefHandle candidateDroppedOrigHandle{};

                std::uint32_t stackIdx = 0;
                for (auto* stack = invItem.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIdx) {
                    const std::uint32_t cnt = stack->GetCount();
                    if (cnt == 0) {
                        continue;
                    }
                    const bool equipped = stack->IsEquipped();
                    if (!a_allowAnyEquipState && equipped != a_target.isEquipped) {
                        continue;
                    }
                    if (stack->flags.all(RE::BGSInventoryItem::Stack::Flag::kEquipStateLocked)) {
                        continue;
                    }

                    RE::ExtraDataList* stackExtra = stack->extra.get();
                    const bool hasOrigRefHandle =
                        stack->flags.all(RE::BGSInventoryItem::Stack::Flag::kTemporary) &&
                        stackExtra != nullptr &&
                        stackExtra->GetByType<RE::ExtraReferenceHandles>() != nullptr;
                    if (hasOrigRefHandle != a_target.isTemporary) {
                        continue;
                    }

                    const RE::TBO_InstanceData* instData = ResolveItemInstanceData(invItem.object, stackExtra);
                    if (!invItem.object->GetPlayable(instData)) {
                        continue;
                    }

                    const bool favorite = (stackExtra && stackExtra->HasType(RE::EXTRA_DATA_TYPE::kFavorite));
                    if (favorite != a_target.isFavorite) {
                        continue;
                    }
                    const bool questItem = IsStackQuestObject(stackExtra);
                    if (questItem != a_target.isQuestItem) {
                        continue;
                    }

                    bool legendary = false;
                    const std::uint64_t stackSig = ComputeStackSignature(stackExtra, stackIdx, legendary);
                    if (legendary != a_target.isLegendary || stackSig != a_target.stackSignature) {
                        continue;
                    }

                    std::string displayName;
                    if (!ResolveInventoryStackDisplayName(
                            const_cast<RE::BGSInventoryItem&>(invItem),
                            stackIdx,
                            stackExtra,
                            baseName,
                            hasBaseName,
                            allowInstancedFullName,
                            displayName)) {
                        continue;
                    }

                    const bool nameMatches =
                        (displayName == a_target.name) ||
                        (!targetCleanUpper.empty() && CleanUpperKey(displayName) == targetCleanUpper);
                    if (!nameMatches) {
                        continue;
                    }

                    if (hasOrigRefHandle) {
                        if (auto* refHandles = stackExtra->GetByType<RE::ExtraReferenceHandles>();
                            refHandles && refHandles->originalRef) {
                            auto refPtr = refHandles->originalRef.get();
                            if (refPtr && !refPtr->IsDeleted() && !IsRefDisabled(refPtr.get())) {
                                candidateDroppedRef = std::move(refPtr);
                                candidateDroppedOrigHandle = refHandles->originalRef;
                                candidateTotal = cnt;
                                candidateIndices.clear();
                                candidateIndices.push_back(stackIdx);
                                break;
                            }
                        }
                        continue;
                    }

                    candidateIndices.push_back(stackIdx);
                    if (equipped) {
                        candidateEquipped.emplace_back(stackIdx, instData);
                    }
                    candidateTotal += cnt;
                }

                if (!candidateIndices.empty() && candidateTotal > 0) {
                    boundObj = invItem.object;
                    boundFormID = itemFormID;
                    stackIndices = std::move(candidateIndices);
                    equippedStacks = std::move(candidateEquipped);
                    totalCount = candidateTotal;
                    droppedRef = std::move(candidateDroppedRef);
                    droppedOrigHandle = candidateDroppedOrigHandle;
                    break;
                }
            }
        };

        collectTargetStacks(false);

        if (!boundObj || totalCount == 0) {
            const std::int64_t nowMs = SteadyNowMs();
            const std::int64_t prevWarn = _diagQcTakeFailLastLogMs.load(std::memory_order_relaxed);
            if ((nowMs - prevWarn) >= 1000) {
                _diagQcTakeFailLastLogMs.store(nowMs, std::memory_order_relaxed);
                logger::warn(
                    "CustomQuickLoot take match failed: owner={:08X} base={:08X} '{}' eq={} temp={} mode={}",
                    a_invOwner->GetFormID(),
                    a_target.baseFormID,
                    a_target.name,
                    a_target.isEquipped,
                    a_target.isTemporary,
                    static_cast<int>(a_mode));
            }
            return false;
        }

        if (droppedRef) {
            const std::uint64_t beforeProxy = CountBoundObjectInInventory(a_invOwner, boundFormID);
            a_player->PickUpObject(droppedRef.get(),
                static_cast<std::int32_t>(std::min<std::uint64_t>(totalCount, std::numeric_limits<std::int32_t>::max())),
                true);

            bool foundResidualTemp = false;
            std::uint32_t cleanupIdx = 0;
            std::uint32_t cleanupCount = 0;
            if (a_invOwner->inventoryList) {
                RE::BSAutoWriteLock lock{ a_invOwner->inventoryList->rwLock };
                for (const auto& invItem : a_invOwner->inventoryList->data) {
                    if (!invItem.object || invItem.object->IsDeleted() ||
                        invItem.object->GetFormID() != boundFormID) {
                        continue;
                    }
                    std::uint32_t stackIdx = 0;
                    for (auto* stack = invItem.stackData.get(); stack; stack = stack->nextStack.get(), ++stackIdx) {
                        if (stack->GetCount() == 0 ||
                            !stack->flags.all(RE::BGSInventoryItem::Stack::Flag::kTemporary)) {
                            continue;
                        }
                        if (auto* stackExtra = stack->extra.get()) {
                            if (auto* refHandles = stackExtra->GetByType<RE::ExtraReferenceHandles>();
                                refHandles && refHandles->originalRef == droppedOrigHandle) {
                                foundResidualTemp = true;
                                cleanupIdx = stackIdx;
                                cleanupCount = stack->GetCount();
                                break;
                            }
                        }
                    }
                    if (foundResidualTemp) {
                        break;
                    }
                }
            }

            if (foundResidualTemp && cleanupCount > 0) {
                const std::int32_t rmCount = static_cast<std::int32_t>(std::min<std::uint32_t>(
                    cleanupCount,
                    static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())));
                RE::TESObjectREFR::RemoveItemData cleanupData(boundObj, rmCount);
                cleanupData.stackData.push_back(cleanupIdx);
                cleanupData.a_otherContainer = nullptr;
                cleanupData.reason = RE::ITEM_REMOVE_REASON::kNone;
                (void)a_invOwner->RemoveItem(cleanupData);
            }
            const std::uint64_t afterProxy = CountBoundObjectInInventory(a_invOwner, boundFormID);
            return afterProxy < beforeProxy || !foundResidualTemp;
        }

        // Deadlock-safe: rwLock is NOT held here. Unequip equipped stacks on Actors first so RemoveItem succeeds.
        if (!equippedStacks.empty()) {
            if (auto* ownerActor = a_invOwner->As<RE::Actor>()) {
                if (auto* eqMgr = RE::ActorEquipManager::GetSingleton()) {
                    for (const auto& [eqIdx, eqInst] : equippedStacks) {
                        RE::BGSObjectInstance objInst(boundObj, const_cast<RE::TBO_InstanceData*>(eqInst));
                        (void)eqMgr->UnequipObject(
                            ownerActor,
                            &objInst,
                            0,
                            nullptr,
                            eqIdx,
                            false,
                            true,
                            false,
                            true,
                            nullptr);
                    }
                    // Unequipping can merge/shift stacks, so refresh indices safely under a fresh lock
                    RE::TESBoundObject* prevObj = boundObj;
                    std::uint32_t prevFormID = boundFormID;
                    std::uint64_t prevTotal = totalCount;
                    collectTargetStacks(true);
                    if (!boundObj) {
                        boundObj = prevObj;
                        boundFormID = prevFormID;
                        totalCount = prevTotal;
                        stackIndices.clear();
                    } else {
                        totalCount = std::min(totalCount, prevTotal);
                    }
                }
            }
        }

        if (!boundObj || totalCount == 0) {
            return false;
        }

        const std::int32_t takeCount = static_cast<std::int32_t>(std::min<std::uint64_t>(
            totalCount,
            static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())));
        const auto removeReason = (a_mode == RE::QuickContainerMode::kTeammate)
            ? RE::ITEM_REMOVE_REASON::kStoreTeammate
            : RE::ITEM_REMOVE_REASON::kStoreContainer;

        const std::uint64_t countBefore = CountBoundObjectInInventory(a_invOwner, boundFormID);
        if (!stackIndices.empty()) {
            RE::TESObjectREFR::RemoveItemData data(boundObj, takeCount);
            for (const std::uint32_t idx : stackIndices) {
                data.stackData.push_back(idx);
            }
            data.a_otherContainer = a_player;
            data.reason = removeReason;
            (void)a_invOwner->RemoveItem(data);
        }
        std::uint64_t countAfter = CountBoundObjectInInventory(a_invOwner, boundFormID);

        if (countAfter >= countBefore && countBefore > 0 &&
            (countBefore <= static_cast<std::uint64_t>(takeCount) || stackIndices.empty())) {
            RE::TESObjectREFR::RemoveItemData fallbackData(boundObj, takeCount);
            fallbackData.a_otherContainer = a_player;
            fallbackData.reason = removeReason;
            (void)a_invOwner->RemoveItem(fallbackData);
            countAfter = CountBoundObjectInInventory(a_invOwner, boundFormID);
        }

        if (countAfter >= countBefore && countBefore > 0) {
            const std::int64_t nowMs = SteadyNowMs();
            const std::int64_t prevWarn = _diagQcTakeFailLastLogMs.load(std::memory_order_relaxed);
            if ((nowMs - prevWarn) >= 1000) {
                _diagQcTakeFailLastLogMs.store(nowMs, std::memory_order_relaxed);
                logger::warn(
                    "CustomQuickLoot take did not reduce count: owner={:08X} item={:08X} '{}' eq={} mode={} before={} after={}",
                    a_invOwner->GetFormID(),
                    boundFormID,
                    a_target.name,
                    a_target.isEquipped,
                    static_cast<int>(a_mode),
                    countBefore,
                    countAfter);
            }
            return false;
        }

        a_player->PlayPickUpSound(boundObj, true, false);
        return true;
    }

    const QuickContainerMergeResult& Controller::ResolveMergedQuickContainerWindow(
        std::uint32_t a_ownerFormID,
        const CachedInventorySnapshot& a_invSnap,
        const std::vector<QuickContainerItemEntry>& a_rawWindow,
        std::int32_t a_rawSel,
        int a_maxRows,
        bool a_expandEnabled)
    {
        return HUDController::ResolveMergedQuickContainerWindow(
            _cachedMerge,
            a_ownerFormID,
            a_invSnap,
            a_rawWindow,
            a_rawSel,
            a_maxRows,
            a_expandEnabled);
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
                EnsureActivateHook();
                EnsureMenuControlsHook();
                EnsurePlayerControlsHooks();
                EnsureUIInputHook();
                EnsureHUDDataModelHook();
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
        } else if (a_event.menuName == "ExamineMenu"sv) {
            if (a_event.opening) {
                _featuredExamineOpenMs.store(SteadyNowMs(), std::memory_order_release);
                _featuredExamineWaitRelease.store(true, std::memory_order_release);
                ClearOverlaySnapshot();
            } else {
                _featuredExamineOpenMs.store(0, std::memory_order_release);
                _featuredExamineWaitRelease.store(false, std::memory_order_release);
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

    namespace
    {
        [[nodiscard]] RE::NiPoint3 ResolvePlayerEyeOrigin(RE::PlayerCharacter* a_player) noexcept
        {
            const RE::NiPoint3 pPos = a_player->GetPosition();
            if (auto* root = a_player->Get3D()) {
                if (auto* head = root->GetObjectByName("HEAD"sv)) {
                    return head->world.translate;
                }
                if (auto* spine = root->GetObjectByName("SPINE2"sv)) {
                    return spine->world.translate;
                }
            }
            const float eyeZ = (a_player->eyeHeight > 10.0f && a_player->eyeHeight < 200.0f) ? a_player->eyeHeight : 95.0f;
            const float zMul = a_player->IsSneaking() ? 0.62f : 0.80f;
            return RE::NiPoint3{ pPos.x, pPos.y, pPos.z + eyeZ * zMul };
        }

        [[nodiscard]] bool HasClearLineOfSightToRef(
            RE::PlayerCharacter* a_player,
            RE::TESObjectCELL* a_cell,
            RE::TESObjectREFR* a_targetRef,
            const RE::NiPoint3& a_rayOrigin,
            const RE::NiPoint3& a_targetPt,
            bool a_allowWallRecess,
            bool& a_outHitSolidWall) noexcept
        {
            a_outHitSolidWall = false;
            if (!a_player || !a_cell || !a_targetRef) {
                return true;
            }
            RE::NiPoint3 delta{
                a_targetPt.x - a_rayOrigin.x,
                a_targetPt.y - a_rayOrigin.y,
                a_targetPt.z - a_rayOrigin.z
            };
            const float rayDist = delta.Length();
            if (!std::isfinite(rayDist) || rayDist < 2.0f) {
                return true;
            }
            RE::NiPoint3 dir{ delta.x / rayDist, delta.y / rayDist, delta.z / rayDist };

            auto evalPickLayer = [&](std::uint32_t a_layerFilter, bool& a_outHasHit, bool& a_outHitTarget, float& a_outOrigFrac, bool& a_outSolid) {
                RE::NiPoint3 curStart = a_rayOrigin;
                float remainingLen = rayDist;
                float traveled = 0.0f;
                for (int attempt = 0; attempt < 2; ++attempt) {
                    if (remainingLen < 2.0f) {
                        a_outHasHit = false;
                        a_outHitTarget = false;
                        a_outOrigFrac = 1.0f;
                        a_outSolid = false;
                        return;
                    }
                    RE::bhkPickData pickData;
                    RE::NiPoint3 start = curStart;
                    RE::NiPoint3 aimVec = dir;
                    pickData.InitializeWithCollisionFilter(a_layerFilter, start, aimVec, remainingLen);
                    (void)a_cell->Pick(pickData);
                    if (!pickData.HasHit()) {
                        a_outHasHit = false;
                        a_outHitTarget = false;
                        a_outOrigFrac = 1.0f;
                        a_outSolid = false;
                        return;
                    }
                    const float segFrac = std::clamp(pickData.GetHitFraction(), 0.0f, 1.0f);
                    bool hitPlayer = false;
                    bool hitTarget = false;
                    if (auto* hitObj = pickData.GetNiAVObject()) {
                        if (auto* hitRef = AnchorManager::ResolveRefFrom3D(hitObj)) {
                            hitPlayer = (hitRef == a_player);
                            hitTarget = (hitRef == a_targetRef);
                        }
                    }
                    if (hitPlayer && attempt == 0) {
                        const float advance = segFrac * remainingLen + 4.0f;
                        traveled += advance;
                        remainingLen = rayDist - traveled;
                        curStart = RE::NiPoint3{
                            a_rayOrigin.x + dir.x * traveled,
                            a_rayOrigin.y + dir.y * traveled,
                            a_rayOrigin.z + dir.z * traveled
                        };
                        continue;
                    }
                    a_outHasHit = true;
                    a_outHitTarget = hitTarget;
                    a_outOrigFrac = std::clamp((traveled + segFrac * remainingLen) / rayDist, 0.0f, 1.0f);
                    std::uint32_t filterInfo = pickData.result.hitBodyInfo.m_shapeCollisionFilterInfo.storage;
                    if (filterInfo == 0) {
                        filterInfo = pickData.GetHitCollisionFilterInfo();
                    }
                    const auto hitLayer = static_cast<std::uint8_t>(filterInfo & 0x7Fu);
                    a_outSolid = (!hitTarget && !hitPlayer && IsSolidWallCollisionLayer(hitLayer));
                    return;
                }
                a_outHasHit = false;
                a_outHitTarget = false;
                a_outOrigFrac = 1.0f;
                a_outSolid = false;
            };

            bool hasHit = false;
            bool hitTarget = false;
            float hitFrac = 1.0f;
            bool hitSolid = false;
            evalPickLayer(static_cast<std::uint32_t>(RE::COL_LAYER::kItemPicker), hasHit, hitTarget, hitFrac, hitSolid);

            if (!hasHit || (!hitSolid && !hitTarget)) {
                bool losHasHit = false;
                bool losHitTarget = false;
                float losFrac = 1.0f;
                bool losSolid = false;
                evalPickLayer(static_cast<std::uint32_t>(RE::COL_LAYER::kLOS), losHasHit, losHitTarget, losFrac, losSolid);
                if (losHasHit && (losSolid || !hasHit || losHitTarget)) {
                    hasHit = losHasHit;
                    hitTarget = losHitTarget;
                    hitFrac = losFrac;
                    hitSolid = losSolid;
                }
            }

            a_outHitSolidWall = hitSolid;
            const float targetDeltaZ = a_targetPt.z - a_player->GetPosition().z;
            return IsThirdPersonAssistLosClear(
                hasHit, hitTarget, hitFrac, rayDist, targetDeltaZ, hitSolid, a_allowWallRecess);
        }
    }

    RE::NiPointer<RE::TESObjectREFR> Controller::FindThirdPersonAssistTarget(
        RE::PlayerCharacter* a_player,
        RE::TESObjectREFR* a_crosshairRef,
        bool a_looseItemsOnly,
        const ConfigData& a_cfg,
        RE::NiCamera* a_cam)
    {
        if (!a_player || !a_cfg.tpAssistEnabled) {
            return nullptr;
        }
        auto* cell = a_player->GetParentCell();
        if (!cell) {
            return nullptr;
        }

        const float baseMaxDist = std::clamp(a_cfg.tpAssistMaxDistance, 60.0f, 250.0f);
        const float maxDist = a_looseItemsOnly ? std::min(baseMaxDist, 85.0f) : baseMaxDist;
        const float maxDistSq = maxDist * maxDist;
        const float halfRad = std::clamp(a_cfg.tpAssistConeDegrees, 20.0f, 90.0f) * (3.14159265f / 180.0f);
        const float minCos = std::cos(halfRad);

        const RE::NiPoint3 playerPos = a_player->GetPosition();
        const RE::NiPoint3 rayOrigin = ResolvePlayerEyeOrigin(a_player);
        const float heading = a_player->data.angle.z;
        const float fwdX = std::sin(heading);
        const float fwdY = std::cos(heading);

        const std::uint32_t currentAssistID =
            _tpAssistRefHandle ? ([&]() { auto p = _tpAssistRefHandle.get(); return p ? p->GetFormID() : 0u; })() : 0u;
        const std::uint32_t crosshairID = a_crosshairRef ? a_crosshairRef->GetFormID() : 0u;

        std::vector<RE::NiPointer<RE::TESObjectREFR>> rawCandidates;
        rawCandidates.reserve(32);
        {
            RE::BSAutoLock lock{ cell->spinLock };
            for (const auto& refPtr : cell->references) {
                auto* ref = refPtr.get();
                if (!ref || ref == a_player || ref->IsDeleted() || ref->predestroyed ||
                    IsRefDisabled(ref) || (ref->formFlags & (1u << 12)) != 0u) {
                    continue;
                }
                auto* base = ref->GetObjectReference();
                if (!base || base->IsMarker() || base->IsOcclusionMarker() || base->GetDangerous()) {
                    continue;
                }
                const auto formType = base->GetFormType();
                const bool whitelistedType =
                    (formType == RE::ENUM_FORM_ID::kWEAP ||
                     formType == RE::ENUM_FORM_ID::kARMO ||
                     formType == RE::ENUM_FORM_ID::kALCH ||
                     formType == RE::ENUM_FORM_ID::kAMMO ||
                     formType == RE::ENUM_FORM_ID::kMISC ||
                     formType == RE::ENUM_FORM_ID::kKEYM ||
                     formType == RE::ENUM_FORM_ID::kBOOK ||
                     formType == RE::ENUM_FORM_ID::kNOTE ||
                     formType == RE::ENUM_FORM_ID::kOMOD ||
                     formType == RE::ENUM_FORM_ID::kCONT ||
                     formType == RE::ENUM_FORM_ID::kDOOR ||
                     formType == RE::ENUM_FORM_ID::kFURN ||
                     formType == RE::ENUM_FORM_ID::kTERM ||
                     formType == RE::ENUM_FORM_ID::kFLOR ||
                     formType == RE::ENUM_FORM_ID::kTREE ||
                     formType == RE::ENUM_FORM_ID::kACTI);
                if (!whitelistedType) {
                    continue;
                }
                if ((formType == RE::ENUM_FORM_ID::kFLOR || formType == RE::ENUM_FORM_ID::kTREE) &&
                    (ref->formFlags & (1u << 13)) != 0u) {
                    continue;
                }
                const auto cat = AnchorManager::ClassifyReference(ref);
                if (cat == AnchorManager::TargetCategory::kActor) {
                    continue;
                }
                if (a_looseItemsOnly &&
                    (cat != AnchorManager::TargetCategory::kItem ||
                     formType == RE::ENUM_FORM_ID::kACTI)) {
                    continue;
                }
                const bool catEnabled =
                    (cat == AnchorManager::TargetCategory::kItem && a_cfg.tpAssistItems) ||
                    (cat == AnchorManager::TargetCategory::kDoor && a_cfg.tpAssistDoors) ||
                    (cat == AnchorManager::TargetCategory::kFurnitureTerminal && a_cfg.tpAssistFurniture) ||
                    (cat == AnchorManager::TargetCategory::kContainer && a_cfg.tpAssistContainers);
                if (!catEnabled) {
                    continue;
                }
                if (cat == AnchorManager::TargetCategory::kItem &&
                    formType == RE::ENUM_FORM_ID::kACTI &&
                    !a_cfg.tpAssistDoors) {
                    continue;
                }
                const RE::NiPoint3 rPos = ref->GetPosition();
                const float dx = rPos.x - playerPos.x;
                const float dy = rPos.y - playerPos.y;
                const float dz = rPos.z - playerPos.z;
                if (dz < -30.0f || dz > 140.0f) {
                    continue;
                }
                const float distSq = dx * dx + dy * dy + dz * dz;
                if (distSq > maxDistSq) {
                    continue;
                }
                const float hLenRaw = std::hypot(dx, dy);
                if (hLenRaw > 45.0f && ((dx * fwdX + dy * fwdY) / hLenRaw) < (minCos - 0.25f)) {
                    continue;
                }
                rawCandidates.push_back(refPtr);
                if (rawCandidates.size() >= 256) {
                    break;
                }
            }
        }

        struct ScoredRef
        {
            RE::NiPointer<RE::TESObjectREFR> ref;
            RE::NiPoint3 targetPt{};
            float score{ -1.0f };
        };
        std::vector<ScoredRef> scored;
        scored.reserve(rawCandidates.size());

        ConfigData zeroOffsetCfg = a_cfg;
        zeroOffsetCfg.doorOffset = {};
        zeroOffsetCfg.npcOffset = {};
        zeroOffsetCfg.containerOffset = {};
        zeroOffsetCfg.itemOffset = {};
        zeroOffsetCfg.furnitureOffset = {};

        for (auto& refPtr : rawCandidates) {
            auto* ref = refPtr.get();
            if (!ref) {
                continue;
            }
            auto* root3D = ref->Get3D();
            if (!root3D || root3D->GetAppCulled()) {
                continue;
            }
            if (crosshairID != 0 && ref->GetFormID() == crosshairID) {
                continue;
            }
            auto* base = ref->GetObjectReference();
            if (!base) {
                continue;
            }
            RE::BSStringT<char> actText;
            if (!base->GetActivateText(ref, actText) || actText.empty()) {
                continue;
            }

            std::string visibleName;
            if (auto* extraList = ref->extraList.get()) {
                if (auto* textExtra = extraList->GetByType<RE::ExtraTextDisplayData>()) {
                    if (!textExtra->displayName.empty() && !TrimWhitespace(textExtra->displayName.c_str()).empty()) {
                        visibleName = textExtra->displayName.c_str();
                    } else {
                        const auto& resolvedName = textExtra->GetDisplayName(base);
                        if (!resolvedName.empty() && !TrimWhitespace(resolvedName.c_str()).empty()) {
                            visibleName = resolvedName.c_str();
                        }
                    }
                }
            }
            if (visibleName.empty()) {
                const std::string_view baseSv = RE::TESFullName::GetFullName(*base);
                if (!baseSv.empty() && !TrimWhitespace(baseSv).empty()) {
                    visibleName.assign(baseSv);
                }
            }
            if (visibleName.empty()) {
                continue;
            }

            const auto formType = base->GetFormType();
            auto parsedAct = TargetInfo::SplitActivateText(actText.c_str());
            std::string_view actVerb = !parsedAct.verb.empty() ? std::string_view(parsedAct.verb) : std::string_view(actText.c_str());
            std::string translatedVerb;
            if (!actVerb.empty() && actVerb.front() == '$') {
                translatedVerb = TranslateScaleformKey(actVerb);
                actVerb = translatedVerb;
            }
            if (IsDisallowedAssistTargetNameOrVerb(visibleName, actVerb, formType == RE::ENUM_FORM_ID::kACTI)) {
                continue;
            }

            const bool isInventoryItemType =
                (formType == RE::ENUM_FORM_ID::kWEAP ||
                 formType == RE::ENUM_FORM_ID::kARMO ||
                 formType == RE::ENUM_FORM_ID::kALCH ||
                 formType == RE::ENUM_FORM_ID::kAMMO ||
                 formType == RE::ENUM_FORM_ID::kMISC ||
                 formType == RE::ENUM_FORM_ID::kKEYM ||
                 formType == RE::ENUM_FORM_ID::kBOOK ||
                 formType == RE::ENUM_FORM_ID::kNOTE ||
                 formType == RE::ENUM_FORM_ID::kOMOD);
            if (isInventoryItemType) {
                const auto* instData = ResolveItemInstanceData(base, ref->extraList.get());
                if (!base->GetPlayable(instData)) {
                    continue;
                }
            }

            const auto cat = AnchorManager::ClassifyReference(ref);
            const bool isLooseItem = (cat == AnchorManager::TargetCategory::kItem) &&
                                     (formType != RE::ENUM_FORM_ID::kACTI);
            bool usedGeom = false;
            const RE::NiPoint3 anchorPos = AnchorManager::ResolveAnchorWorldPosition(ref, nullptr, zeroOffsetCfg, &usedGeom);
            if (!usedGeom) {
                continue;
            }
            const float anchorDz = anchorPos.z - playerPos.z;
            if (anchorDz < -30.0f || anchorDz > 140.0f) {
                continue;
            }
            const float hx = anchorPos.x - playerPos.x;
            const float hy = anchorPos.y - playerPos.y;
            const float hLen = std::hypot(hx, hy);
            const float dot = (hLen > 1.0f) ? ((hx * fwdX + hy * fwdY) / hLen) : 1.0f;
            const float dist3D = rayOrigin.GetDistance(anchorPos);

            ThirdPersonAssistCandidate cand{};
            cand.dist = dist3D;
            cand.forwardDot = dot;
            cand.isLooseItem = isLooseItem;
            cand.isCurrentAssistTarget = (currentAssistID != 0 && ref->GetFormID() == currentAssistID);
            if (a_cam) {
                float sx = 0.0f;
                float sy = 0.0f;
                if (a_cam->WorldPtToScreenPt(anchorPos, sx, sy, 0.001f)) {
                    const float camDist = std::hypot((sx - 0.5f) * 1.5f, sy - 0.5f);
                    cand.crosshairDot = std::clamp(1.0f - camDist * 1.15f, -1.0f, 1.0f);
                    cand.hasCrosshairDot = true;
                }
            }

            const float s = ScoreThirdPersonCandidate(cand, maxDist, minCos);
            if (s > 0.0f) {
                scored.push_back({ refPtr, anchorPos, s });
            }
        }

        if (scored.empty()) {
            return nullptr;
        }

        std::sort(scored.begin(), scored.end(), [](const ScoredRef& a, const ScoredRef& b) {
            return a.score > b.score;
        });

        const std::size_t maxRayChecks = std::min<std::size_t>(scored.size(), 4);
        for (std::size_t i = 0; i < maxRayChecks; ++i) {
            auto* candRef = scored[i].ref.get();
            bool hitSolidWall = false;
            if (HasClearLineOfSightToRef(a_player, cell, candRef, rayOrigin, scored[i].targetPt, false, hitSolidWall)) {
                return scored[i].ref;
            }
        }

        return nullptr;
    }

    bool Controller::OnHUDAdvance(RE::IMenu* a_hudMenu, float a_timeDelta)
    {
        PerformDeferredResets();

        if (!a_hudMenu || !a_hudMenu->uiMovie || !a_hudMenu->uiMovie->GetVisible()) {
            _lastQuickLootActiveMs = 0;
            RestoreCompanionEngineSettings();
            RestoreGrabDelaySetting();
            ClearOverlaySnapshot();
            return false;
        }

        const ConfigData cfg = Config::GetSingleton().GetSnapshot();
        _customQcTakeDebounceMs.store(static_cast<std::int64_t>(cfg.qcTakeDebounceMs), std::memory_order_relaxed);
        _featuredExamineGraceMs.store(static_cast<std::int64_t>(cfg.featuredItemGraceMs), std::memory_order_relaxed);
        _holdDurationMs.store(static_cast<std::int64_t>(std::lround(cfg.holdToActivateDuration * 1000.0f)), std::memory_order_relaxed);
        if (cfg.enabled) {
            ResolveGrabDelaySetting();
            ApplyGrabDelaySetting(cfg.grabHoldDelay);
        } else {
            RestoreGrabDelaySetting();
        }
        const bool hudOverlayActive = MenuFrameworkUI::IsHudOverlayActive();
        const bool modernRolloverActive = (cfg.promptStyle == 1 && hudOverlayActive);
        const bool modernQcActive = (modernRolloverActive && cfg.modernQuickContainer);

        auto& movie = *a_hudMenu->uiMovie;
        ResolvedClips clips;
        const bool ignoreQcAlpha = _qcState.IsHidingAlpha() || modernQcActive;
        if (!LocateHUDClips(*a_hudMenu, movie, ignoreQcAlpha, clips)) {
            _lastQuickLootActiveMs = 0;
            RestoreCompanionEngineSettings();
            RestoreGrabDelaySetting();
            ClearOverlaySnapshot();
            return false;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* playerCam = RE::PlayerCamera::GetSingleton();
        const std::int64_t frameNowMs = SteadyNowMs();
        const bool inPowerArmor = player && RE::PowerArmor::ActorInPowerArmor(*player);
        const bool bypassActive = inPowerArmor && cfg.powerArmorBypass;
        const auto visRect = movie.GetVisibleFrameRect();
        const float stageW = visRect.x2 - visRect.x1;
        const float stageH = visRect.y2 - visRect.y1;

        const bool isThirdPersonCam =
            (playerCam && playerCam->currentState &&
             playerCam->currentState->id == RE::CameraState::k3rdPerson);
        const bool isIronSightsCam =
            (playerCam && playerCam->currentState &&
             playerCam->currentState->id == RE::CameraState::kIronSights);
        const bool rawPlayerAiming = (player != nullptr) &&
            (player->gunState == RE::GUN_STATE::kSighted ||
             player->gunState == RE::GUN_STATE::kFireSighted ||
             isIronSightsCam);
        std::int64_t lastAimMs = _lastAimingMs.load(std::memory_order_acquire);
        const bool effectiveAiming = IsAimingWithCooldown(rawPlayerAiming, frameNowMs, lastAimMs, 280);
        _lastAimingMs.store(lastAimMs, std::memory_order_release);
        const bool inCommandModePre = (player && static_cast<bool>(player->actorDoingPlayerCommand));
        bool inWorkshopPre = false;
        if (auto* ui = RE::UI::GetSingleton()) {
            inWorkshopPre = ui->GetMenuOpen("WorkshopMenu"sv);
        }

        RE::NiPointer<RE::TESObjectREFR> pickedSmartPtr;
        RE::TESObjectREFR* targetRef = nullptr;
        RE::NiAVObject* targeted3DHint = nullptr;
        auto* cam = RE::Main::WorldRootCamera();

        if (player && (cfg.limitCompanionPromptDistance || (cfg.enabled && !bypassActive))) {
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

        // Third-person character-facing proximity assist
        RE::NiPointer<RE::TESObjectREFR> tpAssistSmartPtr;
        bool usingTpAssist = false;
        bool crosshairBlockedCompanion = false;
        if (targetRef && cfg.limitCompanionPromptDistance && clips.rolloverRawVisible && player) {
            if (auto* preActor = targetRef->As<RE::Actor>();
                preActor && !IsActorDeadOrDying(preActor) && !IsActorBleedout(preActor) && IsCompanionActor(preActor, player)) {
                const float preDist = player->GetPosition().GetDistance(targetRef->GetPosition());
                const float maxCompD = (player->IsInCombat() || preActor->IsInCombat())
                    ? cfg.companionCombatMaxDistance
                    : cfg.companionMaxDistance;
                if (preDist > maxCompD) {
                    crosshairBlockedCompanion = true;
                }
            }
        }

        const bool canRunTpAssist =
            modernRolloverActive &&
            cfg.enabled &&
            cfg.tpAssistEnabled &&
            !bypassActive &&
            isThirdPersonCam &&
            !effectiveAiming &&
            !clips.quickContainerVisible &&
            !inCommandModePre &&
            !inWorkshopPre &&
            player != nullptr &&
            cam != nullptr;

        if (canRunTpAssist) {
            const bool crosshairHasTarget =
                (targetRef != nullptr) && clips.rolloverRawVisible && !crosshairBlockedCompanion;
            bool crosshairIsLooseItem = false;
            float crosshairDist = 0.0f;
            if (crosshairHasTarget) {
                crosshairDist = player->GetPosition().GetDistance(targetRef->GetPosition());
                const auto cCat = AnchorManager::ClassifyReference(targetRef);
                const auto* cBase = targetRef->GetObjectReference();
                crosshairIsLooseItem = (cCat == AnchorManager::TargetCategory::kItem) &&
                                       (cBase == nullptr || cBase->GetFormType() != RE::ENUM_FORM_ID::kACTI);
            }
            const bool wantOverride = ShouldThirdPersonAssistOverrideCrosshair(
                cfg.tpAssistMode,
                crosshairHasTarget,
                clips.quickContainerVisible,
                crosshairIsLooseItem,
                crosshairDist,
                cfg.tpAssistMaxDistance);

            if (wantOverride) {
                const std::int64_t nowMs = SteadyNowMs();
                RE::NiPointer<RE::TESObjectREFR> existingPtr = _tpAssistRefHandle ? _tpAssistRefHandle.get() : nullptr;
                bool lockedStillValid = false;
                if (existingPtr && !existingPtr->IsDeleted() && !IsRefDisabled(existingPtr.get())) {
                    auto* locked3D = existingPtr->Get3D();
                    if (locked3D && !locked3D->GetAppCulled() &&
                        player->GetPosition().GetDistance(existingPtr->GetPosition()) <= (cfg.tpAssistMaxDistance + 15.0f)) {
                        if (auto* cell = player->GetParentCell()) {
                            ConfigData zeroOffsetCfg = cfg;
                            zeroOffsetCfg.doorOffset = {};
                            zeroOffsetCfg.npcOffset = {};
                            zeroOffsetCfg.containerOffset = {};
                            zeroOffsetCfg.itemOffset = {};
                            zeroOffsetCfg.furnitureOffset = {};
                            bool usedGeom = false;
                            const RE::NiPoint3 lockedPt =
                                AnchorManager::ResolveAnchorWorldPosition(existingPtr.get(), nullptr, zeroOffsetCfg, &usedGeom);
                            if (usedGeom) {
                                bool hitWall = false;
                                lockedStillValid = HasClearLineOfSightToRef(
                                    player, cell, existingPtr.get(), ResolvePlayerEyeOrigin(player), lockedPt, false, hitWall);
                            }
                        }
                    }
                }
                const bool keepLocked = ShouldKeepLockedThirdPersonTarget(
                    _tpAssistLockedFormID,
                    _tpAssistLockStartMs,
                    nowMs,
                    static_cast<std::int64_t>(cfg.tpAssistLockMs),
                    lockedStillValid,
                    crosshairHasTarget);

                if (!keepLocked && (nowMs - _tpAssistLastScanMs) >= 50) {
                    _tpAssistLastScanMs = nowMs;
                    const bool looseOnly = crosshairHasTarget;
                    auto found = FindThirdPersonAssistTarget(player, targetRef, looseOnly, cfg, cam);
                    if (found) {
                        const std::uint32_t foundID = found->GetFormID();
                        _tpAssistRefHandle = found->GetHandle();
                        if (foundID != _tpAssistLockedFormID) {
                            _tpAssistLockedFormID = foundID;
                            _tpAssistLockStartMs = nowMs;
                        }
                    } else {
                        _tpAssistRefHandle.reset();
                        _tpAssistLockedFormID = 0;
                        _tpAssistLockStartMs = 0;
                    }
                }
                if (_tpAssistRefHandle) {
                    tpAssistSmartPtr = _tpAssistRefHandle.get();
                    if (tpAssistSmartPtr && !tpAssistSmartPtr->IsDeleted() && !IsRefDisabled(tpAssistSmartPtr.get()) &&
                        tpAssistSmartPtr->Get3D() != nullptr &&
                        player->GetPosition().GetDistance(tpAssistSmartPtr->GetPosition()) <= (cfg.tpAssistMaxDistance + 15.0f)) {
                        targetRef = tpAssistSmartPtr.get();
                        targeted3DHint = nullptr;
                        usingTpAssist = true;
                    } else {
                        _tpAssistRefHandle.reset();
                        _tpAssistLockedFormID = 0;
                        _tpAssistLockStartMs = 0;
                    }
                }
            } else {
                _tpAssistRefHandle.reset();
                _tpAssistLockedFormID = 0;
                _tpAssistLockStartMs = 0;
            }
        } else {
            _tpAssistRefHandle.reset();
            _tpAssistLockedFormID = 0;
            _tpAssistLockStartMs = 0;
        }
        _tpAssistActive.store(usingTpAssist, std::memory_order_release);

        // Execute deferred ActivateRef from either 3rd-person assist or Hold-to-Activate completion
        const bool runAssistActivate = _tpAssistActivateRequested.exchange(false, std::memory_order_acq_rel);
        const bool runHoldActivate = _holdPrimaryActivateRequested.exchange(false, std::memory_order_acq_rel);
        if ((runAssistActivate || runHoldActivate) && targetRef && player && !targetRef->IsDeleted()) {
            (void)targetRef->ActivateRef(player, nullptr, 0, false, false, false);
            if (usingTpAssist) {
                _tpAssistRefHandle.reset();
                _tpAssistLockedFormID = 0;
                _tpAssistLockStartMs = 0;
                _tpAssistActive.store(false, std::memory_order_release);
            }
        }

        CompanionFilterDecision companionDecision{};
        auto* targetActor = targetRef ? targetRef->As<RE::Actor>() : nullptr;
        const bool targetIsDead = IsActorDeadOrDying(targetActor);
        const bool targetIsLiveCompanion =
            targetActor && !targetIsDead && player && IsCompanionActor(targetActor, player);
        const bool targetIsLiveNonCompanionNpc =
            targetActor && !targetIsDead && !targetIsLiveCompanion;
        const float targetDist = (targetRef && player)
            ? player->GetPosition().GetDistance(targetRef->GetPosition())
            : 0.0f;

        if (cfg.limitCompanionPromptDistance && player) {
            EnsureActivateHook();
            EnsureMenuControlsHook();
            EnsurePlayerControlsHooks();
            EnsureHUDDataModelHook();
            ResolveCompanionEngineSettings();

            const bool targetIsBleedout = targetIsLiveCompanion && IsActorBleedout(targetActor);

            bool targetHasHealVerb = false;
            if (targetIsLiveCompanion && !targetIsBleedout && clips.hasRollover) {
                std::string btnText;
                if (TryReadTextField(clips.rollover, "RolloverButton_tf"sv, btnText)) {
                    targetHasHealVerb =
                        IsHealActionVerb(btnText) ||
                        IsHealActionVerb(TranslateScaleformKey(btnText));
                }
                if (!targetHasHealVerb) {
                    if (auto* baseObj = targetRef->GetObjectReference()) {
                        RE::BSStringT<char> actText;
                        if (baseObj->GetActivateText(targetRef, actText) && !actText.empty()) {
                            targetHasHealVerb =
                                IsHealActionVerb(actText.c_str()) ||
                                IsHealActionVerb(TranslateScaleformKey(actText.c_str()));
                        }
                    }
                }
            }

            const std::int64_t nowMs = SteadyNowMs();
            const bool quickLootProtected = UpdateQuickLootCompanionProtection(
                _lastQuickLootActiveMs,
                clips.quickContainerVisible,
                targetIsLiveCompanion,
                nowMs,
                kQuickLootCompanionGraceMs);

            CompanionFilterInput filterIn{};
            filterIn.limitEnabled = cfg.limitCompanionPromptDistance;
            filterIn.deprioritizeLoot = cfg.companionDeprioritizeLoot;
            filterIn.maxDistance = cfg.companionMaxDistance;
            filterIn.combatMaxDistance = cfg.companionCombatMaxDistance;
            filterIn.inCombat =
                player->IsInCombat() || (targetActor && targetActor->IsInCombat());
            filterIn.inCommandMode = static_cast<bool>(player->actorDoingPlayerCommand);
            filterIn.quickLootProtected = quickLootProtected;
            filterIn.targetIsLiveCompanion = targetIsLiveCompanion;
            filterIn.targetIsBleedout = targetIsBleedout;
            filterIn.targetHasHealVerb = targetHasHealVerb;
            filterIn.targetDistance = targetDist;
            filterIn.vanillaCommandPickDistance =
                _hasOrigCommandPickDist ? _origCommandPickDist : 2000.0f;
            filterIn.vanillaCommandPickRadius =
                _hasOrigCommandPickRadius ? _origCommandPickRadius : 24.0f;
            filterIn.vanillaActivatePickRadius =
                _hasOrigActivatePickRadius ? _origActivatePickRadius : 16.0f;

            companionDecision = EvaluateCompanionFilter(filterIn);
            if (companionDecision.restoreVanillaSettings) {
                RestoreCompanionEngineSettings();
            } else {
                ApplyCompanionEngineSettings(
                    companionDecision.desiredCommandPickDistance,
                    companionDecision.desiredCommandPickRadius);
            }
        } else {
            _lastQuickLootActiveMs = 0;
            RestoreCompanionEngineSettings();
        }

        const bool aimSuppressRollover = ShouldSuppressForAiming(
            cfg.aimingPromptMode,
            effectiveAiming,
            targetIsLiveNonCompanionNpc || targetIsLiveCompanion,
            false);
        const bool aimSuppressQc = ShouldSuppressForAiming(
            cfg.aimingPromptMode,
            effectiveAiming,
            false,
            true);

        const bool blockHiddenComp = companionDecision.suppressTarget && cfg.blockHiddenCompanionInteractions;
        _suppressCompanionActivate.store(blockHiddenComp, std::memory_order_release);
        _suppressCompanionReady.store(blockHiddenComp, std::memory_order_release);

        bool changed = false;
        bool rolloverFloatingVisible = false;
        OverlaySnapshot nextOverlay{};
        bool holdPriReqNext = false;
        bool holdSecReqNext = false;
        bool holdUseSyntheticTapNext = false;
        bool holdInQcNext = false;
        std::uint64_t holdTargetKeyNext = 0;

        bool wallBlockedPrompt = false;
        if (cfg.blockPromptsThroughWalls && !usingTpAssist && targetRef && player && !targetRef->IsDeleted() &&
            !targetIsLiveCompanion) {
            const auto tCat = AnchorManager::ClassifyReference(targetRef);
            const auto* tBase = targetRef->GetObjectReference();
            const bool isWaterBase =
                tBase &&
                (tBase->GetFormType() == RE::ENUM_FORM_ID::kWATR || tBase->GetWaterType() != nullptr);
            if (!isWaterBase &&
                (tCat == AnchorManager::TargetCategory::kContainer ||
                 tCat == AnchorManager::TargetCategory::kFurnitureTerminal ||
                 (tCat == AnchorManager::TargetCategory::kItem &&
                  (!tBase || tBase->GetFormType() != RE::ENUM_FORM_ID::kACTI)))) {
                if (auto* cell = player->GetParentCell()) {
                    ConfigData zeroOffsetCfg = cfg;
                    zeroOffsetCfg.doorOffset = {};
                    zeroOffsetCfg.npcOffset = {};
                    zeroOffsetCfg.containerOffset = {};
                    zeroOffsetCfg.itemOffset = {};
                    zeroOffsetCfg.furnitureOffset = {};
                    bool usedGeom = false;
                    const RE::NiPoint3 targetAnchor =
                        AnchorManager::ResolveAnchorWorldPosition(targetRef, targeted3DHint, zeroOffsetCfg, &usedGeom);
                    const RE::NiPoint3 targetPt = usedGeom ? targetAnchor : targetRef->GetPosition();
                    const RE::NiPoint3 eyeOrigin =
                        (!player->IsInThirdPerson() && cam) ? cam->world.translate : ResolvePlayerEyeOrigin(player);
                    bool hitSolidWall = false;
                    const bool losClear = HasClearLineOfSightToRef(
                        player, cell, targetRef, eyeOrigin, targetPt, true, hitSolidWall);
                    if (!losClear && hitSolidWall) {
                        wallBlockedPrompt = true;
                    }
                }
            }
        }

        // 1. Rollover prompt controller + metadata publishing
        if (clips.hasRollover) {
            HUDFrameInput in{};
            in.enabled = cfg.enabled;
            in.inPowerArmor = inPowerArmor;
            in.powerArmorBypass = cfg.powerArmorBypass;
            in.smoothingSpeed = cfg.smoothingSpeed;
            in.deltaSeconds = a_timeDelta;
            in.quickContainerVisible = clips.quickContainerVisible;
            in.hideNativeClip = modernRolloverActive || usingTpAssist;
            in.suppressTarget = companionDecision.suppressTarget || aimSuppressRollover || wallBlockedPrompt;

            in.rolloverVisible = clips.rolloverRawVisible || usingTpAssist;
            in.nativeClipVisible = clips.rolloverRawVisible;
            ReadClipBaseTransform(clips.rollover, in);

            bool isWaterRollover = false;
            bool isWaterSurfaceAnchor = false;
            std::string terrainWaterKey;
            std::string terrainWaterVerb;

            auto computePlayerWaterAnchor = [&](float a_waterZ) -> RE::NiPoint3 {
                const auto pPos = player->GetPosition();
                const Vector3 pVec{ pPos.x, pPos.y, pPos.z };
                const Vector3 cVec = cam
                    ? Vector3{ cam->world.translate.x, cam->world.translate.y, cam->world.translate.z }
                    : Vector3{ pPos.x, pPos.y, pPos.z + 90.0f };
                const float yaw = player->data.angle.z;
                const float pitch = player->data.angle.x;
                const float cp = std::cos(pitch);
                const Vector3 cFwd{ cp * std::sin(yaw), cp * std::cos(yaw), -std::sin(pitch) };
                const Vector3 anc = ComputeWaterPromptAnchor(pVec, yaw, cVec, cFwd, a_waterZ, 105.0f);
                return RE::NiPoint3{ anc.x, anc.y, anc.z };
            };

            auto readRolloverPrimaryParsed = [&]() -> TargetInfo::ActionParts {
                std::string btnText;
                if (TryReadTextField(clips.rollover, "RolloverButton_tf"sv, btnText)) {
                    btnText = TranslateScaleformKey(btnText);
                    auto parsed = TargetInfo::SplitActivateText(btnText);
                    if (!parsed.verb.empty() && parsed.verb.front() == '$') {
                        parsed.verb = TargetInfo::ToUpperAscii(TranslateScaleformKey(parsed.verb));
                    }
                    return parsed;
                }
                return {};
            };

            auto isTerrainWaterRollover = [&](const TargetInfo::ActionParts& a_parsed) -> bool {
                if (a_parsed.verb.empty()) {
                    return false;
                }
                if (VerbMatchesToken(a_parsed.verb, "DRINK")) {
                    return true;
                }
                if (VerbMatchesToken(a_parsed.verb, "ACTIVATE")) {
                    std::string rName;
                    if (TryReadTextField(clips.rollover, "RolloverName_tf"sv, rName) &&
                        TargetInfo::ToUpperAscii(TrimWhitespace(rName)) == "WATER") {
                        return true;
                    }
                    std::vector<ButtonHintEntry> rHints;
                    bool rHasPri = false;
                    ReadButtonHintBar(clips.rollover, rHints, rHasPri);
                    for (std::size_t hi = 1; hi < rHints.size(); ++hi) {
                        if (VerbMatchesToken(rHints[hi].label, "FILL")) {
                            return true;
                        }
                    }
                }
                return false;
            };

            if (in.enabled && !bypassActive && !in.quickContainerVisible && in.rolloverVisible &&
                !in.suppressTarget && cam) {
                if (targetRef) {
                    in.hasTarget = true;
                    RE::NiPoint3 worldAnchor =
                        ResolveStableAnchorWorldPosition(targetRef, targeted3DHint, cfg);
                    if (player) {
                        const auto* tBase = targetRef->GetObjectReference();
                        const bool isWatrForm = tBase && (tBase->GetFormType() == RE::ENUM_FORM_ID::kWATR);
                        const bool hasWaterType = tBase && (tBase->GetWaterType() != nullptr);
                        const float distFromPlayer = player->GetPosition().GetDistance(worldAnchor);
                        if (isWatrForm || hasWaterType ||
                            (tBase && tBase->GetFormType() == RE::ENUM_FORM_ID::kACTI && distFromPlayer > 160.0f)) {
                            const auto parsed = !usingTpAssist ? readRolloverPrimaryParsed() : TargetInfo::ActionParts{};
                            const bool isDrinkVerb = TargetInfo::ToUpperAscii(parsed.verb).rfind("DRINK", 0) == 0;
                            if (isWatrForm || hasWaterType || isDrinkVerb) {
                                isWaterRollover = true;
                                if (isWatrForm || distFromPlayer > 160.0f) {
                                    const float refZ = targetRef->GetPosition().z;
                                    const float pZ = player->GetPosition().z;
                                    const float waterZ = (refZ >= pZ - 45.0f && refZ <= pZ + 90.0f) ? refZ : (pZ + 8.0f);
                                    worldAnchor = computePlayerWaterAnchor(waterZ);
                                    isWaterSurfaceAnchor = true;
                                }
                            }
                        }
                    }
                    in.projectedTarget = ProjectWorldPoint(
                        worldAnchor, *cam, visRect,
                        cfg.enableClamping, cfg.edgeMargin,
                        0.0f, 0.0f, 0.0f, 0.0f);
                } else if (!usingTpAssist && modernRolloverActive && player) {
                    const auto parsed = readRolloverPrimaryParsed();
                    if (isTerrainWaterRollover(parsed)) {
                        in.hasTarget = true;
                        const RE::NiPoint3 worldAnchor = computePlayerWaterAnchor(player->GetPosition().z + 8.0f);
                        in.projectedTarget = ProjectWorldPoint(
                            worldAnchor, *cam, visRect,
                            cfg.enableClamping, cfg.edgeMargin,
                            0.0f, 0.0f, 0.0f, 0.0f);
                        isWaterRollover = true;
                        isWaterSurfaceAnchor = true;
                        terrainWaterKey = parsed.keyLabel.empty() ? std::string("E") : parsed.keyLabel;
                        terrainWaterVerb = parsed.verb;
                    }
                }
            }

            const auto out = _state.Step(in);
            if (ApplyTransformToClip(movie, clips.rolloverParent, clips.rollover, out)) {
                changed = true;
            } else if ((modernRolloverActive || in.suppressTarget || usingTpAssist) && clips.rolloverRawVisible &&
                       out.action == HUDAction::kNoOp && _state.IsHidingAlpha()) {
                clips.rollover.SetMember("alpha"sv, RE::Scaleform::GFx::Value(0.0));
                changed = true;
            }

            rolloverFloatingVisible =
                (!in.suppressTarget &&
                 out.action == HUDAction::kApplyFloat && in.projectedTarget.visible &&
                 !clips.quickContainerVisible && (targetRef || isWaterSurfaceAnchor) && stageW > 0.0f && stageH > 0.0f);

            if (rolloverFloatingVisible) {
                const std::int64_t nowMs = SteadyNowMs();
                const std::uint32_t formId = targetRef ? targetRef->GetFormID() : 0u;
                std::uintptr_t targetKey = 0;
                if (targetRef) {
                    targetKey = (formId != 0)
                        ? static_cast<std::uintptr_t>(formId)
                        : reinterpret_cast<std::uintptr_t>(targetRef);
                    nextOverlay.target = ResolveTargetMetadata(targetRef);
                } else {
                    targetKey = static_cast<std::uintptr_t>(0x5741544552ull);
                    nextOverlay.target = {};
                    nextOverlay.target.displayName = "WATER";
                    nextOverlay.target.rawDisplayName = "WATER";
                    nextOverlay.target.actionVerb = terrainWaterVerb.empty() ? std::string("DRINK") : terrainWaterVerb;
                    nextOverlay.target.itemKind = TargetInfo::ItemKind::None;
                }

                nextOverlay.visible = true;
                nextOverlay.quickContainerVisible = false;
                nextOverlay.normScreenX = std::clamp((out.x - visRect.x1) / stageW, 0.0f, 1.0f);
                nextOverlay.normScreenY = std::clamp((out.y - visRect.y1) / stageH, 0.0f, 1.0f);
                nextOverlay.edgeFade = in.projectedTarget.alpha;
                nextOverlay.updateTimeMs = nowMs;

                std::vector<ButtonHintEntry> hints;
                bool hasPrimaryHintSource = false;
                if (!usingTpAssist) {
                    ReadButtonHintBar(clips.rollover, hints, hasPrimaryHintSource);
                    if (!hasPrimaryHintSource) {
                        const auto parsed = readRolloverPrimaryParsed();
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
                    nextOverlay.actions = std::move(hints);
                }
                if (nextOverlay.actions.empty()) {
                    if (!nextOverlay.target.actionVerb.empty() && nextOverlay.target.actionVerb.front() == '$') {
                        nextOverlay.target.actionVerb = TargetInfo::ToUpperAscii(TranslateScaleformKey(nextOverlay.target.actionVerb));
                    }
                    if (!nextOverlay.target.actionVerb.empty()) {
                        nextOverlay.actions.push_back({
                            terrainWaterKey.empty() ? std::string("E") : terrainWaterKey,
                            nextOverlay.target.actionVerb,
                            true
                        });
                    }
                }

                // Per-action non-companion NPC distance filtering (keeps ranged actions up to npcRangedMaxDistance)
                const bool npcFilteredOut = FilterNpcPromptActions(
                    nextOverlay.actions,
                    targetIsLiveNonCompanionNpc,
                    targetDist,
                    cfg.limitNpcPromptDistance,
                    cfg.npcMaxDistance,
                    cfg.npcKeepRangedActions,
                    cfg.npcRangedMaxDistance);

                if (npcFilteredOut) {
                    nextOverlay.visible = false;
                    rolloverFloatingVisible = false;
                    if (clips.rolloverRawVisible) {
                        clips.rollover.SetMember("alpha"sv, RE::Scaleform::GFx::Value(0.0));
                        changed = true;
                    }
                } else {
                    std::int64_t firstSeen = _hoverFirstSeenMs.load(std::memory_order_relaxed);
                    if (targetKey != _hoverTargetKey.load(std::memory_order_relaxed) || firstSeen == 0) {
                        _hoverTargetKey.store(targetKey, std::memory_order_relaxed);
                        firstSeen = nowMs;
                        _hoverFirstSeenMs.store(firstSeen, std::memory_order_relaxed);
                    }
                    nextOverlay.targetFirstSeenMs = firstSeen;

                    if (!nextOverlay.actions.empty()) {
                        nextOverlay.actionKey = nextOverlay.actions[0].key;
                        nextOverlay.actionVerb = nextOverlay.actions[0].label;
                        if (nextOverlay.actions.size() >= 2) {
                            nextOverlay.secondaryKey = nextOverlay.actions[1].key;
                            nextOverlay.secondaryVerb = nextOverlay.actions[1].label;
                        }
                    }

                    std::string rolloverName;
                    const bool hasRolloverName = !usingTpAssist && TryReadTextField(clips.rollover, "RolloverName_tf"sv, rolloverName);
                    if (nextOverlay.target.isLoadDoor) {
                        nextOverlay.target.doorDestination = TargetInfo::ResolveLoadDoorDestination(
                            hasRolloverName ? rolloverName : std::string_view{},
                            nextOverlay.target.displayName,
                            nextOverlay.target.doorDestination,
                            {});
                    }

                    if (nextOverlay.target.displayName.empty() && hasRolloverName) {
                        nextOverlay.target.displayName = std::move(rolloverName);
                    }
                    if (nextOverlay.target.rawDisplayName.empty() && !nextOverlay.target.displayName.empty()) {
                        nextOverlay.target.rawDisplayName = nextOverlay.target.displayName;
                    }

                    bool rolloverCrime = (targetRef && !targetRef->IsDeleted() && targetRef->IsCrimeToActivate());
                    if (targetIsLiveNonCompanionNpc && cfg.limitNpcPromptDistance && targetDist > cfg.npcMaxDistance) {
                        // Recompute crime strictly from surviving actions when close-range Pickpocket was filtered out
                        rolloverCrime = false;
                    }
                    if (!rolloverCrime && IsCrimeActionVerb(nextOverlay.actionVerb)) {
                        rolloverCrime = true;
                    }
                    if (!rolloverCrime) {
                        for (const auto& act : nextOverlay.actions) {
                            if (IsCrimeActionVerb(act.label)) {
                                rolloverCrime = true;
                                break;
                            }
                        }
                    }
                    nextOverlay.isCrime = rolloverCrime;

                    // Hold-to-Activate tagging on surviving prompt actions
                    holdTargetKeyNext = (formId != 0)
                        ? static_cast<std::uint64_t>(formId)
                        : (isWaterRollover ? 0x5741544552ull : reinterpret_cast<std::uint64_t>(targetRef));
                    const std::string upperWaterDisp =
                        TargetInfo::ToUpperAscii(TrimWhitespace(nextOverlay.target.displayName));
                    bool hasFillSecondary = false;
                    for (std::size_t ai = 1; ai < nextOverlay.actions.size(); ++ai) {
                        if (VerbMatchesToken(nextOverlay.actions[ai].label, "FILL")) {
                            hasFillSecondary = true;
                            break;
                        }
                    }
                    const bool isWaterTarget = isWaterRollover || (upperWaterDisp == "WATER") || hasFillSecondary;
                    for (std::size_t ai = 0; ai < nextOverlay.actions.size(); ++ai) {
                        if (ai > 1) {
                            break;
                        }
                        auto& act = nextOverlay.actions[ai];
                        act.holdSlot = static_cast<std::uint8_t>(ai);
                        const bool actCrime = IsCrimeActionVerb(act.label) ||
                                              (rolloverCrime && ai == 0 && !IsRangedNpcActionVerb(act.label));
                        act.requiresHold = DoesActionRequireHold(
                            act.label,
                            actCrime,
                            nextOverlay.target.itemKind,
                            nextOverlay.target.isLoadDoor,
                            cfg,
                            isWaterTarget && (ai == 0));
                        if (act.requiresHold) {
                            if (ai == 0) {
                                holdPriReqNext = true;
                                if (isWaterTarget || targetRef == nullptr) {
                                    holdUseSyntheticTapNext = true;
                                }
                                act.holdProgress = GetLiveHoldProgress(0);
                            } else if (ai == 1) {
                                holdSecReqNext = true;
                                act.holdProgress = GetLiveHoldProgress(1);
                            }
                        }
                    }
                }
            }

            // World water / terrain-only rollover fallback for Vanilla style
            if (!rolloverFloatingVisible &&
                !clips.quickContainerVisible &&
                !bypassActive &&
                cfg.enabled &&
                !aimSuppressRollover &&
                clips.rolloverRawVisible &&
                targetRef == nullptr) {
                const auto parsed = readRolloverPrimaryParsed();
                const bool isTerrainWater = isTerrainWaterRollover(parsed);
                if (!parsed.verb.empty() &&
                    DoesActionRequireHold(parsed.verb, false, TargetInfo::ItemKind::None, false, cfg, isTerrainWater)) {
                    holdPriReqNext = true;
                    holdUseSyntheticTapNext = true;
                    holdTargetKeyNext = 0x5741544552ull; // "WATER"
                }
            }

            if (!rolloverFloatingVisible || clips.quickContainerVisible || bypassActive || !cfg.enabled) {
                _hoverTargetKey.store(0, std::memory_order_relaxed);
                _hoverFirstSeenMs.store(0, std::memory_order_relaxed);
            }
        }

        // 2. Floating & Modern QuickContainer / Workbench controller
        if (clips.hasQuickContainer) {
            HUDFrameInput qcIn{};
            qcIn.enabled = cfg.enabled && (cfg.floatQuickContainer || modernQcActive);
            qcIn.inPowerArmor = inPowerArmor;
            qcIn.powerArmorBypass = cfg.powerArmorBypass;
            qcIn.quickContainerVisible = false;
            qcIn.rolloverVisible = clips.quickContainerVisible;
            qcIn.nativeClipVisible = clips.quickContainerRawVisible;
            qcIn.smoothingSpeed = cfg.quickContainerSmoothing;
            qcIn.deadzonePx = cfg.quickContainerDeadzone;
            qcIn.hideNativeClip = modernQcActive;
            qcIn.suppressTarget = companionDecision.suppressTarget || aimSuppressQc || wallBlockedPrompt;
            qcIn.deltaSeconds = a_timeDelta;
            ReadClipBaseTransform(clips.quickContainer, qcIn);

            if (qcIn.enabled && !bypassActive && qcIn.rolloverVisible && !qcIn.suppressTarget) {
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
            } else if ((modernQcActive || qcIn.suppressTarget) && clips.quickContainerRawVisible &&
                       qcOut.action == HUDAction::kNoOp && _qcState.IsHidingAlpha()) {
                clips.quickContainer.SetMember("alpha"sv, RE::Scaleform::GFx::Value(0.0));
                changed = true;
            }

            const bool publishFloatQc =
                (qcOut.action == HUDAction::kApplyFloat && qcIn.projectedTarget.visible);
            const bool publishGraceQc =
                (qcOut.action == HUDAction::kNoOp && _qcState.IsHidingAlpha() && _qcState.HasLastScreen());

            if (modernQcActive && clips.quickContainerVisible && !qcIn.suppressTarget &&
                (publishFloatQc || publishGraceQc) && stageW > 0.0f && stageH > 0.0f) {
                EngineQuickContainerSnapshot engineSnap;
                if (hasPreEngineQc) {
                    engineSnap = std::move(preEngineQc);
                } else {
                    std::string sfHeader;
                    std::string sfFirstItem;
                    TryReadScaleformQuickContainerIdentity(clips.quickContainer, sfHeader, sfFirstItem);
                    TryReadEngineQuickContainerState(
                        engineSnap,
                        targetRef,
                        cfg.floatQuickContainer,
                        sfHeader,
                        sfFirstItem,
                        true);
                }

                const TargetInfo::TargetMetadata targetMeta =
                    targetRef ? ResolveTargetMetadata(targetRef) : TargetInfo::TargetMetadata{};

                auto invPtr = engineSnap.valid ? engineSnap.inventoryRef.get() : RE::NiPointer<RE::TESObjectREFR>{};
                auto contPtr = engineSnap.valid ? engineSnap.containerRef.get() : RE::NiPointer<RE::TESObjectREFR>{};
                RE::TESObjectREFR* invOwner = invPtr ? invPtr.get() : (contPtr ? contPtr.get() : targetRef);
                const std::uint32_t ownerFormID = invOwner ? invOwner->GetFormID() : 0u;

                const bool isPowerArmorMode =
                    engineSnap.valid &&
                    (engineSnap.mode == RE::QuickContainerMode::kPowerArmor ||
                     engineSnap.mode == RE::QuickContainerMode::kStealingPowerArmor);
                const bool isCraftingOrWorkshopMode =
                    engineSnap.valid &&
                    (engineSnap.mode == RE::QuickContainerMode::kCrafting ||
                     engineSnap.mode == RE::QuickContainerMode::kWorkshop);
                const bool isWorkbench = IsQuickContainerWorkbenchTarget(
                    targetMeta.itemKind,
                    engineSnap.valid,
                    isPowerArmorMode,
                    isCraftingOrWorkshopMode);
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
                const bool targetIsCrime =
                    (targetRef && !targetRef->IsDeleted() && targetRef->IsCrimeToActivate());
                const bool modeEligibleForCustom =
                    engineSnap.valid &&
                    (engineSnap.mode == RE::QuickContainerMode::kLoot ||
                     engineSnap.mode == RE::QuickContainerMode::kTurret ||
                     engineSnap.mode == RE::QuickContainerMode::kTeammate ||
                     engineSnap.mode == RE::QuickContainerMode::kPowerArmor);
                const bool wantCustomQuickLoot =
                    modernQcActive &&
                    cfg.qcSortMode != 6 &&
                    modeEligibleForCustom &&
                    !targetIsCrime &&
                    !isWorkbench &&
                    !isLockedOrEmpty &&
                    invOwner != nullptr;

                const bool shouldCollectInv =
                    !isWorkbench &&
                    !isLockedOrEmpty &&
                    invOwner != nullptr &&
                    (wantCustomQuickLoot ||
                     cfg.qcShowItemIcons ||
                     cfg.itemConditionDisplay != 0 ||
                     (cfg.qcExpandInventoryList &&
                      (!engineSnap.valid || engineHasEllipsis || engineSnap.items.size() >= 5)));
                const CachedInventorySnapshot* invSnapPtr = shouldCollectInv
                    ? &CollectQuickContainerInventory(invOwner, skipEquipped)
                    : &kEmptyInvSnap;

                if (wantCustomQuickLoot && !invSnapPtr->truncatedByCap && !invSnapPtr->items.empty()) {
                    if (ownerFormID == 0 || ownerFormID != _customQcOwnerFormID) {
                        _customQcOwnerFormID = ownerFormID;
                        _customQcSelectedIndex = 0;
                        (void)_customQcPendingScrollDelta.exchange(0, std::memory_order_acq_rel);
                        (void)_customQcTakeRequested.exchange(false, std::memory_order_acq_rel);
                    }

                    const auto sortCfg = QuickLootSortConfig::FromConfig(cfg);
                    _customQcSortedItems = invSnapPtr->items;
                    SortQuickContainerItems(_customQcSortedItems, sortCfg);

                    const std::int32_t scrollDelta =
                        _customQcPendingScrollDelta.exchange(0, std::memory_order_acq_rel);
                    _customQcSelectedIndex = StepCustomQuickLootSelection(
                        _customQcSelectedIndex,
                        scrollDelta,
                        static_cast<std::int32_t>(_customQcSortedItems.size()));

                    if (_customQcTakeRequested.exchange(false, std::memory_order_acq_rel) &&
                        player &&
                        !engineSnap.isLocked &&
                        !targetIsCrime &&
                        engineSnap.mode != RE::QuickContainerMode::kPowerArmor &&
                        engineSnap.mode != RE::QuickContainerMode::kStealingPowerArmor &&
                        !_customQcSortedItems.empty()) {
                        const QuickContainerItemEntry targetEntry =
                            _customQcSortedItems[static_cast<std::size_t>(_customQcSelectedIndex)];
                        if (ExecuteCustomQuickLootTake(invOwner, player, engineSnap.mode, targetEntry)) {
                            const std::int64_t takenAtMs = SteadyNowMs();
                            _lastQuickLootTakeMs.store(takenAtMs, std::memory_order_release);
                            _cachedInvSnapshot.ownerFormID = 0;
                            _cachedInvSnapshot.timestampMs = 0;
                            invSnapPtr = &CollectQuickContainerInventory(invOwner, skipEquipped);
                            _customQcSortedItems = invSnapPtr->items;
                            SortQuickContainerItems(_customQcSortedItems, sortCfg);
                            _customQcSelectedIndex = StepCustomQuickLootSelection(
                                _customQcSelectedIndex,
                                0,
                                static_cast<std::int32_t>(_customQcSortedItems.size()));
                            if (_customQcSortedItems.empty()) {
                                _lastQuickLootEmptyMs.store(takenAtMs, std::memory_order_release);
                            }
                        }
                    }

                    ReadQuickContainerHeader(
                        clips.quickContainer,
                        targetMeta.displayName,
                        engineSnap,
                        nextOverlay.quickContainer);
                    ReadQuickContainerButtons(
                        clips.quickContainer,
                        engineSnap,
                        nextOverlay.quickContainer);

                    const int totalCount = static_cast<int>(_customQcSortedItems.size());
                    if (totalCount > 0) {
                        const int maxRows = cfg.qcExpandInventoryList
                            ? std::clamp(cfg.qcMaxVisibleRows, 3, 12)
                            : 5;
                        const auto win = OverlayLayout::ComputeListWindow(
                            totalCount,
                            _customQcSelectedIndex,
                            maxRows);
                        _customQcSelectedIndex = win.clampedSelected;
                        nextOverlay.quickContainer.totalItemCount = totalCount;
                        nextOverlay.quickContainer.windowStartIndex = win.start;
                        nextOverlay.quickContainer.selectedIndex = win.clampedSelected;
                        nextOverlay.quickContainer.hasMoreAbove = (win.start > 0);
                        nextOverlay.quickContainer.hasMoreBelow = (win.end < totalCount);
                        nextOverlay.quickContainer.hasExactTotal = true;
                        nextOverlay.quickContainer.items.reserve(
                            static_cast<std::size_t>(std::max(0, win.end - win.start)));
                        for (int i = win.start; i < win.end; ++i) {
                            nextOverlay.quickContainer.items.push_back(
                                _customQcSortedItems[static_cast<std::size_t>(i)]);
                        }
                        _customQcTakeEnabled.store(
                            !engineSnap.isLocked &&
                            !targetIsCrime &&
                            engineSnap.mode != RE::QuickContainerMode::kPowerArmor &&
                            engineSnap.mode != RE::QuickContainerMode::kStealingPowerArmor,
                            std::memory_order_release);
                        _customQcActive.store(true, std::memory_order_release);
                    } else {
                        _customQcActive.store(false, std::memory_order_release);
                        _customQcTakeEnabled.store(false, std::memory_order_release);
                        _customQcSelectedIndex = 0;
                        nextOverlay.quickContainer.totalItemCount = 0;
                        nextOverlay.quickContainer.windowStartIndex = 0;
                        nextOverlay.quickContainer.selectedIndex = 0;
                        nextOverlay.quickContainer.hasMoreAbove = false;
                        nextOverlay.quickContainer.hasMoreBelow = false;
                        nextOverlay.quickContainer.hasExactTotal = true;
                    }
                    _lastQcSelectedIndex = nextOverlay.quickContainer.selectedIndex;
                } else {
                    _customQcActive.store(false, std::memory_order_release);
                    _customQcTakeEnabled.store(false, std::memory_order_release);
                    _customQcPendingScrollDelta.store(0, std::memory_order_release);
                    _customQcTakeRequested.store(false, std::memory_order_release);
                    _customQcOwnerFormID = 0;

                    ReadQuickContainerData(
                        *this,
                        ownerFormID,
                        clips.quickContainer,
                        targetMeta.displayName,
                        engineSnap,
                        *invSnapPtr,
                        cfg.qcMaxVisibleRows,
                        cfg.qcExpandInventoryList,
                        _lastQcSelectedIndex,
                        nextOverlay.quickContainer);
                    _lastQcSelectedIndex = nextOverlay.quickContainer.selectedIndex;
                }

                const float screenX = publishFloatQc ? qcOut.x : _qcState.LastScreen().x;
                const float screenY = publishFloatQc ? qcOut.y : _qcState.LastScreen().y;
                nextOverlay.quickContainer.active = true;
                nextOverlay.quickContainer.docked = !cfg.floatQuickContainer;
                nextOverlay.quickContainer.isWorkbench = isWorkbench;

                bool qcCrime =
                    engineSnap.valid &&
                    (engineSnap.mode == RE::QuickContainerMode::kStealing ||
                     engineSnap.mode == RE::QuickContainerMode::kStealingPowerArmor);
                if (!qcCrime && targetRef && !targetRef->IsDeleted()) {
                    qcCrime = targetRef->IsCrimeToActivate();
                }
                if (!qcCrime) {
                    for (const auto& btn : nextOverlay.quickContainer.buttons) {
                        if (IsCrimeActionVerb(btn.label)) {
                            qcCrime = true;
                            break;
                        }
                    }
                }
                nextOverlay.quickContainer.isCrime = qcCrime;

                // Hold-to-Activate tagging on QuickContainer buttons (Workbenches, Power Armor, Steal Containers)
                if (nextOverlay.quickContainer.buttons.empty()) {
                    if (!nextOverlay.quickContainer.items.empty()) {
                        nextOverlay.quickContainer.buttons.push_back({ "E", "TAKE", true });
                        nextOverlay.quickContainer.buttons.push_back({ "R", "TRANSFER", true });
                    } else if (isWorkbench) {
                        nextOverlay.quickContainer.buttons.push_back({ "E", "CRAFT", true });
                        nextOverlay.quickContainer.buttons.push_back({ "R", "TRANSFER", true });
                    } else {
                        nextOverlay.quickContainer.buttons.push_back({ "R", "TRANSFER", true });
                    }
                }
                const TargetInfo::ItemKind qcHoldKind =
                    (isWorkbench || isPowerArmorMode) ? TargetInfo::ItemKind::Furniture : targetMeta.itemKind;
                const std::uint64_t qcHoldKey = (ownerFormID != 0)
                    ? static_cast<std::uint64_t>(ownerFormID)
                    : (targetRef ? static_cast<std::uint64_t>(targetRef->GetFormID()) : 0x51435549ull);
                const bool hasEnginePrimaryBtn = engineSnap.valid
                    ? !engineSnap.aButtonText.empty()
                    : (!nextOverlay.quickContainer.items.empty() || isWorkbench || nextOverlay.quickContainer.buttons.size() >= 2);
                for (std::size_t bi = 0; bi < nextOverlay.quickContainer.buttons.size(); ++bi) {
                    if (bi > 1) {
                        break;
                    }
                    auto& qBtn = nextOverlay.quickContainer.buttons[bi];
                    const std::string upperKey = TargetInfo::ToUpperAscii(qBtn.key);
                    const bool isSecondarySlot =
                        (bi == 1) ||
                        (!hasEnginePrimaryBtn && bi == 0) ||
                        upperKey == "R" ||
                        upperKey == "X" ||
                        upperKey == "SQUARE";
                    const std::uint8_t holdSlotIdx = isSecondarySlot ? 1u : 0u;
                    qBtn.holdSlot = holdSlotIdx;
                    const bool btnCrime = IsCrimeActionVerb(qBtn.label) || (qcCrime && !isSecondarySlot && !isWorkbench);
                    qBtn.requiresHold = DoesActionRequireHold(qBtn.label, btnCrime, qcHoldKind, false, cfg);
                    if (qBtn.requiresHold) {
                        holdInQcNext = true;
                        holdTargetKeyNext = qcHoldKey;
                        if (holdSlotIdx == 0u) {
                            holdPriReqNext = true;
                            holdUseSyntheticTapNext = true;
                            qBtn.holdProgress = GetLiveHoldProgress(0);
                        } else {
                            holdSecReqNext = true;
                            qBtn.holdProgress = GetLiveHoldProgress(1);
                        }
                    }
                }

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
                InvalidateQuickContainerCacheInternal();
            }
        } else {
            InvalidateQuickContainerCacheInternal();
        }

        _holdPrimaryRequired.store(holdPriReqNext, std::memory_order_release);
        _holdSecondaryRequired.store(holdSecReqNext, std::memory_order_release);
        _holdPrimaryUseSyntheticTap.store(holdUseSyntheticTapNext, std::memory_order_release);
        _holdInQuickContainer.store(holdInQcNext, std::memory_order_release);
        _holdTargetKey.store(holdTargetKeyNext, std::memory_order_release);
        if (!holdPriReqNext) {
            _holdPrimaryProgress.store(0.0f, std::memory_order_release);
        }
        if (!holdSecReqNext) {
            _holdSecondaryProgress.store(0.0f, std::memory_order_release);
        }

        const bool inCommandMode = (player && static_cast<bool>(player->actorDoingPlayerCommand));
        bool inWorkshopMenu = false;
        if (auto* ui = RE::UI::GetSingleton()) {
            inWorkshopMenu = ui->GetMenuOpen("WorkshopMenu"sv);
        }
        const bool effectiveModernRollover = modernRolloverActive && cfg.enabled && !bypassActive;
        const bool anyRolloverPromptVisible =
            !companionDecision.suppressTarget && !aimSuppressRollover &&
            (effectiveModernRollover ? rolloverFloatingVisible : (clips.hasRollover && clips.rolloverRawVisible));
        const bool anyPromptVisible =
            anyRolloverPromptVisible || (clips.quickContainerVisible && !companionDecision.suppressTarget && !aimSuppressQc);
        const bool blockOrphanSecondary =
            cfg.enabled &&
            !bypassActive &&
            cfg.limitCompanionPromptDistance &&
            cfg.blockHiddenCompanionInteractions &&
            !inCommandMode &&
            !inWorkshopMenu &&
            !anyPromptVisible;
        _blockOrphanSecondaryActivate.store(blockOrphanSecondary, std::memory_order_release);

        {
            std::lock_guard lock{ _overlayMutex };
            _overlaySnapshot = std::move(nextOverlay);
        }

        return changed;
    }
#endif
}

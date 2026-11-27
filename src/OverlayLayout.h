#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "TargetInfo.h"

namespace OverlayLayout
{
    inline constexpr float kDefaultGapPx = 12.0f;

    struct Point2D
    {
        float x{ 0.0f };
        float y{ 0.0f };
    };

    struct CardRect
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float w{ 0.0f };
        float h{ 0.0f };
    };

    [[nodiscard]] CardRect ComputeInfoCardRect(
        float a_anchorX,
        float a_anchorY,
        float a_promptHalfW,
        float a_promptHalfH,
        float a_cardW,
        float a_cardH,
        float a_screenW,
        float a_screenH,
        float a_margin,
        int a_positionMode,
        float a_offsetX,
        float a_offsetY,
        bool a_enableClamping = true) noexcept;

    [[nodiscard]] Point2D ComputeInlineSubPillPos(
        float a_mainX,
        float a_mainY,
        float a_mainW,
        float a_mainH,
        float a_subW,
        float a_subH,
        float a_gap,
        float a_screenW,
        float a_screenH,
        float a_margin,
        bool a_enableClamping) noexcept;

    struct ListWindow
    {
        int start{ 0 };
        int end{ 0 };
        int clampedSelected{ 0 };
    };

    [[nodiscard]] float ComputeHoverDelayAlpha(
        std::int64_t a_elapsedMs,
        float a_delaySeconds,
        float a_fadeInSeconds = 0.18f) noexcept;

    [[nodiscard]] ListWindow ComputeListWindow(
        int a_total,
        int a_selected,
        int a_maxVisible) noexcept;

    [[nodiscard]] unsigned int GetItemIconCodepoint(TargetInfo::ItemKind a_kind) noexcept;

    [[nodiscard]] std::uint32_t GetItemIconColorRgb(
        TargetInfo::ItemKind a_kind,
        std::uint32_t a_fallbackRgb,
        bool a_colored) noexcept;

    [[nodiscard]] bool IsCategoryIconEnabled(
        TargetInfo::ItemKind a_kind,
        const ConfigData& a_cfg) noexcept;

    struct HoldRingSegment
    {
        float x1{ 0.0f };
        float y1{ 0.0f };
        float x2{ 0.0f };
        float y2{ 0.0f };
    };

    void BuildRoundedRectProgressSegments(
        float a_minX,
        float a_minY,
        float a_maxX,
        float a_maxY,
        float a_radius,
        float a_progress,
        std::vector<HoldRingSegment>& a_out) noexcept;

    void BuildCircleProgressSegments(
        float a_cx,
        float a_cy,
        float a_radius,
        float a_progress,
        int a_totalSegments,
        std::vector<HoldRingSegment>& a_out) noexcept;

    [[nodiscard]] std::string TruncateUtf8Prefix(
        std::string_view a_text,
        std::size_t a_maxBytes);
}

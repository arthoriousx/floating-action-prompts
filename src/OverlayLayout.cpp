#include "OverlayLayout.h"

#include <algorithm>
#include <cmath>

namespace OverlayLayout
{
    CardRect ComputeInfoCardRect(
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
        bool a_enableClamping) noexcept
    {
        const float cardW = std::max(0.0f, a_cardW);
        const float cardH = std::max(0.0f, a_cardH);
        const float margin = std::max(0.0f, a_margin);

        float x = a_anchorX + a_promptHalfW + kDefaultGapPx + a_offsetX;
        float y = a_anchorY - cardH * 0.5f + a_offsetY;

        switch (a_positionMode) {
        case 1: // Below prompt
            x = a_anchorX - cardW * 0.5f + a_offsetX;
            y = a_anchorY + a_promptHalfH + kDefaultGapPx + a_offsetY;
            break;
        case 2: // Above prompt
            x = a_anchorX - cardW * 0.5f + a_offsetX;
            y = a_anchorY - a_promptHalfH - kDefaultGapPx - cardH + a_offsetY;
            break;
        case 3: // Right of prompt
            x = a_anchorX + a_promptHalfW + kDefaultGapPx + a_offsetX;
            y = a_anchorY - cardH * 0.5f + a_offsetY;
            break;
        case 4: // Left of prompt
            x = a_anchorX - a_promptHalfW - kDefaultGapPx - cardW + a_offsetX;
            y = a_anchorY - cardH * 0.5f + a_offsetY;
            break;
        case 5: // Fixed Top-Right
            x = a_screenW - margin - cardW + a_offsetX;
            y = margin + a_offsetY;
            break;
        case 6: // Fixed Bottom-Right
            x = a_screenW - margin - cardW + a_offsetX;
            y = a_screenH - margin - cardH + a_offsetY;
            break;
        default:
            break;
        }

        if (a_enableClamping) {
            const float maxX = std::max(margin, a_screenW - margin - cardW);
            const float maxY = std::max(margin, a_screenH - margin - cardH);
            x = std::clamp(x, margin, maxX);
            y = std::clamp(y, margin, maxY);
        }

        return { x, y, cardW, cardH };
    }

    Point2D ComputeInlineSubPillPos(
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
        bool a_enableClamping) noexcept
    {
        const float centerX = a_mainX + a_mainW * 0.5f;
        float x = centerX - a_subW * 0.5f;
        float y = a_mainY + a_mainH + a_gap;

        if (a_enableClamping) {
            const float margin = std::max(0.0f, a_margin);
            const float maxX = std::max(margin, a_screenW - margin - a_subW);
            x = std::clamp(x, margin, maxX);

            if (y + a_subH > a_screenH - margin && (a_mainY - a_gap - a_subH) >= margin) {
                y = a_mainY - a_gap - a_subH;
            } else {
                const float maxY = std::max(margin, a_screenH - margin - a_subH);
                y = std::clamp(y, margin, maxY);
            }
        }

        return { x, y };
    }

    float ComputeHoverDelayAlpha(
        std::int64_t a_elapsedMs,
        float a_delaySeconds,
        float a_fadeInSeconds) noexcept
    {
        if (!std::isfinite(a_delaySeconds) || a_delaySeconds <= 0.001f) {
            return 1.0f;
        }
        if (a_elapsedMs <= 0) {
            return 0.0f;
        }
        const float elapsedSec = static_cast<float>(a_elapsedMs) / 1000.0f;
        if (elapsedSec <= a_delaySeconds) {
            return 0.0f;
        }
        if (!std::isfinite(a_fadeInSeconds) || a_fadeInSeconds <= 0.001f) {
            return 1.0f;
        }
        return std::clamp((elapsedSec - a_delaySeconds) / a_fadeInSeconds, 0.0f, 1.0f);
    }

    ListWindow ComputeListWindow(int a_total, int a_selected, int a_maxVisible) noexcept
    {
        if (a_total <= 0 || a_maxVisible <= 0) {
            return { 0, 0, 0 };
        }
        const int clampedSel = std::clamp(a_selected, 0, a_total - 1);
        const int maxStart = std::max(0, a_total - a_maxVisible);
        const int start = std::clamp(clampedSel - 2, 0, maxStart);
        const int end = std::min(a_total, start + a_maxVisible);
        return { start, end, clampedSel };
    }

    unsigned int GetItemIconCodepoint(TargetInfo::ItemKind a_kind) noexcept
    {
        switch (a_kind) {
        case TargetInfo::ItemKind::Weapon:
            return 0xF05Bu;
        case TargetInfo::ItemKind::Armor:
            return 0xF3EDu;
        case TargetInfo::ItemKind::Aid:
            return 0xF0FAu;
        case TargetInfo::ItemKind::Ammo:
            return 0xF135u;
        case TargetInfo::ItemKind::MiscJunk:
            return 0xF013u;
        case TargetInfo::ItemKind::Key:
            return 0xF084u;
        case TargetInfo::ItemKind::BookNote:
            return 0xF02Du;
        case TargetInfo::ItemKind::Holotape:
            return 0xF1C7u;
        case TargetInfo::ItemKind::Container:
            return 0xF49Eu;
        case TargetInfo::ItemKind::Door:
            return 0xF52Au;
        case TargetInfo::ItemKind::Furniture:
            return 0xF6C0u;
        case TargetInfo::ItemKind::Terminal:
            return 0xF120u;
        case TargetInfo::ItemKind::Npc:
            return 0xF007u;
        case TargetInfo::ItemKind::None:
        default:
            return 0xF05Au;
        }
    }

    std::uint32_t GetItemIconColorRgb(
        TargetInfo::ItemKind a_kind,
        std::uint32_t a_fallbackRgb,
        bool a_colored) noexcept
    {
        if (!a_colored) {
            return a_fallbackRgb;
        }

        switch (a_kind) {
        case TargetInfo::ItemKind::Weapon:
            return 0xF27D66u;
        case TargetInfo::ItemKind::Armor:
            return 0x6CB6FFu;
        case TargetInfo::ItemKind::Aid:
            return 0x6BE08Au;
        case TargetInfo::ItemKind::Ammo:
            return 0xF5C451u;
        case TargetInfo::ItemKind::MiscJunk:
            return 0xB8C4D0u;
        case TargetInfo::ItemKind::Key:
            return 0xFFD86Bu;
        case TargetInfo::ItemKind::BookNote:
            return 0xD2A8FFu;
        case TargetInfo::ItemKind::Holotape:
            return 0x62D8E6u;
        case TargetInfo::ItemKind::Container:
            return 0xE5B567u;
        case TargetInfo::ItemKind::Door:
            return 0xA8C5E0u;
        case TargetInfo::ItemKind::Furniture:
            return 0xC9A47Eu;
        case TargetInfo::ItemKind::Terminal:
            return 0x5CE6A0u;
        case TargetInfo::ItemKind::Npc:
            return 0xF6D28Bu;
        case TargetInfo::ItemKind::None:
        default:
            return a_fallbackRgb;
        }
    }

    bool IsCategoryIconEnabled(TargetInfo::ItemKind a_kind, const ConfigData& a_cfg) noexcept
    {
        if (!a_cfg.modernShowItemIcon) {
            return false;
        }

        switch (a_kind) {
        case TargetInfo::ItemKind::Weapon:
            return a_cfg.iconWeapon;
        case TargetInfo::ItemKind::Armor:
            return a_cfg.iconArmor;
        case TargetInfo::ItemKind::Aid:
            return a_cfg.iconAid;
        case TargetInfo::ItemKind::Ammo:
            return a_cfg.iconAmmo;
        case TargetInfo::ItemKind::MiscJunk:
            return a_cfg.iconMiscJunk;
        case TargetInfo::ItemKind::Key:
            return a_cfg.iconKey;
        case TargetInfo::ItemKind::BookNote:
            return a_cfg.iconBookNote;
        case TargetInfo::ItemKind::Holotape:
            return a_cfg.iconHolotape;
        case TargetInfo::ItemKind::Container:
            return a_cfg.iconContainer;
        case TargetInfo::ItemKind::Door:
            return a_cfg.iconDoor;
        case TargetInfo::ItemKind::Furniture:
            return a_cfg.iconFurniture;
        case TargetInfo::ItemKind::Terminal:
            return a_cfg.iconTerminal;
        case TargetInfo::ItemKind::Npc:
            return a_cfg.iconNpc;
        case TargetInfo::ItemKind::None:
        default:
            return false;
        }
    }

    void BuildRoundedRectProgressSegments(
        float a_minX,
        float a_minY,
        float a_maxX,
        float a_maxY,
        float a_radius,
        float a_progress,
        std::vector<HoldRingSegment>& a_out) noexcept
    {
        a_out.clear();
        if (!std::isfinite(a_minX) || !std::isfinite(a_minY) ||
            !std::isfinite(a_maxX) || !std::isfinite(a_maxY) ||
            !std::isfinite(a_progress)) {
            return;
        }
        const float progress = std::clamp(a_progress, 0.0f, 1.0f);
        if (progress <= 0.0f) {
            return;
        }
        const float w = a_maxX - a_minX;
        const float h = a_maxY - a_minY;
        if (w <= 0.01f || h <= 0.01f) {
            return;
        }

        const float r = std::clamp(std::isfinite(a_radius) ? a_radius : 0.0f, 0.0f, 0.5f * std::min(w, h));
        const float midX = 0.5f * (a_minX + a_maxX);
        constexpr float kHalfPi = 1.57079632679f;

        std::vector<Point2D> poly;
        poly.reserve(40);
        auto pushPt = [&](float x, float y) {
            if (poly.empty() ||
                std::fabs(poly.back().x - x) > 0.01f ||
                std::fabs(poly.back().y - y) > 0.01f) {
                poly.push_back({ x, y });
            }
        };
        auto pushArc = [&](float cx, float cy, float startAng, float endAng) {
            if (r <= 0.05f) {
                return;
            }
            constexpr int kArcSteps = 5;
            for (int i = 0; i <= kArcSteps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(kArcSteps);
                const float ang = startAng + (endAng - startAng) * t;
                pushPt(cx + std::cos(ang) * r, cy + std::sin(ang) * r);
            }
        };

        // Start at top-center and trace clockwise around rounded rectangle
        pushPt(midX, a_minY);
        pushPt(a_maxX - r, a_minY);
        pushArc(a_maxX - r, a_minY + r, -kHalfPi, 0.0f);
        pushPt(a_maxX, a_maxY - r);
        pushArc(a_maxX - r, a_maxY - r, 0.0f, kHalfPi);
        pushPt(a_minX + r, a_maxY);
        pushArc(a_minX + r, a_maxY - r, kHalfPi, 2.0f * kHalfPi);
        pushPt(a_minX, a_minY + r);
        pushArc(a_minX + r, a_minY + r, 2.0f * kHalfPi, 3.0f * kHalfPi);
        pushPt(midX, a_minY);

        if (poly.size() < 2) {
            return;
        }

        float totalLen = 0.0f;
        for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
            totalLen += std::hypot(poly[i + 1].x - poly[i].x, poly[i + 1].y - poly[i].y);
        }
        if (totalLen <= 0.01f) {
            return;
        }

        float targetLen = totalLen * progress;
        for (std::size_t i = 0; i + 1 < poly.size() && targetLen > 0.001f; ++i) {
            const float dx = poly[i + 1].x - poly[i].x;
            const float dy = poly[i + 1].y - poly[i].y;
            const float segLen = std::hypot(dx, dy);
            if (segLen <= 0.001f) {
                continue;
            }
            if (targetLen >= segLen) {
                a_out.push_back({ poly[i].x, poly[i].y, poly[i + 1].x, poly[i + 1].y });
                targetLen -= segLen;
            } else {
                const float frac = targetLen / segLen;
                a_out.push_back({
                    poly[i].x,
                    poly[i].y,
                    poly[i].x + dx * frac,
                    poly[i].y + dy * frac
                });
                targetLen = 0.0f;
                break;
            }
        }
    }

    void BuildCircleProgressSegments(
        float a_cx,
        float a_cy,
        float a_radius,
        float a_progress,
        int a_totalSegments,
        std::vector<HoldRingSegment>& a_out) noexcept
    {
        a_out.clear();
        if (!std::isfinite(a_cx) || !std::isfinite(a_cy) ||
            !std::isfinite(a_radius) || !std::isfinite(a_progress) ||
            a_radius <= 0.01f) {
            return;
        }
        const float progress = std::clamp(a_progress, 0.0f, 1.0f);
        if (progress <= 0.0f) {
            return;
        }

        const int segs = std::clamp(a_totalSegments, 8, 64);
        const float startAng = -1.57079632679f;
        const float sweep = 6.28318530718f * progress;
        const int activeSegs = std::max(1, static_cast<int>(std::ceil(static_cast<float>(segs) * progress)));

        for (int i = 0; i < activeSegs; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(activeSegs);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(activeSegs);
            const float ang0 = startAng + sweep * t0;
            const float ang1 = startAng + sweep * t1;
            a_out.push_back({
                a_cx + std::cos(ang0) * a_radius,
                a_cy + std::sin(ang0) * a_radius,
                a_cx + std::cos(ang1) * a_radius,
                a_cy + std::sin(ang1) * a_radius
            });
        }
    }

    std::string TruncateUtf8Prefix(std::string_view a_text, std::size_t a_maxBytes)
    {
        if (a_maxBytes >= a_text.size()) {
            return std::string(a_text);
        }
        std::size_t cut = a_maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(a_text[cut]) & 0xC0u) == 0x80u) {
            --cut;
        }
        return std::string(a_text.substr(0, cut));
    }
}

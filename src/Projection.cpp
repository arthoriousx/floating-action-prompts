#include "Projection.h"

#include <algorithm>
#include <cmath>

namespace Projection
{
    namespace
    {
        [[nodiscard]] ScreenCoord ClampAndFadeBoxStagePos(
            float a_sx,
            float a_sy,
            float a_stageOriginX,
            float a_stageOriginY,
            float a_stageWidth,
            float a_stageHeight,
            float a_boxWidth,
            float a_boxHeight,
            float a_pivotNormX,
            float a_pivotNormY,
            bool a_enableClamping,
            float a_edgeMargin)
        {
            if (a_stageWidth <= 0.0f || a_stageHeight <= 0.0f) {
                return {};
            }

            const float maxAllowedMargin = std::min(a_stageWidth, a_stageHeight) * 0.45f;
            const float margin = std::clamp(a_edgeMargin, 0.0f, maxAllowedMargin);

            const float boxW = std::max(0.0f, a_boxWidth);
            const float boxH = std::max(0.0f, a_boxHeight);
            const float pivX = std::clamp(a_pivotNormX, 0.0f, 1.0f);
            const float pivY = std::clamp(a_pivotNormY, 0.0f, 1.0f);

            const float screenMinX = a_stageOriginX + margin;
            const float screenMaxX = a_stageOriginX + a_stageWidth - margin;
            const float screenMinY = a_stageOriginY + margin;
            const float screenMaxY = a_stageOriginY + a_stageHeight - margin;

            float minX = screenMinX + pivX * boxW;
            float maxX = screenMaxX - (1.0f - pivX) * boxW;
            if (minX > maxX) {
                const float midX = 0.5f * (minX + maxX);
                minX = maxX = midX;
            }

            float minY = screenMinY + pivY * boxH;
            float maxY = screenMaxY - (1.0f - pivY) * boxH;
            if (minY > maxY) {
                const float midY = 0.5f * (minY + maxY);
                minY = maxY = midY;
            }

            if (a_enableClamping) {
                // Measure fade against the viewport safe margins (not the inset box anchor),
                // so a large panel near the bottom-right corner clamps cleanly without fading out.
                float dx = 0.0f;
                if (a_sx < screenMinX) dx = screenMinX - a_sx;
                else if (a_sx > screenMaxX) dx = a_sx - screenMaxX;

                float dy = 0.0f;
                if (a_sy < screenMinY) dy = screenMinY - a_sy;
                else if (a_sy > screenMaxY) dy = a_sy - screenMaxY;

                const float dist = std::hypot(dx, dy);
                const float clampedX = std::clamp(a_sx, minX, maxX);
                const float clampedY = std::clamp(a_sy, minY, maxY);

                const float fadeRange = std::max(margin, 20.0f);
                const float alpha = std::clamp(1.0f - (dist / (fadeRange * 2.0f)), 0.0f, 1.0f);
                if (alpha <= 0.001f) {
                    return { clampedX, clampedY, 0.0f, false };
                }
                return { clampedX, clampedY, alpha, true };
            }

            if (a_sx < a_stageOriginX || a_sx > a_stageOriginX + a_stageWidth ||
                a_sy < a_stageOriginY || a_sy > a_stageOriginY + a_stageHeight) {
                return { a_sx, a_sy, 0.0f, false };
            }

            return { a_sx, a_sy, 1.0f, true };
        }

        [[nodiscard]] ScreenCoord ClampAndFadeStagePos(
            float a_sx,
            float a_sy,
            float a_stageOriginX,
            float a_stageOriginY,
            float a_stageWidth,
            float a_stageHeight,
            bool a_enableClamping,
            float a_edgeMargin)
        {
            return ClampAndFadeBoxStagePos(
                a_sx, a_sy,
                a_stageOriginX, a_stageOriginY, a_stageWidth, a_stageHeight,
                0.0f, 0.0f, 0.0f, 0.0f,
                a_enableClamping, a_edgeMargin);
        }
    }

    ScreenCoord ClampAndFadeBoxViewportPos(
        float a_normX,
        float a_normY,
        float a_portLeft,
        float a_portRight,
        float a_portBottom,
        float a_portTop,
        float a_stageOriginX,
        float a_stageOriginY,
        float a_stageWidth,
        float a_stageHeight,
        float a_boxWidth,
        float a_boxHeight,
        float a_pivotNormX,
        float a_pivotNormY,
        bool a_enableClamping,
        float a_edgeMargin)
    {
        if (a_stageWidth <= 0.0f || a_stageHeight <= 0.0f) {
            return {};
        }

        const float portW = a_portRight - a_portLeft;
        const float portH = a_portTop - a_portBottom;
        if (std::fabs(portW) <= 0.0001f || std::fabs(portH) <= 0.0001f) {
            return {};
        }

        const float u = (a_normX - a_portLeft) / portW;
        const float vTopDown = 1.0f - ((a_normY - a_portBottom) / portH);

        const float sx = a_stageOriginX + u * a_stageWidth;
        const float sy = a_stageOriginY + vTopDown * a_stageHeight;

        return ClampAndFadeBoxStagePos(
            sx,
            sy,
            a_stageOriginX,
            a_stageOriginY,
            a_stageWidth,
            a_stageHeight,
            a_boxWidth,
            a_boxHeight,
            a_pivotNormX,
            a_pivotNormY,
            a_enableClamping,
            a_edgeMargin);
    }

    ScreenCoord ClampAndFadeViewportPos(
        float a_normX,
        float a_normY,
        float a_portLeft,
        float a_portRight,
        float a_portBottom,
        float a_portTop,
        float a_stageOriginX,
        float a_stageOriginY,
        float a_stageWidth,
        float a_stageHeight,
        bool a_enableClamping,
        float a_edgeMargin)
    {
        return ClampAndFadeBoxViewportPos(
            a_normX, a_normY,
            a_portLeft, a_portRight, a_portBottom, a_portTop,
            a_stageOriginX, a_stageOriginY, a_stageWidth, a_stageHeight,
            0.0f, 0.0f, 0.0f, 0.0f,
            a_enableClamping, a_edgeMargin);
    }

    ScreenCoord WorldToScreen(
        const Vector3& a_worldPos,
        const Matrix4x4& a_worldToCam,
        float a_stageOriginX,
        float a_stageOriginY,
        float a_stageWidth,
        float a_stageHeight,
        bool a_enableClamping,
        float a_edgeMargin)
    {
        if (a_stageWidth <= 0.0f || a_stageHeight <= 0.0f) {
            return {};
        }

        const auto& mat = a_worldToCam.m;
        const float w =
            mat[3][0] * a_worldPos.x +
            mat[3][1] * a_worldPos.y +
            mat[3][2] * a_worldPos.z +
            mat[3][3];

        // Behind camera plane or on near-zero clip plane: prevent inverted mirroring
        if (w <= 0.0001f) {
            return {};
        }

        const float invW = 1.0f / w;
        const float ndcX = (mat[0][0] * a_worldPos.x + mat[0][1] * a_worldPos.y + mat[0][2] * a_worldPos.z + mat[0][3]) * invW;
        const float ndcY = (mat[1][0] * a_worldPos.x + mat[1][1] * a_worldPos.y + mat[1][2] * a_worldPos.z + mat[1][3]) * invW;

        const float sx = a_stageOriginX + (ndcX * 0.5f + 0.5f) * a_stageWidth;
        const float sy = a_stageOriginY + (0.5f - ndcY * 0.5f) * a_stageHeight;

        return ClampAndFadeStagePos(
            sx,
            sy,
            a_stageOriginX,
            a_stageOriginY,
            a_stageWidth,
            a_stageHeight,
            a_enableClamping,
            a_edgeMargin);
    }

    ScreenCoord SmoothStep(
        const ScreenCoord& a_prev,
        const ScreenCoord& a_target,
        float a_smoothingSpeed,
        bool a_hasPrev,
        float a_deltaSeconds)
    {
        if (!a_hasPrev || !a_target.visible) {
            return a_target;
        }

        const float speed = std::clamp(a_smoothingSpeed, 0.05f, 1.0f);
        const float dt = std::clamp(a_deltaSeconds, 0.0f, 0.1f);

        float t = 1.0f;
        if (speed < 0.9999f) {
            t = (dt <= 0.0f) ? 0.0f : (1.0f - std::pow(1.0f - speed, dt * 60.0f));
        }

        return {
            a_prev.x + (a_target.x - a_prev.x) * t,
            a_prev.y + (a_target.y - a_prev.y) * t,
            a_prev.alpha + (a_target.alpha - a_prev.alpha) * t,
            true
        };
    }
}

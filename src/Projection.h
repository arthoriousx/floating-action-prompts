#pragma once

#include "Config.h"

namespace Projection
{
    struct ScreenCoord
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float alpha{ 0.0f }; // 0.0..1.0 normalized opacity
        bool visible{ false };
    };

    struct Matrix4x4
    {
        float m[4][4]{}; // Matches RE::NiCamera::worldToCam[4][4]
    };

    [[nodiscard]] ScreenCoord ClampAndFadeViewportPos(
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
        float a_edgeMargin);

    [[nodiscard]] ScreenCoord ClampAndFadeBoxViewportPos(
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
        float a_edgeMargin);

    [[nodiscard]] ScreenCoord WorldToScreen(
        const Vector3& a_worldPos,
        const Matrix4x4& a_worldToCam,
        float a_stageOriginX,
        float a_stageOriginY,
        float a_stageWidth,
        float a_stageHeight,
        bool a_enableClamping,
        float a_edgeMargin);

    [[nodiscard]] ScreenCoord SmoothStep(
        const ScreenCoord& a_prev,
        const ScreenCoord& a_target,
        float a_smoothingSpeed,
        bool a_hasPrev,
        float a_deltaSeconds = 1.0f / 60.0f);
}

#include "check.h"
#include <iostream>
#include "../src/Config.h"
#include "../src/Projection.h"

namespace
{
    Projection::Matrix4x4 MakeIdentity()
    {
        Projection::Matrix4x4 mat{};
        mat.m[0][0] = 1.0f;
        mat.m[1][1] = 1.0f;
        mat.m[2][2] = 1.0f;
        mat.m[3][3] = 1.0f;
        return mat;
    }
}

int main()
{
    std::cout << "[TEST] Running test_projection..." << std::endl;

    const auto id = MakeIdentity();

    // 1. Identity matrix at origin maps to stage center (640, 360) on 1280x720
    {
        const auto sc = Projection::WorldToScreen(
            { 0.0f, 0.0f, 0.0f }, id, 0.0f, 0.0f, 1280.0f, 720.0f, true, 40.0f);
        CHECK(sc.visible);
        CHECK_NEAR(sc.x, 640.0f, 0.01f);
        CHECK_NEAR(sc.y, 360.0f, 0.01f);
        CHECK_NEAR(sc.alpha, 1.0f, 0.001f);
    }

    // 2. ClampAndFadeViewportPos maps NiCamera::WorldPtToScreenPt Y-up port coords to Scaleform stage
    {
        // Default port [0..1, 0..1], center (0.5, 0.5) -> (640, 360)
        const auto center = Projection::ClampAndFadeViewportPos(
            0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1280.0f, 720.0f, true, 40.0f);
        CHECK(center.visible);
        CHECK_NEAR(center.x, 640.0f, 0.01f);
        CHECK_NEAR(center.y, 360.0f, 0.01f);
        CHECK_NEAR(center.alpha, 1.0f, 0.001f);

        // Y-up normY = 0.75 -> top quarter of screen (y = 180)
        const auto topQ = Projection::ClampAndFadeViewportPos(
            0.75f, 0.75f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1280.0f, 720.0f, true, 40.0f);
        CHECK(topQ.visible);
        CHECK_NEAR(topQ.x, 960.0f, 0.01f);
        CHECK_NEAR(topQ.y, 180.0f, 0.01f);

        // Custom letterboxed camera port [left=0.1, right=0.9, bottom=0.1, top=0.9]
        // normY = 0.9 is top edge -> sx=640, sy=0 -> clamped to margin=40 with partial fade
        const auto portTop = Projection::ClampAndFadeViewportPos(
            0.5f, 0.9f, 0.1f, 0.9f, 0.1f, 0.9f, 0.0f, 0.0f, 1280.0f, 720.0f, true, 40.0f);
        CHECK(portTop.visible);
        CHECK_NEAR(portTop.x, 640.0f, 0.01f);
        CHECK_NEAR(portTop.y, 40.0f, 0.01f);
        CHECK(portTop.alpha > 0.0f && portTop.alpha < 1.0f);
    }

    // 3. Behind-camera point (w <= 0) returns visible=false, alpha=0
    {
        Projection::Matrix4x4 persp{};
        persp.m[0][0] = 1.0f;
        persp.m[1][1] = 1.0f;
        persp.m[2][2] = 1.0f;
        persp.m[3][2] = 1.0f;

        const auto behind = Projection::WorldToScreen(
            { 2.0f, 2.0f, -5.0f }, persp, 0.0f, 0.0f, 1280.0f, 720.0f, true, 40.0f);
        CHECK(!behind.visible);
        CHECK_NEAR(behind.alpha, 0.0f, 0.001f);
    }

    // 4. Edge clamping keeps coordinates inside [margin, W - margin] and fades alpha
    {
        const auto clamped = Projection::WorldToScreen(
            { 1.02f, 0.0f, 0.0f }, id, 0.0f, 0.0f, 1280.0f, 720.0f, true, 40.0f);
        CHECK(clamped.visible);
        CHECK_NEAR(clamped.x, 1240.0f, 0.01f);
        CHECK_NEAR(clamped.y, 360.0f, 0.01f);
        CHECK(clamped.alpha > 0.0f && clamped.alpha < 1.0f);

        const auto farOut = Projection::WorldToScreen(
            { 2.0f, 0.0f, 0.0f }, id, 0.0f, 0.0f, 1280.0f, 720.0f, true, 40.0f);
        CHECK(!farOut.visible);
        CHECK_NEAR(farOut.alpha, 0.0f, 0.001f);
    }

    // 5. Without clamping, off-screen point returns visible=false, alpha=0
    {
        const auto off = Projection::WorldToScreen(
            { 1.05f, 0.0f, 0.0f }, id, 0.0f, 0.0f, 1280.0f, 720.0f, false, 40.0f);
        CHECK(!off.visible);
        CHECK_NEAR(off.alpha, 0.0f, 0.001f);
    }

    // 6. Framerate-independent SmoothStep: 1 step @ 60fps == 2 steps @ 120fps
    {
        const Projection::ScreenCoord prev{ 0.0f, 0.0f, 0.0f, true };
        const Projection::ScreenCoord target{ 100.0f, 200.0f, 1.0f, true };

        const auto one60 = Projection::SmoothStep(prev, target, 0.25f, true, 1.0f / 60.0f);
        CHECK_NEAR(one60.x, 25.0f, 0.05f);
        CHECK_NEAR(one60.y, 50.0f, 0.05f);

        const auto half1 = Projection::SmoothStep(prev, target, 0.25f, true, 1.0f / 120.0f);
        const auto half2 = Projection::SmoothStep(half1, target, 0.25f, true, 1.0f / 120.0f);
        CHECK_NEAR(half2.x, one60.x, 0.05f);
        CHECK_NEAR(half2.y, one60.y, 0.05f);
        CHECK_NEAR(half2.alpha, one60.alpha, 0.005f);
    }

    // 7. ClampAndFadeBoxViewportPos clamps box dimensions inside safe stage margins
    {
        // Target near bottom-right corner (normX=0.95, normY=0.05 in Y-up -> sx=1216, sy=684)
        // With box 260x240, pivot (0,0), margin 40 on 1280x720:
        // maxX = 1280 - 40 - 260 = 980, maxY = 720 - 40 - 240 = 440
        const auto boxBr = Projection::ClampAndFadeBoxViewportPos(
            0.95f, 0.05f,
            0.0f, 1.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1280.0f, 720.0f,
            260.0f, 240.0f, 0.0f, 0.0f,
            true, 40.0f);
        CHECK(boxBr.visible);
        CHECK_NEAR(boxBr.x, 980.0f, 0.01f);
        CHECK_NEAR(boxBr.y, 440.0f, 0.01f);

        // If box is larger than available stage width, safely collapses to midpoint without NaN/crash
        const auto hugeBox = Projection::ClampAndFadeBoxViewportPos(
            0.5f, 0.5f,
            0.0f, 1.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 400.0f, 300.0f,
            500.0f, 400.0f, 0.0f, 0.0f,
            true, 40.0f);
        CHECK(hugeBox.x >= -100.0f && hugeBox.x <= 400.0f);
    }

    std::cout << "[TEST] test_projection PASSED" << std::endl;
    return 0;
}

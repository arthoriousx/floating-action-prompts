#include "check.h"
#include <iostream>
#include <limits>
#include "../src/OverlayLayout.h"

int main()
{
    std::cout << "[TEST] Running test_overlay_layout..." << std::endl;

    const float screenW = 1920.0f;
    const float screenH = 1080.0f;
    const float margin = 20.0f;
    const float anchorX = 960.0f;
    const float anchorY = 540.0f;
    const float halfW = 100.0f;
    const float halfH = 20.0f;
    const float cardW = 240.0f;
    const float cardH = 120.0f;

    // 1. Mode 1 (Below)
    {
        const auto r = OverlayLayout::ComputeInfoCardRect(
            anchorX, anchorY, halfW, halfH, cardW, cardH,
            screenW, screenH, margin, 1, 0.0f, 0.0f);
        CHECK_NEAR(r.x, anchorX - cardW * 0.5f, 0.01f);
        CHECK_NEAR(r.y, anchorY + halfH + OverlayLayout::kDefaultGapPx, 0.01f);
    }

    // 2. Mode 2 (Above)
    {
        const auto r = OverlayLayout::ComputeInfoCardRect(
            anchorX, anchorY, halfW, halfH, cardW, cardH,
            screenW, screenH, margin, 2, 0.0f, 0.0f);
        CHECK_NEAR(r.x, anchorX - cardW * 0.5f, 0.01f);
        CHECK_NEAR(r.y, anchorY - halfH - OverlayLayout::kDefaultGapPx - cardH, 0.01f);
    }

    // 3. Mode 3 (Right) with manual offsets
    {
        const auto r = OverlayLayout::ComputeInfoCardRect(
            anchorX, anchorY, halfW, halfH, cardW, cardH,
            screenW, screenH, margin, 3, 16.0f, -8.0f);
        CHECK_NEAR(r.x, anchorX + halfW + OverlayLayout::kDefaultGapPx + 16.0f, 0.01f);
        CHECK_NEAR(r.y, anchorY - cardH * 0.5f - 8.0f, 0.01f);
    }

    // 4. Mode 4 (Left)
    {
        const auto r = OverlayLayout::ComputeInfoCardRect(
            anchorX, anchorY, halfW, halfH, cardW, cardH,
            screenW, screenH, margin, 4, 0.0f, 0.0f);
        CHECK_NEAR(r.x, anchorX - halfW - OverlayLayout::kDefaultGapPx - cardW, 0.01f);
        CHECK_NEAR(r.y, anchorY - cardH * 0.5f, 0.01f);
    }

    // 5. Mode 5 (Fixed Top-Right) & Mode 6 (Fixed Bottom-Right)
    {
        const auto topR = OverlayLayout::ComputeInfoCardRect(
            anchorX, anchorY, halfW, halfH, cardW, cardH,
            screenW, screenH, margin, 5, 0.0f, 0.0f);
        CHECK_NEAR(topR.x, screenW - margin - cardW, 0.01f);
        CHECK_NEAR(topR.y, margin, 0.01f);

        const auto botR = OverlayLayout::ComputeInfoCardRect(
            anchorX, anchorY, halfW, halfH, cardW, cardH,
            screenW, screenH, margin, 6, 0.0f, 0.0f);
        CHECK_NEAR(botR.x, screenW - margin - cardW, 0.01f);
        CHECK_NEAR(botR.y, screenH - margin - cardH, 0.01f);
    }

    // 6. Clamping prevents card from clipping off screen edges
    {
        const auto r = OverlayLayout::ComputeInfoCardRect(
            1890.0f, 1060.0f, halfW, halfH, cardW, cardH,
            screenW, screenH, margin, 3, 50.0f, 50.0f);
        CHECK(r.x >= margin && r.x + cardW <= screenW - margin + 0.01f);
        CHECK(r.y >= margin && r.y + cardH <= screenH - margin + 0.01f);
    }

    // 7. FontAwesome Solid codepoints are valid PUA glyphs for all categories
    {
        using IK = TargetInfo::ItemKind;
        const IK kinds[] = {
            IK::Weapon, IK::Armor, IK::Aid, IK::Ammo, IK::MiscJunk,
            IK::Key, IK::BookNote, IK::Holotape, IK::Container,
            IK::Door, IK::Furniture, IK::Terminal, IK::Npc
        };
        for (const auto kind : kinds) {
            const unsigned int cp = OverlayLayout::GetItemIconCodepoint(kind);
            CHECK(cp >= 0xF000u && cp <= 0xFFFFu);
        }
    }

    // 8. ComputeInlineSubPillPos places below normally and flips above near bottom margin
    {
        const auto below = OverlayLayout::ComputeInlineSubPillPos(
            900.0f, 500.0f, 120.0f, 32.0f, 180.0f, 22.0f, 4.0f,
            1920.0f, 1080.0f, 20.0f, true);
        CHECK_NEAR(below.x, 870.0f, 0.01f);
        CHECK_NEAR(below.y, 536.0f, 0.01f);

        // Near bottom edge (mainY=1020, mainH=32 -> bottom=1052; margin=20 -> maxY=1060):
        // sub pill height 22 below would overlap bottom margin -> flips above (1020 - 4 - 22 = 994)
        const auto flipped = OverlayLayout::ComputeInlineSubPillPos(
            900.0f, 1020.0f, 120.0f, 32.0f, 180.0f, 22.0f, 4.0f,
            1920.0f, 1080.0f, 20.0f, true);
        CHECK(flipped.y + 22.0f <= 1020.0f);
        CHECK_NEAR(flipped.y, 994.0f, 0.01f);
    }

    // 9. enableClamping = false leaves InfoCard unclamped even outside margin
    {
        const auto unclamped = OverlayLayout::ComputeInfoCardRect(
            1890.0f, 1060.0f, halfW, halfH, cardW, cardH,
            screenW, screenH, margin, 3, 50.0f, 50.0f, false);
        CHECK(unclamped.x > screenW - margin - cardW);
    }

    // 10. ComputeHoverDelayAlpha gates and smoothly fades InfoCard reveal (incl. NaN safety)
    {
        const float qnan = std::numeric_limits<float>::quiet_NaN();
        CHECK_NEAR(OverlayLayout::ComputeHoverDelayAlpha(0, 0.0f), 1.0f, 0.001f);
        CHECK_NEAR(OverlayLayout::ComputeHoverDelayAlpha(500, 1.0f), 0.0f, 0.001f);
        CHECK_NEAR(OverlayLayout::ComputeHoverDelayAlpha(1000, 1.0f, 0.18f), 0.0f, 0.001f);
        CHECK_NEAR(OverlayLayout::ComputeHoverDelayAlpha(1090, 1.0f, 0.18f), 0.5f, 0.01f);
        CHECK_NEAR(OverlayLayout::ComputeHoverDelayAlpha(1300, 1.0f, 0.18f), 1.0f, 0.001f);
        CHECK_NEAR(OverlayLayout::ComputeHoverDelayAlpha(100, qnan, 0.18f), 1.0f, 0.001f);
        CHECK_NEAR(OverlayLayout::ComputeHoverDelayAlpha(1200, 1.0f, qnan), 1.0f, 0.001f);
    }

    // 11. ComputeListWindow handles large containers past item #64 and edge bounds
    {
        const auto wMid = OverlayLayout::ComputeListWindow(500, 120, 6);
        CHECK(wMid.start == 118);
        CHECK(wMid.end == 124);
        CHECK(wMid.clampedSelected == 120);

        const auto wEnd = OverlayLayout::ComputeListWindow(500, 499, 6);
        CHECK(wEnd.start == 494);
        CHECK(wEnd.end == 500);
        CHECK(wEnd.clampedSelected == 499);

        const auto wSmall = OverlayLayout::ComputeListWindow(3, 1, 6);
        CHECK(wSmall.start == 0);
        CHECK(wSmall.end == 3);
        CHECK(wSmall.clampedSelected == 1);

        const auto wEmpty = OverlayLayout::ComputeListWindow(0, 2, 6);
        CHECK(wEmpty.start == 0);
        CHECK(wEmpty.end == 0);
        CHECK(wEmpty.clampedSelected == 0);

        const auto wClamp = OverlayLayout::ComputeListWindow(10, -5, 6);
        CHECK(wClamp.start == 0);
        CHECK(wClamp.end == 6);
        CHECK(wClamp.clampedSelected == 0);

        const auto wOver = OverlayLayout::ComputeListWindow(10, 99, 6);
        CHECK(wOver.start == 4);
        CHECK(wOver.end == 10);
        CHECK(wOver.clampedSelected == 9);
    }

    // 12. IsCategoryIconEnabled respects both master toggle and per-category toggles
    {
        ConfigData cfg;
        CHECK(OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Weapon, cfg));
        CHECK(OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Aid, cfg));
        CHECK(!OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Door, cfg));
        CHECK(!OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Furniture, cfg));
        CHECK(!OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Npc, cfg));
        CHECK(!OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::None, cfg));

        cfg.iconDoor = true;
        cfg.iconFurniture = true;
        CHECK(OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Door, cfg));
        CHECK(OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Furniture, cfg));

        cfg.iconDoor = false;
        cfg.iconFurniture = false;
        CHECK(!OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Door, cfg));
        CHECK(!OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Furniture, cfg));
        CHECK(OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Weapon, cfg));

        cfg.modernShowItemIcon = false;
        CHECK(!OverlayLayout::IsCategoryIconEnabled(TargetInfo::ItemKind::Weapon, cfg));
    }

    // 13. TruncateUtf8Prefix never splits multi-byte UTF-8 sequences
    {
        const std::string ascii = "Industrial Oil Canister";
        CHECK(OverlayLayout::TruncateUtf8Prefix(ascii, 100) == ascii);
        CHECK(OverlayLayout::TruncateUtf8Prefix(ascii, 10) == "Industrial");

        // "Ab\xC3\xB3bora" -> 'A'(1) 'b'(1) '\xC3\xB3'(2) 'b'(1) ...
        // maxBytes=3 cuts mid-codepoint -> must step back to 2 ("Ab")
        const std::string utf8 = "Ab\xC3\xB3""bora";
        CHECK(OverlayLayout::TruncateUtf8Prefix(utf8, 3) == "Ab");
        CHECK(OverlayLayout::TruncateUtf8Prefix(utf8, 4) == "Ab\xC3\xB3");
        CHECK(OverlayLayout::TruncateUtf8Prefix(utf8, 0).empty());
    }

    // 14. GetItemIconColorRgb preserves single color when OFF and distinct palette when ON
    {
        const std::uint32_t customCol = 0x112233u;
        CHECK(OverlayLayout::GetItemIconColorRgb(TargetInfo::ItemKind::Weapon, customCol, false) == customCol);
        CHECK(OverlayLayout::GetItemIconColorRgb(TargetInfo::ItemKind::Aid, customCol, false) == customCol);
        CHECK(OverlayLayout::GetItemIconColorRgb(TargetInfo::ItemKind::None, customCol, true) == customCol);

        const auto weapCol = OverlayLayout::GetItemIconColorRgb(TargetInfo::ItemKind::Weapon, customCol, true);
        const auto aidCol = OverlayLayout::GetItemIconColorRgb(TargetInfo::ItemKind::Aid, customCol, true);
        const auto ammoCol = OverlayLayout::GetItemIconColorRgb(TargetInfo::ItemKind::Ammo, customCol, true);
        CHECK(weapCol != customCol);
        CHECK(aidCol != customCol);
        CHECK(weapCol != aidCol);
        CHECK(aidCol != ammoCol);
    }

    // 15. Hold progress ring geometry helpers (rounded rect & circle)
    {
        std::vector<OverlayLayout::HoldRingSegment> segs;

        // 0% -> empty, 100% -> full perimeter
        OverlayLayout::BuildRoundedRectProgressSegments(10.0f, 10.0f, 30.0f, 30.0f, 4.0f, 0.0f, segs);
        CHECK(segs.empty());

        OverlayLayout::BuildRoundedRectProgressSegments(10.0f, 10.0f, 30.0f, 30.0f, 0.0f, 0.25f, segs);
        CHECK(!segs.empty());
        // Starts at top-center (20, 10)
        CHECK_NEAR(segs.front().x1, 20.0f, 0.05f);
        CHECK_NEAR(segs.front().y1, 10.0f, 0.05f);
        // 25% of square 20x20 (perimeter 80 -> len 20): 10 right to (30,10) + 10 down to (30,20)
        CHECK_NEAR(segs.back().x2, 30.0f, 0.05f);
        CHECK_NEAR(segs.back().y2, 20.0f, 0.05f);

        OverlayLayout::BuildRoundedRectProgressSegments(10.0f, 10.0f, 30.0f, 30.0f, 4.0f, 1.0f, segs);
        CHECK(!segs.empty());
        CHECK_NEAR(segs.front().x1, 20.0f, 0.05f);
        CHECK_NEAR(segs.back().x2, 20.0f, 0.05f);
        CHECK_NEAR(segs.back().y2, 10.0f, 0.05f);

        // Circle progress
        OverlayLayout::BuildCircleProgressSegments(50.0f, 50.0f, 10.0f, 0.0f, 24, segs);
        CHECK(segs.empty());

        OverlayLayout::BuildCircleProgressSegments(50.0f, 50.0f, 10.0f, 0.5f, 24, segs);
        CHECK(!segs.empty());
        // Starts at 12 o'clock (50, 40) and ends at 6 o'clock (50, 60)
        CHECK_NEAR(segs.front().x1, 50.0f, 0.05f);
        CHECK_NEAR(segs.front().y1, 40.0f, 0.05f);
        CHECK_NEAR(segs.back().x2, 50.0f, 0.05f);
        CHECK_NEAR(segs.back().y2, 60.0f, 0.05f);

        OverlayLayout::BuildCircleProgressSegments(50.0f, 50.0f, 10.0f, 1.0f, 24, segs);
        CHECK(segs.size() == 24);
        CHECK_NEAR(segs.back().x2, 50.0f, 0.05f);
        CHECK_NEAR(segs.back().y2, 40.0f, 0.05f);
    }

    std::cout << "[TEST] test_overlay_layout PASSED" << std::endl;
    return 0;
}

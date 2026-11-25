#include "check.h"
#include <cstdio>
#include <iostream>
#include <limits>
#include "../src/Config.h"
#include "../src/Projection.h"
#include "../src/HUDController.h"

int main()
{
    std::cout << "[TEST] Running test_hud_controller..." << std::endl;

    // 1. Captures baseline on first float and applies framerate-independent smoothed coordinates
    {
        HUDController::HUDTransformState state;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 500.0f, 300.0f, 1.0f, true };
        in.smoothingSpeed = 0.25f;
        in.deltaSeconds = 1.0f / 60.0f;
        in.currentClipX = 100.0f;
        in.currentClipY = 200.0f;
        in.currentClipAlpha = 1.0f;

        const auto out1 = state.Step(in);
        CHECK(out1.action == HUDController::HUDAction::kApplyFloat);
        CHECK(out1.applyPosition);
        CHECK(!out1.applyAlpha); // Full on-screen alpha preserves AS3 fade-in
        CHECK_NEAR(out1.x, 500.0f, 0.01f);
        CHECK_NEAR(out1.y, 300.0f, 0.01f);

        // Subsequent frame at 60Hz lerps by 0.25
        in.projectedTarget = { 600.0f, 300.0f, 1.0f, true };
        const auto out2 = state.Step(in);
        CHECK(out2.action == HUDController::HUDAction::kApplyFloat);
        CHECK(out2.applyPosition);
        CHECK_NEAR(out2.x, 525.0f, 0.05f);
        CHECK_NEAR(out2.y, 300.0f, 0.05f);
    }

    // 2. Critical #1: Behind-camera / clipped projection hides (alpha = 0), does NOT snap to center HUD!
    {
        HUDController::HUDTransformState state;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 500.0f, 300.0f, 1.0f, true };
        in.smoothingSpeed = 0.25f;
        in.deltaSeconds = 1.0f / 60.0f;
        in.currentClipX = 100.0f;
        in.currentClipY = 200.0f;
        in.currentClipAlpha = 1.0f;

        CHECK(state.Step(in).action == HUDController::HUDAction::kApplyFloat);

        // Anchor moves behind camera while target is still active
        in.projectedTarget = { 0.0f, 0.0f, 0.0f, false };
        const auto hidden = state.Step(in);
        CHECK(hidden.action == HUDController::HUDAction::kApplyFloat);
        CHECK(!hidden.applyPosition);
        CHECK(hidden.applyAlpha);
        CHECK_NEAR(hidden.alpha, 0.0f, 0.001f);

        // Re-entering frustum snaps cleanly to new screen position & restores alpha
        in.projectedTarget = { 800.0f, 450.0f, 1.0f, true };
        const auto reentered = state.Step(in);
        CHECK(reentered.action == HUDController::HUDAction::kApplyFloat);
        CHECK(reentered.applyPosition);
        CHECK(reentered.applyAlpha);
        CHECK_NEAR(reentered.x, 800.0f, 0.05f);
        CHECK_NEAR(reentered.y, 450.0f, 0.05f);
        CHECK_NEAR(reentered.alpha, 1.0f, 0.001f);
    }

    // 3. Critical #2: Mid-fade-in currentClipAlpha = 0.0 on frame 1 must NOT latch alpha to 0!
    {
        HUDController::HUDTransformState state;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 450.0f, 250.0f, 1.0f, true };
        in.currentClipX = 100.0f;
        in.currentClipY = 200.0f;
        in.currentClipAlpha = 0.0f;

        const auto out = state.Step(in);
        CHECK(out.action == HUDController::HUDAction::kApplyFloat);
        CHECK(out.applyPosition);
        CHECK(!out.applyAlpha); // Must leave AS3 tween alone instead of locking alpha = 0
    }

    // 4. Entering Power Armor immediately restores baseline (0 grace frames) & clears baseline for PA exit
    {
        HUDController::HUDTransformState state;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 500.0f, 300.0f, 1.0f, true };
        in.smoothingSpeed = 0.5f;
        in.deltaSeconds = 1.0f / 60.0f;
        in.currentClipX = 100.0f;
        in.currentClipY = 200.0f;
        in.currentClipAlpha = 1.0f;
        in.powerArmorBypass = false;
        in.inPowerArmor = true;
        CHECK(state.Step(in).action == HUDController::HUDAction::kApplyFloat);

        in.powerArmorBypass = true;
        const auto restore = state.Step(in);
        CHECK(restore.action == HUDController::HUDAction::kRestoreBaseline);
        CHECK(restore.applyPosition);
        CHECK_NEAR(restore.x, 100.0f, 0.01f);
        CHECK_NEAR(restore.y, 200.0f, 0.01f);

        // Next PA frame is kNoOp
        CHECK(state.Step(in).action == HUDController::HUDAction::kNoOp);

        // Exiting PA with new PA/FallUI resting layout captures the new coords cleanly
        in.inPowerArmor = false;
        in.currentClipX = 324.0f;
        in.currentClipY = 9.0f;
        CHECK(state.Step(in).action == HUDController::HUDAction::kApplyFloat);

        in.enabled = false;
        const auto restoreNew = state.Step(in);
        CHECK(restoreNew.action == HUDController::HUDAction::kRestoreBaseline);
        CHECK_NEAR(restoreNew.x, 324.0f, 0.01f);
        CHECK_NEAR(restoreNew.y, 9.0f, 0.01f);
    }

    // 5. QuickContainer visible immediately restores baseline (0 grace frames)
    {
        HUDController::HUDTransformState state;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 420.0f, 220.0f, 1.0f, true };
        in.currentClipX = 50.0f;
        in.currentClipY = 80.0f;
        in.currentClipAlpha = 1.0f;

        CHECK(state.Step(in).action == HUDController::HUDAction::kApplyFloat);

        in.quickContainerVisible = true;
        const auto restore = state.Step(in);
        CHECK(restore.action == HUDController::HUDAction::kRestoreBaseline);
        CHECK_NEAR(restore.x, 50.0f, 0.01f);
        CHECK_NEAR(restore.y, 80.0f, 0.01f);
    }

    // 6. Important #6: 1-2 frame target dip holds during grace window, restores on frame 3
    {
        HUDController::HUDTransformState state;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 700.0f, 400.0f, 1.0f, true };
        in.currentClipX = 120.0f;
        in.currentClipY = 240.0f;
        in.currentClipAlpha = 1.0f;

        CHECK(state.Step(in).action == HUDController::HUDAction::kApplyFloat);

        in.hasTarget = false;
        CHECK(state.Step(in).action == HUDController::HUDAction::kNoOp); // grace frame 1
        CHECK(state.Step(in).action == HUDController::HUDAction::kNoOp); // grace frame 2

        const auto restore = state.Step(in); // frame 3 -> sustained loss
        CHECK(restore.action == HUDController::HUDAction::kRestoreBaseline);
        CHECK_NEAR(restore.x, 120.0f, 0.01f);
        CHECK_NEAR(restore.y, 240.0f, 0.01f);

        CHECK(state.Step(in).action == HUDController::HUDAction::kNoOp);
    }

    // 7. StageToParentLocalFallback converts stage coords to scaled/offset parent clip coords
    {
        float lx = 0.0f, ly = 0.0f;
        HUDController::StageToParentLocalFallback(740.0f, 460.0f, 640.0f, 360.0f, 2.0f, 0.5f, lx, ly);
        CHECK_NEAR(lx, 50.0f, 0.01f);
        CHECK_NEAR(ly, 200.0f, 0.01f);

        HUDController::StageToParentLocalFallback(100.0f, 200.0f, 10.0f, 20.0f, 0.0f, -0.00001f, lx, ly);
        CHECK_NEAR(ly, 180.0f, 0.01f);
    }

    // 8. hideNativeClip forces native clip alpha = 0 while advancing smoothed coords, then restores 1.0
    {
        HUDController::HUDTransformState state;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 500.0f, 300.0f, 1.0f, true };
        in.smoothingSpeed = 0.5f;
        in.deltaSeconds = 1.0f / 60.0f;
        in.currentClipX = 100.0f;
        in.currentClipY = 200.0f;
        in.currentClipAlpha = 1.0f;
        in.hideNativeClip = true;

        const auto hiddenOut = state.Step(in);
        CHECK(hiddenOut.action == HUDController::HUDAction::kApplyFloat);
        CHECK_NEAR(hiddenOut.x, 500.0f, 0.01f);
        CHECK_NEAR(hiddenOut.y, 300.0f, 0.01f);
        CHECK(hiddenOut.applyAlpha);
        CHECK_NEAR(hiddenOut.alpha, 0.0f, 0.001f);

        // Switching back to Vanilla (hideNativeClip = false) restores alpha cleanly to 1.0
        in.hideNativeClip = false;
        in.projectedTarget = { 600.0f, 300.0f, 1.0f, true };
        const auto visibleOut = state.Step(in);
        CHECK(visibleOut.action == HUDController::HUDAction::kApplyFloat);
        CHECK(visibleOut.applyPosition);
        CHECK_NEAR(visibleOut.x, 550.0f, 0.05f);
        CHECK(visibleOut.applyAlpha);
        CHECK_NEAR(visibleOut.alpha, 1.0f, 0.001f);
    }

    // 9. deadzonePx pins smoothed coordinates against micro-jitter and tracks once threshold exceeded
    {
        HUDController::HUDTransformState state;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 400.0f, 300.0f, 1.0f, true };
        in.smoothingSpeed = 0.5f;
        in.deltaSeconds = 1.0f / 60.0f;
        in.deadzonePx = 8.0f;

        const auto f1 = state.Step(in);
        CHECK(f1.action == HUDController::HUDAction::kApplyFloat);
        CHECK_NEAR(f1.x, 400.0f, 0.01f);

        // 4 px shift < 8 px deadzone -> stays pinned at (400, 300)
        in.projectedTarget = { 403.0f, 302.0f, 1.0f, true };
        const auto f2 = state.Step(in);
        CHECK_NEAR(f2.x, 400.0f, 0.01f);
        CHECK_NEAR(f2.y, 300.0f, 0.01f);

        // 20 px shift > 8 px deadzone -> resumes smoothing to 410 and converges all the way to 420
        in.projectedTarget = { 420.0f, 300.0f, 1.0f, true };
        const auto f3 = state.Step(in);
        CHECK_NEAR(f3.x, 410.0f, 0.05f);

        HUDController::HUDFrameOutput settled = f3;
        for (int i = 0; i < 12; ++i) {
            settled = state.Step(in);
        }
        CHECK_NEAR(settled.x, 420.0f, 0.1f);

        // Micro-jitter < 8 px around new settled anchor 420 holds at 420
        in.projectedTarget = { 424.0f, 300.0f, 1.0f, true };
        const auto fHold = state.Step(in);
        CHECK_NEAR(fHold.x, 420.0f, 0.1f);
    }

    // 10. Critical #1: IsHidingAlpha tracks FAP-owned hidden state without triggering strobe loops
    {
        HUDController::HUDTransformState qcState;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 500.0f, 300.0f, 1.0f, true };

        CHECK(qcState.Step(in).action == HUDController::HUDAction::kApplyFloat);
        CHECK(!qcState.IsHidingAlpha());

        // Move behind camera: hides alpha = 0 and marks IsHidingAlpha() = true
        in.projectedTarget = { 0.0f, 0.0f, 0.0f, false };
        for (int i = 0; i < 5; ++i) {
            const auto out = qcState.Step(in);
            CHECK(out.action == HUDController::HUDAction::kApplyFloat);
            CHECK_NEAR(out.alpha, 0.0f, 0.001f);
            CHECK(qcState.IsHidingAlpha());
        }

        // Re-entering frustum clears IsHidingAlpha() cleanly
        in.projectedTarget = { 600.0f, 320.0f, 1.0f, true };
        const auto back = qcState.Step(in);
        CHECK(back.action == HUDController::HUDAction::kApplyFloat);
        CHECK_NEAR(back.alpha, 1.0f, 0.001f);
        CHECK(!qcState.IsHidingAlpha());
    }

    // 11. MergeQuickContainerInventory expands truncated 5-slot engine windows & populates ItemKind icons
    {
        auto mkItem = [](std::string a_name, std::uint32_t a_count, TargetInfo::ItemKind a_kind = TargetInfo::ItemKind::None) {
            HUDController::QuickContainerItemEntry e;
            e.name = std::move(a_name);
            e.count = a_count;
            e.itemKind = a_kind;
            return e;
        };

        std::vector<HUDController::QuickContainerItemEntry> inv10{
            mkItem("Daddy-O", 3, TargetInfo::ItemKind::Aid),
            mkItem("Calmex", 1, TargetInfo::ItemKind::Aid),
            mkItem("Med-X", 9, TargetInfo::ItemKind::Aid),
            mkItem("Abomination", 1, TargetInfo::ItemKind::BookNote),
            mkItem("Stimpak", 2, TargetInfo::ItemKind::Aid),
            mkItem("10mm Pistol", 1, TargetInfo::ItemKind::Weapon),
            mkItem("Leather Armor", 1, TargetInfo::ItemKind::Armor),
            mkItem("10mm Round", 24, TargetInfo::ItemKind::Ammo),
            mkItem("Desk Fan", 1, TargetInfo::ItemKind::MiscJunk),
            mkItem("Vault Key", 1, TargetInfo::ItemKind::Key)
        };

        // 11a. Top window (4 real + "...") expands to 6 real rows with no "..."
        {
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("Daddy-O", 3),
                mkItem("Calmex", 1),
                mkItem("Med-X", 9),
                mkItem("Abomination", 1),
                mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 3, inv10, 6);
            CHECK(res.totalItemCount == 10);
            CHECK(res.selectedIndex == 3);
            CHECK(res.windowStartIndex == 1);
            CHECK(res.hasMoreAbove == true);
            CHECK(res.hasMoreBelow == true);
            CHECK(res.windowItems.size() == 6);
            CHECK(res.windowItems[0].name == "Calmex");
            CHECK(res.windowItems[2].name == "Abomination");
            CHECK(res.windowItems[2].itemKind == TargetInfo::ItemKind::BookNote);
            CHECK(res.windowItems[3].name == "Stimpak");
            CHECK(res.windowItems[4].name == "10mm Pistol");
            CHECK(res.windowItems[5].name == "Leather Armor");
            for (const auto& w : res.windowItems) {
                CHECK(w.name != "...");
                CHECK(w.itemKind != TargetInfo::ItemKind::None);
            }
        }

        // 11b. Reversed inventory order detected contiguously
        {
            std::vector<HUDController::QuickContainerItemEntry> revInv(inv10.rbegin(), inv10.rend());
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("Daddy-O", 3),
                mkItem("Calmex", 1),
                mkItem("Med-X", 9),
                mkItem("Abomination", 1),
                mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 0, revInv, 6);
            CHECK(res.totalItemCount == 10);
            CHECK(res.selectedIndex == 0);
            CHECK(res.windowItems.size() == 6);
            CHECK(res.windowItems[0].name == "Daddy-O");
            CHECK(res.windowItems[5].name == "10mm Pistol");
        }

        // 11c. Bottom of list ("..." + 4 real items) anchors at bottom
        {
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("...", 1),
                mkItem("Leather Armor", 1),
                mkItem("10mm Round", 24),
                mkItem("Desk Fan", 1),
                mkItem("Vault Key", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 4, inv10, 6);
            CHECK(res.totalItemCount == 10);
            CHECK(res.selectedIndex == 9);
            CHECK(res.windowStartIndex == 4);
            CHECK(res.windowItems.size() == 6);
            CHECK(res.windowItems.back().name == "Vault Key");
            CHECK(res.windowItems.back().itemKind == TargetInfo::ItemKind::Key);
        }

        // 11d. Unmatched custom order avoids fabricating unverified neighbors & preserves exact engine items
        {
            std::vector<HUDController::QuickContainerItemEntry> customInv{
                inv10[4], inv10[1], inv10[6], inv10[3], inv10[0], inv10[8], inv10[2], inv10[5]
            };
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("Daddy-O", 3),
                mkItem("Calmex", 1),
                mkItem("Med-X", 9),
                mkItem("Abomination", 1),
                mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 1, customInv, 6);
            CHECK(res.totalItemCount == 4);
            CHECK(res.selectedIndex == 1);
            CHECK(res.windowItems.size() == 4);
            CHECK(res.windowItems[0].name == "Daddy-O");
            CHECK(res.windowItems[0].itemKind == TargetInfo::ItemKind::Aid);
            CHECK(res.windowItems[1].name == "Calmex");
            CHECK(res.windowItems[2].name == "Med-X");
            CHECK(res.windowItems[3].name == "Abomination");
            CHECK(res.windowItems[3].itemKind == TargetInfo::ItemKind::BookNote);
        }

        // 11e. Small container (<= 4 items, no "...") keeps exact count and enriches ItemKind
        {
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("Daddy-O", 3),
                mkItem("10mm Pistol", 1),
                mkItem("Vault Key", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 1, inv10, 6);
            CHECK(res.totalItemCount == 3);
            CHECK(res.selectedIndex == 1);
            CHECK(res.windowItems.size() == 3);
            CHECK(res.windowItems[0].itemKind == TargetInfo::ItemKind::Aid);
            CHECK(res.windowItems[1].itemKind == TargetInfo::ItemKind::Weapon);
            CHECK(res.windowItems[2].itemKind == TargetInfo::ItemKind::Key);
        }

        // 11f. Dual-ellipsis middle window expands to full maxRows while preserving selection
        {
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("...", 1),
                mkItem("Abomination", 1),
                mkItem("Stimpak", 2),
                mkItem("10mm Pistol", 1),
                mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 2, inv10, 6);
            CHECK(res.totalItemCount == 10);
            CHECK(res.selectedIndex == 4);
            CHECK(res.windowStartIndex == 2);
            CHECK(res.windowItems.size() == 6);
            CHECK(res.windowItems[0].name == "Med-X");
            CHECK(res.windowItems[2].name == "Stimpak");
            CHECK(res.windowItems[5].name == "10mm Round");
        }

        // 11g. Huge inventory (> kMaxExpandedInventoryItems) caps sorting/expansion safely while enriching icons
        {
            std::vector<HUDController::QuickContainerItemEntry> hugeInv;
            hugeInv.reserve(600);
            for (int i = 0; i < 600; ++i) {
                hugeInv.push_back(mkItem("Junk Item " + std::to_string(i), 1, TargetInfo::ItemKind::MiscJunk));
            }
            hugeInv[10] = mkItem("Daddy-O", 3, TargetInfo::ItemKind::Aid);
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("Daddy-O", 3),
                mkItem("Junk Item 1", 1),
                mkItem("Junk Item 2", 1),
                mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 0, hugeInv, 6);
            CHECK(res.totalItemCount == 3);
            CHECK(res.selectedIndex == 0);
            CHECK(res.windowItems.size() == 3);
            CHECK(res.windowItems[0].itemKind == TargetInfo::ItemKind::Aid);
            CHECK(res.windowItems[1].itemKind == TargetInfo::ItemKind::MiscJunk);
        }

        // 11h. FIS-tagged items preserve exact raw sort across tag boundary without 4<->6 flickering
        {
            std::vector<HUDController::QuickContainerItemEntry> fisInv{
                mkItem("[Med] Absorbent Cotton", 1, TargetInfo::ItemKind::Aid),
                mkItem("[Med] IFAK", 4, TargetInfo::ItemKind::Aid),
                mkItem("[Device] Aozora Item Extractor", 1, TargetInfo::ItemKind::MiscJunk),
                mkItem("[Med] Bandage", 4, TargetInfo::ItemKind::Aid),
                mkItem("[Med] Hard Reset - Keys", 1, TargetInfo::ItemKind::MiscJunk),
                mkItem("[Med] AFF Master Control", 1, TargetInfo::ItemKind::MiscJunk),
                mkItem("[Med] (Flashy) Sterile Bandage", 3, TargetInfo::ItemKind::Aid)
            };

            std::vector<HUDController::QuickContainerItemEntry> engTop{
                mkItem("[Device] Aozora Item Extractor", 1),
                mkItem("[Med] (Flashy) Sterile Bandage", 3),
                mkItem("[Med] Absorbent Cotton", 1),
                mkItem("[Med] AFF Master Control", 1),
                mkItem("...", 1)
            };
            const auto resTop = HUDController::MergeQuickContainerInventory(engTop, 0, fisInv, 6);
            CHECK(resTop.expanded == true);
            CHECK(resTop.matchedOrderIndex == 2);
            CHECK(resTop.totalItemCount == 7);
            CHECK(resTop.windowItems.size() == 6);
            CHECK(resTop.selectedIndex == 0);
            CHECK(resTop.windowItems[0].name == "[Device] Aozora Item Extractor");

            std::vector<HUDController::QuickContainerItemEntry> engMid{
                mkItem("...", 1),
                mkItem("[Med] (Flashy) Sterile Bandage", 3),
                mkItem("[Med] Absorbent Cotton", 1),
                mkItem("[Med] AFF Master Control", 1),
                mkItem("[Med] Bandage", 4),
                mkItem("...", 1)
            };
            const auto resMid = HUDController::MergeQuickContainerInventory(engMid, 3, fisInv, 6, resTop.matchedOrderIndex);
            CHECK(resMid.expanded == true);
            CHECK(resMid.matchedOrderIndex == 2);
            CHECK(resMid.windowItems.size() == 6);
            CHECK(resMid.selectedIndex == 3);

            std::vector<HUDController::QuickContainerItemEntry> engBot{
                mkItem("...", 1),
                mkItem("[Med] AFF Master Control", 1),
                mkItem("[Med] Bandage", 4),
                mkItem("[Med] Hard Reset - Keys", 1),
                mkItem("[Med] IFAK", 4)
            };
            const auto resBot = HUDController::MergeQuickContainerInventory(engBot, 4, fisInv, 6, resTop.matchedOrderIndex);
            CHECK(resBot.expanded == true);
            CHECK(resBot.windowItems.size() == 6);
            CHECK(resBot.selectedIndex == 6);
            CHECK(resBot.windowItems.back().name == "[Med] IFAK");
        }

        // 11i. Steamer Trunk regression: !hasEllipsisBefore strictly forbids inserting items above real row 0
        {
            std::vector<HUDController::QuickContainerItemEntry> trunkInv{
                mkItem(".44 Pistol | Advanced | Tactical", 1, TargetInfo::ItemKind::Weapon),
                mkItem("A45 Auto Pistol", 1, TargetInfo::ItemKind::Weapon),
                mkItem("ACombat Knife", 1, TargetInfo::ItemKind::Weapon),
                mkItem("AMagNite - 2D - Incandescent Bulb", 1, TargetInfo::ItemKind::Weapon),
                mkItem("Baseball Bat", 1, TargetInfo::ItemKind::Weapon),
                mkItem("Bulldog", 1, TargetInfo::ItemKind::Weapon)
            };
            std::vector<HUDController::QuickContainerItemEntry> engTrunkTop{
                mkItem("A45 Auto Pistol", 1),
                mkItem("ACombat Knife", 1),
                mkItem("AMagNite - 2D - Incandescent Bulb", 1),
                mkItem("Baseball Bat", 1),
                mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(engTrunkTop, 0, trunkInv, 6);
            CHECK(!res.windowItems.empty());
            CHECK(res.windowItems[0].name == "A45 Auto Pistol");
            CHECK(res.selectedIndex == 0);
            CHECK(res.windowStartIndex == 0);
        }

        // 11j. Unmatched custom order reports expanded == false and matchedOrderIndex == -1
        {
            std::vector<HUDController::QuickContainerItemEntry> customInv{
                inv10[4], inv10[1], inv10[6], inv10[3], inv10[0], inv10[8], inv10[2], inv10[5]
            };
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("Daddy-O", 3),
                mkItem("Calmex", 1),
                mkItem("Med-X", 9),
                mkItem("Abomination", 1),
                mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 1, customInv, 6);
            CHECK(res.expanded == false);
            CHECK(res.matchedOrderIndex == -1);
            CHECK(res.totalItemCount == 4);
        }

        // 11k. Dual-ellipsis with nInv == winLen + 1 does not underflow interior range
        {
            std::vector<HUDController::QuickContainerItemEntry> inv5(inv10.begin(), inv10.begin() + 5);
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("...", 1),
                inv5[1],
                inv5[2],
                inv5[3],
                inv5[4],
                mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 2, inv5, 6);
            CHECK(res.expanded == false);
            CHECK(res.totalItemCount == 4);
            CHECK(res.windowItems.size() == 4);
            CHECK(res.hasMoreAbove == true);
            CHECK(res.hasMoreBelow == true);
        }

        // 11l. AggregateItemStacks merges duplicate stack nodes safely & preserves distinct state stacks
        {
            std::vector<HUDController::QuickContainerItemEntry> stacks;
            stacks.push_back(mkItem("Dirty Water", 1, TargetInfo::ItemKind::Aid));
            stacks.push_back(mkItem("Dirty Water", 1, TargetInfo::ItemKind::Aid));
            auto favStack = mkItem("Dirty Water", 1, TargetInfo::ItemKind::Aid);
            favStack.isFavorite = true;
            stacks.push_back(favStack);
            auto eqStack = mkItem("10mm Pistol", 1, TargetInfo::ItemKind::Weapon);
            eqStack.isEquipped = true;
            stacks.push_back(eqStack);
            stacks.push_back(mkItem("10mm Pistol", 2, TargetInfo::ItemKind::Weapon));

            HUDController::AggregateItemStacks(stacks);
            CHECK(stacks.size() == 4);
            CHECK(stacks[0].name == "Dirty Water" && stacks[0].count == 2 && !stacks[0].isFavorite);
            CHECK(stacks[1].name == "Dirty Water" && stacks[1].count == 1 && stacks[1].isFavorite);
            CHECK(stacks[2].name == "10mm Pistol" && stacks[2].count == 1 && stacks[2].isEquipped);
            CHECK(stacks[3].name == "10mm Pistol" && stacks[3].count == 2 && !stacks[3].isEquipped);

            // Saturation guard against uint32 wrap-around and 4x QuikClot Injector stack merge
            std::vector<HUDController::QuickContainerItemEntry> quik{
                mkItem("QuikClot Injector", 2, TargetInfo::ItemKind::Aid),
                mkItem("QuikClot Injector", 2, TargetInfo::ItemKind::Aid),
                mkItem("QuikClot Injector", 1, TargetInfo::ItemKind::Aid),
                mkItem("QuikClot Injector", 1, TargetInfo::ItemKind::Aid)
            };
            HUDController::AggregateItemStacks(quik);
            CHECK(quik.size() == 1);
            CHECK(quik[0].count == 6);

            std::vector<HUDController::QuickContainerItemEntry> bigStacks{
                mkItem("Ammo", std::numeric_limits<std::uint32_t>::max() - 5u, TargetInfo::ItemKind::Ammo),
                mkItem("Ammo", 20u, TargetInfo::ItemKind::Ammo)
            };
            HUDController::AggregateItemStacks(bigStacks);
            CHECK(bigStacks.size() == 1);
            CHECK(bigStacks[0].count == std::numeric_limits<std::uint32_t>::max());

            // Distinct stackSignature (e.g., Fusion Core charge, health, or weapon mods) must NOT merge
            auto fcFull = mkItem("Fusion Core", 1, TargetInfo::ItemKind::Ammo);
            fcFull.stackSignature = 0x1111ull;
            auto fcHalf = mkItem("Fusion Core", 1, TargetInfo::ItemKind::Ammo);
            fcHalf.stackSignature = 0x2222ull;
            auto fcHalfDup = mkItem("Fusion Core", 2, TargetInfo::ItemKind::Ammo);
            fcHalfDup.stackSignature = 0x2222ull;
            std::vector<HUDController::QuickContainerItemEntry> fcStacks{ fcFull, fcHalf, fcHalfDup };
            HUDController::AggregateItemStacks(fcStacks);
            CHECK(fcStacks.size() == 2);
            CHECK(fcStacks[0].stackSignature == 0x1111ull && fcStacks[0].count == 1);
            CHECK(fcStacks[1].stackSignature == 0x2222ull && fcStacks[1].count == 3);
        }

        // 11m. Vanilla mid-scroll WITHOUT leading "..." expands smoothly past item 5
        {
            std::vector<HUDController::QuickContainerItemEntry> coolerInv{
                mkItem("Aozora Item Extractor", 1, TargetInfo::ItemKind::Aid),
                mkItem("(Flashy) Sterile Bandage", 3, TargetInfo::ItemKind::Aid),
                mkItem("AFT Master Control", 1, TargetInfo::ItemKind::Aid),
                mkItem("Absorbent Cotton", 1, TargetInfo::ItemKind::Aid),
                mkItem("Bandage", 4, TargetInfo::ItemKind::Aid),
                mkItem("Hard Reset - Keys", 1, TargetInfo::ItemKind::Aid),
                mkItem("IFAK", 4, TargetInfo::ItemKind::Aid),
                mkItem("PatchMeUp", 3, TargetInfo::ItemKind::Aid),
                mkItem("QuikClot Injector", 6, TargetInfo::ItemKind::Aid),
                mkItem("Rustic Med Kit", 1, TargetInfo::ItemKind::Aid)
            };

            // Initial open at item 0
            std::vector<HUDController::QuickContainerItemEntry> eng0{
                coolerInv[0], coolerInv[1], coolerInv[2], coolerInv[3], mkItem("...", 1)
            };
            const auto res0 = HUDController::MergeQuickContainerInventory(eng0, 0, coolerInv, 6, -1, -1);
            CHECK(res0.expanded == true);
            CHECK(res0.matchedStartIndex == 0);
            CHECK(res0.windowItems.size() == 6);
            CHECK(res0.hasMoreAbove == false);
            CHECK(res0.hasMoreBelow == true);

            // Scrolled down to "Hard Reset - Keys" (vanilla sends [AFT, Absorbent, Bandage, Hard Reset, "..."] with NO leading "...")
            std::vector<HUDController::QuickContainerItemEntry> engScroll{
                coolerInv[2], coolerInv[3], coolerInv[4], coolerInv[5], mkItem("...", 1)
            };
            const auto resScroll = HUDController::MergeQuickContainerInventory(
                engScroll, 3, coolerInv, 6, res0.matchedOrderIndex, res0.matchedStartIndex);
            CHECK(resScroll.expanded == true);
            CHECK(resScroll.matchedStartIndex == 2);
            CHECK(resScroll.selectedIndex == 5);
            CHECK(resScroll.windowStartIndex == 3);
            CHECK(resScroll.windowItems.size() == 6);
            CHECK(resScroll.hasMoreAbove == true);
            CHECK(resScroll.hasMoreBelow == true);
        }

        // 11n. Vanilla bottom-of-list WITHOUT any "..." (5 items) anchors strictly at container end & expands to maxRows
        {
            std::vector<HUDController::QuickContainerItemEntry> engBot{
                inv10[5], inv10[6], inv10[7], inv10[8], inv10[9]
            };
            const auto resBot = HUDController::MergeQuickContainerInventory(engBot, 4, inv10, 6, 0, 4);
            CHECK(resBot.expanded == true);
            CHECK(resBot.matchedStartIndex == 5);
            CHECK(resBot.selectedIndex == 9);
            CHECK(resBot.windowStartIndex == 4);
            CHECK(resBot.windowItems.size() == 6);
            CHECK(resBot.windowItems.front().name == "Stimpak");
            CHECK(resBot.windowItems.back().name == "Vault Key");
            CHECK(resBot.hasMoreAbove == true);
            CHECK(resBot.hasMoreBelow == false);
        }

        // 11o. Native 5-slot engine mode propagates ellipsis indicators accurately without fabricating rows
        {
            std::vector<HUDController::QuickContainerItemEntry> engMid{
                inv10[2], inv10[3], inv10[4], inv10[5], mkItem("...", 1)
            };
            const auto nativeMid = HUDController::MergeQuickContainerInventory(engMid, 1, {}, 5);
            CHECK(nativeMid.expanded == false);
            CHECK(nativeMid.totalItemCount == 4);
            CHECK(nativeMid.windowItems.size() == 4);
            CHECK(nativeMid.hasMoreAbove == false);
            CHECK(nativeMid.hasMoreBelow == true);

            std::vector<HUDController::QuickContainerItemEntry> engEnd{
                inv10[5], inv10[6], inv10[7], inv10[8], inv10[9]
            };
            const auto nativeEnd = HUDController::MergeQuickContainerInventory(engEnd, 4, {}, 5);
            CHECK(nativeEnd.expanded == false);
            CHECK(nativeEnd.totalItemCount == 5);
            CHECK(nativeEnd.windowItems.size() == 5);
            CHECK(nativeEnd.hasMoreAbove == false);
            CHECK(nativeEnd.hasMoreBelow == false);
        }

        // 11p. Locked sort order (a_preferredOrder) outranks a_prevStart proximity penalty at dist >= 6
        {
            std::vector<HUDController::QuickContainerItemEntry> dupInv{
                mkItem("ItemZ", 1, TargetInfo::ItemKind::Aid),
                mkItem("ItemA", 1, TargetInfo::ItemKind::Aid),
                mkItem("ItemB", 1, TargetInfo::ItemKind::Aid),
                mkItem("ItemC", 1, TargetInfo::ItemKind::Aid),
                mkItem("ItemD", 1, TargetInfo::ItemKind::Aid),
                mkItem("A0", 1, TargetInfo::ItemKind::Aid),
                mkItem("A1", 1, TargetInfo::ItemKind::Aid),
                mkItem("A2", 1, TargetInfo::ItemKind::Aid),
                mkItem("A3", 1, TargetInfo::ItemKind::Aid),
                mkItem("A4", 1, TargetInfo::ItemKind::Aid),
                mkItem("A5", 1, TargetInfo::ItemKind::Aid),
                mkItem("ItemW", 1, TargetInfo::ItemKind::Aid)
            };
            std::vector<HUDController::QuickContainerItemEntry> eng{
                mkItem("ItemA", 1),
                mkItem("ItemB", 1),
                mkItem("ItemC", 1),
                mkItem("ItemD", 1),
                mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 2, dupInv, 6, 2, 0);
            CHECK(res.expanded == true);
            CHECK(res.matchedOrderIndex == 2);
            CHECK(res.matchedStartIndex == 6);
            CHECK(res.windowItems.size() == 6);
            CHECK(res.windowItems.front().name == "ItemA");
            CHECK(res.windowItems[3].name == "ItemD");
            CHECK(res.windowItems.back().name == "ItemZ");
        }

        // 11q. 6-item Steamer Trunk (1 hidden item + 5 visible items, no "...") never expands at prevStart == -1 even if mappedSel > 0
        {
            std::vector<HUDController::QuickContainerItemEntry> trunk6{
                mkItem(".44 Pistol", 1, TargetInfo::ItemKind::Weapon),
                mkItem("A45 Auto Pistol", 1, TargetInfo::ItemKind::Weapon),
                mkItem("ACombat Knife", 1, TargetInfo::ItemKind::Weapon),
                mkItem("Assault Rifle", 1, TargetInfo::ItemKind::Weapon),
                mkItem("Baseball Bat", 1, TargetInfo::ItemKind::Weapon),
                mkItem("Bulldog", 1, TargetInfo::ItemKind::Weapon)
            };
            std::vector<HUDController::QuickContainerItemEntry> engNoEllipsis5{
                trunk6[1], trunk6[2], trunk6[3], trunk6[4], trunk6[5]
            };
            const auto res = HUDController::MergeQuickContainerInventory(engNoEllipsis5, 1, trunk6, 6, -1, -1);
            CHECK(res.expanded == false);
            CHECK(res.totalItemCount == 5);
            CHECK(res.windowItems.size() == 5);
            CHECK(res.windowItems[0].name == "A45 Auto Pistol");
            CHECK(res.matchedStartIndex == -1);
        }

        // 11r. nInv == winLen + 1 in mid-scroll (!hasEllipsisBefore && hasEllipsisAfter, prevStart >= 0) avoids underflow & matches start=0
        {
            std::vector<HUDController::QuickContainerItemEntry> inv5(inv10.begin(), inv10.begin() + 5);
            std::vector<HUDController::QuickContainerItemEntry> eng{
                inv5[0], inv5[1], inv5[2], inv5[3], mkItem("...", 1)
            };
            const auto res = HUDController::MergeQuickContainerInventory(eng, 2, inv5, 6, 0, 0);
            CHECK(res.expanded == true);
            CHECK(res.matchedStartIndex == 0);
            CHECK(res.totalItemCount == 5);
            CHECK(res.windowItems.size() == 5);
            CHECK(res.hasMoreAbove == false);
            CHECK(res.hasMoreBelow == false);
        }
    }

    // 12. Native QuickContainer close animation stays suppressed at alpha 0 until nativeClipVisible turns false
    {
        HUDController::HUDTransformState qcState;
        HUDController::HUDFrameInput in{};
        in.enabled = true;
        in.rolloverVisible = true;
        in.nativeClipVisible = true;
        in.hasTarget = true;
        in.projectedTarget = { 540.0f, 360.0f, 1.0f, true };
        in.currentClipX = 640.0f;
        in.currentClipY = 380.0f;
        in.hideNativeClip = true;

        const auto openOut = qcState.Step(in);
        CHECK(openOut.action == HUDController::HUDAction::kApplyFloat);
        CHECK_NEAR(openOut.alpha, 0.0f, 0.001f);

        // Look away: Scaleform clears Shown (rolloverVisible = false) and targetRef drops,
        // while QuickContainerWidget_mc.visible (nativeClipVisible) stays true for close animation
        in.rolloverVisible = false;
        in.hasTarget = false;
        in.nativeClipVisible = true;
        CHECK(qcState.Step(in).action == HUDController::HUDAction::kNoOp); // grace frame 1
        CHECK(qcState.Step(in).action == HUDController::HUDAction::kNoOp); // grace frame 2

        // Frame 3+: emits kSuppressNative with alpha = 0 (never flashes alpha = 1.0)
        for (int f = 0; f < 4; ++f) {
            const auto supOut = qcState.Step(in);
            CHECK(supOut.action == HUDController::HUDAction::kSuppressNative);
            CHECK(!supOut.applyPosition);
            CHECK(supOut.applyAlpha);
            CHECK_NEAR(supOut.alpha, 0.0f, 0.001f);
            CHECK(qcState.IsHidingAlpha());
        }

        // Re-entering a new container directly from kSuppressNative preserves original vanilla baseline (640, 380)
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.currentClipX = 540.0f; // stale floated clip position, must NOT overwrite baseline
        in.currentClipY = 360.0f;
        const auto reenterOut = qcState.Step(in);
        CHECK(reenterOut.action == HUDController::HUDAction::kApplyFloat);
        CHECK_NEAR(reenterOut.alpha, 0.0f, 0.001f);

        // Look away again until close animation ends (nativeClipVisible -> false)
        in.rolloverVisible = false;
        in.hasTarget = false;
        in.nativeClipVisible = true;
        CHECK(qcState.Step(in).action == HUDController::HUDAction::kNoOp);
        CHECK(qcState.Step(in).action == HUDController::HUDAction::kNoOp);
        CHECK(qcState.Step(in).action == HUDController::HUDAction::kSuppressNative);

        in.nativeClipVisible = false;
        const auto restOut = qcState.Step(in);
        CHECK(restOut.action == HUDController::HUDAction::kRestoreBaseline);
        CHECK(restOut.applyPosition);
        CHECK(restOut.applyAlpha);
        CHECK_NEAR(restOut.x, 640.0f, 0.01f);
        CHECK_NEAR(restOut.y, 380.0f, 0.01f);
        CHECK_NEAR(restOut.alpha, 1.0f, 0.001f);
        CHECK(!qcState.IsHidingAlpha());

        // Disabling hideNativeClip during kSuppressNative immediately restores alpha = 1.0
        in.rolloverVisible = true;
        in.hasTarget = true;
        in.nativeClipVisible = true;
        in.currentClipX = 700.0f;
        in.currentClipY = 400.0f;
        CHECK(qcState.Step(in).action == HUDController::HUDAction::kApplyFloat);
        in.rolloverVisible = false;
        in.hasTarget = false;
        CHECK(qcState.Step(in).action == HUDController::HUDAction::kNoOp);
        CHECK(qcState.Step(in).action == HUDController::HUDAction::kNoOp);
        CHECK(qcState.Step(in).action == HUDController::HUDAction::kSuppressNative);
        in.hideNativeClip = false;
        const auto toggleOffOut = qcState.Step(in);
        CHECK(toggleOffOut.action == HUDController::HUDAction::kRestoreBaseline);
        CHECK(toggleOffOut.applyAlpha);
        CHECK_NEAR(toggleOffOut.alpha, 1.0f, 0.001f);
        CHECK_NEAR(toggleOffOut.x, 700.0f, 0.01f);
        CHECK_NEAR(toggleOffOut.y, 400.0f, 0.01f);
    }

    // 13. ResolveMergedQuickContainerWindow recovers seamlessly to 12 rows after looting desync
    {
        auto mkItem = [](std::string a_name, std::uint32_t a_count, TargetInfo::ItemKind a_kind = TargetInfo::ItemKind::Aid) {
            HUDController::QuickContainerItemEntry e;
            e.name = std::move(a_name);
            e.count = a_count;
            e.itemKind = a_kind;
            return e;
        };

        std::vector<HUDController::QuickContainerItemEntry> inv15;
        for (int i = 0; i < 15; ++i) {
            char nameBuf[32]{};
            std::snprintf(nameBuf, sizeof(nameBuf), "Item %02d", i);
            inv15.push_back(mkItem(nameBuf, 1));
        }

        HUDController::QuickContainerMergeCache cache;
        HUDController::CachedInventorySnapshot snap1;
        snap1.ownerFormID = 0x1001u;
        snap1.generation = 1u;
        snap1.items = inv15;

        // 13a. Expanded at middle of 15-item container (maxRows = 12)
        std::vector<HUDController::QuickContainerItemEntry> engMid{
            inv15[5], inv15[6], inv15[7], inv15[8], mkItem("...", 1)
        };
        const auto& r0 = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x1001u, snap1, engMid, 1, 12, true);
        CHECK(r0.expanded == true);
        CHECK(r0.windowItems.size() == 12);
        CHECK(r0.totalItemCount == 15);
        CHECK(r0.selectedIndex == 6);

        // Take Item 06 in the middle: inventory updates immediately (gen 2, 14 items),
        // while Scaleform rawWindow is desynced by 1 frame (still holds Item 06)
        HUDController::CachedInventorySnapshot snap2 = snap1;
        snap2.generation = 2u;
        snap2.items.erase(snap2.items.begin() + 6); // 14 items remain

        const auto& rDesync = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x1001u, snap2, engMid, 1, 12, true);
        CHECK(rDesync.expanded == true);
        CHECK(rDesync.windowItems.size() == 12); // Holds 12 rows without flashing down to 4

        // Next frame Scaleform updates rawWindow with mappedSel == 0 -> must recover to 12 rows!
        std::vector<HUDController::QuickContainerItemEntry> engMidAfter{
            snap2.items[6], snap2.items[7], snap2.items[8], snap2.items[9], mkItem("...", 1)
        };
        const auto& rMidRecovered = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x1001u, snap2, engMidAfter, 0, 12, true);
        CHECK(rMidRecovered.expanded == true);
        CHECK(rMidRecovered.totalItemCount == 14);
        CHECK(rMidRecovered.windowItems.size() == 12);
        CHECK(rMidRecovered.selectedIndex == 6);

        // 13b. Taking item near bottom (5 items, no "...") after 1-frame desync stays at 12 rows
        std::vector<HUDController::QuickContainerItemEntry> engBot{
            snap2.items[9], snap2.items[10], snap2.items[11], snap2.items[12], snap2.items[13]
        };
        const auto& rBot = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x1001u, snap2, engBot, 2, 12, true);
        CHECK(rBot.expanded == true);
        CHECK(rBot.windowItems.size() == 12);

        HUDController::CachedInventorySnapshot snap3 = snap2;
        snap3.generation = 3u;
        snap3.items.erase(snap3.items.begin() + 11); // 13 items remain

        const auto& rBotDesync = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x1001u, snap3, engBot, 2, 12, true);
        CHECK(rBotDesync.expanded == true);
        CHECK(rBotDesync.windowItems.size() == 12);

        std::vector<HUDController::QuickContainerItemEntry> engBotAfter{
            snap3.items[8], snap3.items[9], snap3.items[10], snap3.items[11], snap3.items[12]
        };
        const auto& rBotRecovered = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x1001u, snap3, engBotAfter, 3, 12, true);
        CHECK(rBotRecovered.expanded == true);
        CHECK(rBotRecovered.totalItemCount == 13);
        CHECK(rBotRecovered.windowItems.size() == 12);

        // 13c. Taking item at start of list after desync stays at 12 rows
        std::vector<HUDController::QuickContainerItemEntry> engTop{
            snap3.items[0], snap3.items[1], snap3.items[2], snap3.items[3], mkItem("...", 1)
        };
        const auto& rTop = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x1001u, snap3, engTop, 0, 12, true);
        CHECK(rTop.expanded == true);
        CHECK(rTop.windowItems.size() == 12);

        HUDController::CachedInventorySnapshot snap4 = snap3;
        snap4.generation = 4u;
        snap4.items.erase(snap4.items.begin()); // 12 items remain

        const auto& rTopDesync = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x1001u, snap4, engTop, 0, 12, true);
        CHECK(rTopDesync.expanded == true);
        CHECK(rTopDesync.windowItems.size() == 12);

        std::vector<HUDController::QuickContainerItemEntry> engTopAfter{
            snap4.items[0], snap4.items[1], snap4.items[2], snap4.items[3], mkItem("...", 1)
        };
        const auto& rTopRecovered = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x1001u, snap4, engTopAfter, 0, 12, true);
        CHECK(rTopRecovered.expanded == true);
        CHECK(rTopRecovered.totalItemCount == 12);
        CHECK(rTopRecovered.windowItems.size() == 12);

        // 13d. Genuinely custom-sorted container that never expanded still falls back safely after 3 misses
        HUDController::QuickContainerMergeCache customCache;
        HUDController::CachedInventorySnapshot customSnap;
        customSnap.ownerFormID = 0x2002u;
        customSnap.generation = 1u;
        customSnap.items = {
            inv15[10], inv15[1], inv15[8], inv15[3], inv15[12], inv15[0], inv15[7]
        };
        std::vector<HUDController::QuickContainerItemEntry> customEng{
            inv15[0], inv15[1], inv15[2], inv15[3], mkItem("...", 1)
        };
        for (int m = 0; m < 3; ++m) {
            const auto& rCustom = HUDController::ResolveMergedQuickContainerWindow(
                customCache, 0x2002u, customSnap, customEng, 0, 12, true);
            CHECK(rCustom.expanded == false);
            CHECK(rCustom.windowItems.size() == 4);
        }
        CHECK(customCache.stickyFallback == true);
    }

    // 14. Value-descending re-sort after looting, alphabetical prefix ties, and category+value sort
    {
        auto mkValItem = [](
            std::string_view a_name,
            std::uint32_t a_value,
            float a_weight,
            TargetInfo::ItemKind a_kind = TargetInfo::ItemKind::MiscJunk,
            std::uint32_t a_count = 1u)
        {
            HUDController::QuickContainerItemEntry e;
            e.name = std::string(a_name);
            e.count = a_count;
            e.itemKind = a_kind;
            e.value = a_value;
            e.weight = a_weight;
            return e;
        };

        // 14a. Container switches from alphabetical (order 2) to Value DESC (order 5) after removing 2 items
        HUDController::CachedInventorySnapshot snap;
        snap.ownerFormID = 0x5555u;
        snap.generation = 1u;
        snap.items = {
            mkValItem("Rusty Metal Cup", 9, 0.5f),
            mkValItem("Busted Box Torch", 300, 2.0f),
            mkValItem("Small Rusty Thermos", 54, 0.4f),
            mkValItem("Used Gas Mask Filter", 67, 1.0f),
            mkValItem("Fine Feathers", 60, 0.2f),
            mkValItem("Biometric Scanner", 20, 3.0f, TargetInfo::ItemKind::MiscJunk, 2u),
            mkValItem("Can Of Screws", 40, 1.0f),
            mkValItem("Brahmin Hide", 8, 2.0f),
            mkValItem("Wallet", 33, 1.0f),
            mkValItem("Duct Tape", 12, 0.1f),
            mkValItem("Fuse", 15, 0.5f, TargetInfo::ItemKind::MiscJunk, 3u),
            mkValItem("Gas Canister", 28, 3.0f),
            mkValItem("Office Desk Fan", 22, 3.0f),
            mkValItem("Used Oil Can", 16, 2.0f),
            mkValItem("White Bottle", 4, 0.5f),
            mkValItem("Tin Can", 2, 0.1f)
        };

        std::vector<HUDController::QuickContainerItemEntry> engAlphaTop{
            mkValItem("Biometric Scanner", 20, 3.0f, TargetInfo::ItemKind::MiscJunk, 2u),
            mkValItem("Brahmin Hide", 8, 2.0f),
            mkValItem("Busted Box Torch", 300, 2.0f),
            mkValItem("Can Of Screws", 40, 1.0f),
            mkValItem("...", 1, 0.0f)
        };
        HUDController::QuickContainerMergeCache cache;
        const auto& rInitial = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x5555u, snap, engAlphaTop, 0, 12, true);
        CHECK(rInitial.expanded == true);
        CHECK(rInitial.matchedOrderIndex == 2);
        CHECK(rInitial.totalItemCount == 16);
        CHECK(rInitial.windowItems.size() == 12);

        // Remove 2 low-value items ("Tin Can" and "Duct Tape"), engine re-sorts visible rows by Value DESC
        HUDController::CachedInventorySnapshot snapAfter = snap;
        snapAfter.generation = 2u;
        snapAfter.items.pop_back(); // removes "Tin Can"
        snapAfter.items.erase(snapAfter.items.begin() + 9); // removes "Duct Tape"

        std::vector<HUDController::QuickContainerItemEntry> engValueTop{
            mkValItem("Busted Box Torch", 300, 2.0f),
            mkValItem("Used Gas Mask Filter", 67, 1.0f),
            mkValItem("Fine Feathers", 60, 0.2f),
            mkValItem("Small Rusty Thermos", 54, 0.4f),
            mkValItem("...", 1, 0.0f)
        };
        const auto& rResorted = HUDController::ResolveMergedQuickContainerWindow(
            cache, 0x5555u, snapAfter, engValueTop, 0, 12, true);
        CHECK(rResorted.expanded == true);
        CHECK(rResorted.matchedOrderIndex == 5);
        CHECK(rResorted.totalItemCount == 14);
        CHECK(rResorted.windowItems.size() == 12);
        CHECK(rResorted.windowItems[0].name == "Busted Box Torch");
        CHECK(rResorted.windowItems[4].name == "Can Of Screws");
        CHECK(cache.stickyFallback == false);

        // 14b. Alphabetical prefix tie against raw swap-and-popped BGSInventoryList prefers alphabetical (order 2)
        std::vector<HUDController::QuickContainerItemEntry> tieInv{
            mkValItem("Alpha", 10, 1.0f),
            mkValItem("Bravo", 10, 1.0f),
            mkValItem("Charlie", 10, 1.0f),
            mkValItem("Delta", 10, 1.0f),
            mkValItem("Wallet", 10, 1.0f), // swap-and-popped non-alphabetical item at raw index 4
            mkValItem("Echo", 10, 1.0f),
            mkValItem("Foxtrot", 10, 1.0f),
            mkValItem("Golf", 10, 1.0f)
        };
        std::vector<HUDController::QuickContainerItemEntry> tieEng{
            mkValItem("Alpha", 10, 1.0f),
            mkValItem("Bravo", 10, 1.0f),
            mkValItem("Charlie", 10, 1.0f),
            mkValItem("Delta", 10, 1.0f),
            mkValItem("...", 1, 0.0f)
        };
        const auto rTie = HUDController::MergeQuickContainerInventory(tieEng, 0, tieInv, 12);
        CHECK(rTie.expanded == true);
        CHECK(rTie.matchedOrderIndex == 2);
        CHECK(rTie.windowItems.size() == 8);
        CHECK(rTie.windowItems[4].name == "Echo");

        // 14c. Category ASC + Value DESC sort (order 8): Key -> Weapon -> Armor -> Aid -> MiscJunk
        std::vector<HUDController::QuickContainerItemEntry> catInv{
            mkValItem("Expensive Junk", 500, 1.0f, TargetInfo::ItemKind::MiscJunk),
            mkValItem("Laser Rifle", 120, 4.0f, TargetInfo::ItemKind::Weapon),
            mkValItem("Storage Key", 0, 0.0f, TargetInfo::ItemKind::Key),
            mkValItem("Combat Armor", 140, 5.0f, TargetInfo::ItemKind::Armor),
            mkValItem("Stimpak", 48, 0.1f, TargetInfo::ItemKind::Aid),
            mkValItem("Desk Fan", 22, 3.0f, TargetInfo::ItemKind::MiscJunk),
            mkValItem("Scrap Copper", 15, 0.5f, TargetInfo::ItemKind::MiscJunk)
        };
        std::vector<HUDController::QuickContainerItemEntry> catEng{
            mkValItem("Storage Key", 0, 0.0f, TargetInfo::ItemKind::Key),
            mkValItem("Laser Rifle", 120, 4.0f, TargetInfo::ItemKind::Weapon),
            mkValItem("Combat Armor", 140, 5.0f, TargetInfo::ItemKind::Armor),
            mkValItem("Stimpak", 48, 0.1f, TargetInfo::ItemKind::Aid),
            mkValItem("...", 1, 0.0f)
        };
        const auto rCat = HUDController::MergeQuickContainerInventory(catEng, 0, catInv, 12);
        CHECK(rCat.expanded == true);
        CHECK(rCat.matchedOrderIndex == 8);
        CHECK(rCat.totalItemCount == 7);
        CHECK(rCat.windowItems.size() == 7);
        CHECK(rCat.windowItems[4].name == "Expensive Junk");

        // 14d. NaN weights in MergeQuickContainerInventory are safely sanitized during Value/Weight & Weight sorting
        const float qNan = std::numeric_limits<float>::quiet_NaN();
        std::vector<HUDController::QuickContainerItemEntry> nanInv{
            mkValItem("Zeta", 10, qNan),
            mkValItem("Busted Box Torch", 300, qNan),
            mkValItem("Used Gas Mask Filter", 67, 1.0f),
            mkValItem("Fine Feathers", 60, qNan),
            mkValItem("Small Rusty Thermos", 54, 0.4f),
            mkValItem("Can Of Screws", 40, 1.0f),
            mkValItem("Alpha", 5, qNan)
        };
        const auto rNan = HUDController::MergeQuickContainerInventory(engValueTop, 0, nanInv, 12);
        CHECK(rNan.expanded == true);
        CHECK(rNan.matchedOrderIndex == 5);
        CHECK(rNan.windowItems.size() == 7);

        // 14e. Small 3-item container keeps cache.consecutiveMisses == 0 and stickyFallback == false
        HUDController::QuickContainerMergeCache smallCache;
        HUDController::CachedInventorySnapshot smallSnap;
        smallSnap.ownerFormID = 0x6666u;
        smallSnap.generation = 1u;
        smallSnap.items = {
            mkValItem("Item A", 10, 1.0f),
            mkValItem("Item B", 20, 1.0f),
            mkValItem("Item C", 30, 1.0f)
        };
        std::vector<HUDController::QuickContainerItemEntry> smallEng = smallSnap.items;
        for (int frame = 0; frame < 5; ++frame) {
            const auto& rSmall = HUDController::ResolveMergedQuickContainerWindow(
                smallCache, 0x6666u, smallSnap, smallEng, 0, 12, true);
            CHECK(rSmall.expanded == false);
            CHECK(rSmall.totalItemCount == 3);
            CHECK(smallCache.consecutiveMisses == 0);
            CHECK(smallCache.stickyFallback == false);
        }
    }

    // 15. MSVC _stricmp ordering ('[' < 'a'..'z') for tagged/untagged dead NPCs, unequipped-first, and Weight ASC
    {
        auto mkNpcItem = [](
            std::string_view a_name,
            std::uint32_t a_value,
            float a_weight,
            TargetInfo::ItemKind a_kind,
            std::uint32_t a_count,
            bool a_equipped)
        {
            HUDController::QuickContainerItemEntry e;
            e.name = std::string(a_name);
            e.count = a_count;
            e.itemKind = a_kind;
            e.value = a_value;
            e.weight = a_weight;
            e.isEquipped = a_equipped;
            return e;
        };

        // 15a. Dead NPC mixing FIS [Tag] items ('[' = 91) and untagged Uppercase items ('A'..'Z' vs _stricmp 'a'..'z')
        std::vector<HUDController::QuickContainerItemEntry> npcInv{
            mkNpcItem("Harness", 10, 1.0f, TargetInfo::ItemKind::Armor, 1u, true),
            mkNpcItem("[Drink] Lipton Tea Box", 15, 0.5f, TargetInfo::ItemKind::Aid, 1u, false),
            mkNpcItem("Combat Armor Left Arm", 40, 3.0f, TargetInfo::ItemKind::Armor, 1u, false),
            mkNpcItem("[Ammo] 4.7mm Caseless Round", 1, 0.0f, TargetInfo::ItemKind::Ammo, 13u, false),
            mkNpcItem("Grease Rat Outfit", 35, 2.0f, TargetInfo::ItemKind::Armor, 1u, true),
            mkNpcItem("[Ammo] .357 Magnum", 3, 0.0f, TargetInfo::ItemKind::Ammo, 11u, false),
            mkNpcItem("[Torso] Raider Chest Piece", 25, 7.0f, TargetInfo::ItemKind::Armor, 1u, true),
            mkNpcItem("[Ammo] .30 Carbine", 2, 0.0f, TargetInfo::ItemKind::Ammo, 13u, false)
        };
        std::vector<HUDController::QuickContainerItemEntry> engNpcTop{
            mkNpcItem("[Ammo] .30 Carbine", 2, 0.0f, TargetInfo::ItemKind::Ammo, 13u, false),
            mkNpcItem("[Ammo] .357 Magnum", 3, 0.0f, TargetInfo::ItemKind::Ammo, 11u, false),
            mkNpcItem("[Ammo] 4.7mm Caseless Round", 1, 0.0f, TargetInfo::ItemKind::Ammo, 13u, false),
            mkNpcItem("[Drink] Lipton Tea Box", 15, 0.5f, TargetInfo::ItemKind::Aid, 1u, false),
            mkNpcItem("...", 1, 0.0f, TargetInfo::ItemKind::None, 1u, false)
        };
        const auto rNpc = HUDController::MergeQuickContainerInventory(engNpcTop, 0, npcInv, 12);
        CHECK(rNpc.expanded == true);
        CHECK(rNpc.matchedOrderIndex == 2);
        CHECK(rNpc.totalItemCount == 8);
        CHECK(rNpc.windowItems.size() == 8);
        CHECK(rNpc.windowItems[0].name == "[Ammo] .30 Carbine");
        CHECK(rNpc.windowItems[4].name == "[Torso] Raider Chest Piece");
        CHECK(rNpc.windowItems[5].name == "Combat Armor Left Arm");

        // 15b. Digit/dot prefix tie places '[Drink] Lipton Tea Box' (ASCII 91) before 'Harness' ('h' = 104) under _stricmp
        std::vector<HUDController::QuickContainerItemEntry> prefixTieInv{
            mkNpcItem(".30 Carbine", 2, 0.0f, TargetInfo::ItemKind::Ammo, 13u, false),
            mkNpcItem(".357 Magnum", 3, 0.0f, TargetInfo::ItemKind::Ammo, 11u, false),
            mkNpcItem("4.7mm Caseless Round", 1, 0.0f, TargetInfo::ItemKind::Ammo, 13u, false),
            mkNpcItem("5.56 Round", 2, 0.0f, TargetInfo::ItemKind::Ammo, 10u, false),
            mkNpcItem("Harness", 10, 1.0f, TargetInfo::ItemKind::Armor, 1u, true),
            mkNpcItem("[Drink] Lipton Tea Box", 15, 0.5f, TargetInfo::ItemKind::Aid, 1u, false)
        };
        std::vector<HUDController::QuickContainerItemEntry> prefixTieEng{
            mkNpcItem(".30 Carbine", 2, 0.0f, TargetInfo::ItemKind::Ammo, 13u, false),
            mkNpcItem(".357 Magnum", 3, 0.0f, TargetInfo::ItemKind::Ammo, 11u, false),
            mkNpcItem("4.7mm Caseless Round", 1, 0.0f, TargetInfo::ItemKind::Ammo, 13u, false),
            mkNpcItem("5.56 Round", 2, 0.0f, TargetInfo::ItemKind::Ammo, 10u, false),
            mkNpcItem("...", 1, 0.0f, TargetInfo::ItemKind::None, 1u, false)
        };
        const auto rPrefixTie = HUDController::MergeQuickContainerInventory(prefixTieEng, 0, prefixTieInv, 12);
        CHECK(rPrefixTie.expanded == true);
        CHECK(rPrefixTie.matchedOrderIndex == 2);
        CHECK(rPrefixTie.windowItems.size() == 6);
        CHECK(rPrefixTie.windowItems[4].name == "[Drink] Lipton Tea Box");
        CHECK(rPrefixTie.windowItems[5].name == "Harness");

        // 15c. Unequipped-before-equipped ordering (order 12) and Weight ASC ordering (order 13)
        std::vector<HUDController::QuickContainerItemEntry> eqInv{
            mkNpcItem("Alpha Armor", 10, 5.0f, TargetInfo::ItemKind::Armor, 1u, true),
            mkNpcItem("Beta Helmet", 20, 2.0f, TargetInfo::ItemKind::Armor, 1u, true),
            mkNpcItem("[Ammo] 10mm Round", 2, 0.0f, TargetInfo::ItemKind::Ammo, 20u, false),
            mkNpcItem("Pipe Rifle", 25, 4.0f, TargetInfo::ItemKind::Weapon, 1u, false),
            mkNpcItem("Combat Armor", 45, 3.0f, TargetInfo::ItemKind::Armor, 1u, false),
            mkNpcItem("Stimpak", 48, 0.1f, TargetInfo::ItemKind::Aid, 2u, false)
        };
        std::vector<HUDController::QuickContainerItemEntry> engEq{
            mkNpcItem("[Ammo] 10mm Round", 2, 0.0f, TargetInfo::ItemKind::Ammo, 20u, false),
            mkNpcItem("Combat Armor", 45, 3.0f, TargetInfo::ItemKind::Armor, 1u, false),
            mkNpcItem("Pipe Rifle", 25, 4.0f, TargetInfo::ItemKind::Weapon, 1u, false),
            mkNpcItem("Stimpak", 48, 0.1f, TargetInfo::ItemKind::Aid, 2u, false),
            mkNpcItem("...", 1, 0.0f, TargetInfo::ItemKind::None, 1u, false)
        };
        const auto rEq = HUDController::MergeQuickContainerInventory(engEq, 0, eqInv, 12);
        CHECK(rEq.expanded == true);
        CHECK(rEq.matchedOrderIndex == 12);
        CHECK(rEq.windowItems.size() == 6);
        CHECK(rEq.windowItems[4].name == "Alpha Armor");
        CHECK(rEq.windowItems[5].name == "Beta Helmet");

        std::vector<HUDController::QuickContainerItemEntry> wtAscInv{
            mkNpcItem("Heavy Armor", 50, 7.0f, TargetInfo::ItemKind::Armor, 1u, false),
            mkNpcItem("Light Aid", 15, 0.1f, TargetInfo::ItemKind::Aid, 1u, false),
            mkNpcItem("One Pound Tool", 40, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u, false),
            mkNpcItem("Zero Weight Ammo", 2, 0.0f, TargetInfo::ItemKind::Ammo, 10u, false),
            mkNpcItem("Medium Rifle", 120, 3.0f, TargetInfo::ItemKind::Weapon, 1u, false),
            mkNpcItem("Half Pound Cup", 5, 0.5f, TargetInfo::ItemKind::MiscJunk, 1u, false)
        };
        std::vector<HUDController::QuickContainerItemEntry> engWtAsc{
            mkNpcItem("Zero Weight Ammo", 2, 0.0f, TargetInfo::ItemKind::Ammo, 10u, false),
            mkNpcItem("Light Aid", 15, 0.1f, TargetInfo::ItemKind::Aid, 1u, false),
            mkNpcItem("Half Pound Cup", 5, 0.5f, TargetInfo::ItemKind::MiscJunk, 1u, false),
            mkNpcItem("One Pound Tool", 40, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u, false),
            mkNpcItem("...", 1, 0.0f, TargetInfo::ItemKind::None, 1u, false)
        };
        const auto rWtAsc = HUDController::MergeQuickContainerInventory(engWtAsc, 0, wtAscInv, 12);
        CHECK(rWtAsc.expanded == true);
        CHECK(rWtAsc.matchedOrderIndex == 13);
        CHECK(rWtAsc.windowItems.size() == 6);
        CHECK(rWtAsc.windowItems[4].name == "Medium Rifle");
        CHECK(rWtAsc.windowItems[5].name == "Heavy Armor");
    }

    // 16. Fallout 4 native corpse QuickContainer category order (Key->Book->Holo->Ammo->Aid->Weap->Misc->Armor) + priority bubbling
    {
        auto mkCorpseItem = [](
            std::string_view a_name,
            std::uint32_t a_value,
            float a_weight,
            TargetInfo::ItemKind a_kind,
            std::uint32_t a_count = 1u,
            bool a_equipped = false,
            bool a_better = false,
            bool a_tagged = false)
        {
            HUDController::QuickContainerItemEntry e;
            e.name = std::string(a_name);
            e.count = a_count;
            e.itemKind = a_kind;
            e.value = a_value;
            e.weight = a_weight;
            e.isEquipped = a_equipped;
            e.isBetterThanEquipped = a_better;
            e.isTaggedForSearch = a_tagged;
            return e;
        };

        // 16a. GLOWING ONE: Ammo -> Aid (Value DESC) -> MiscJunk (Value DESC) selects candidate 14 on Frame 0 (no scroll swap)
        {
            std::vector<HUDController::QuickContainerItemEntry> glowingInv{
                mkCorpseItem("[Ammo] .303 British", 2, 0.0f, TargetInfo::ItemKind::Ammo, 1u),
                mkCorpseItem("[Chem] Irradiated Blood", 50, 0.5f, TargetInfo::ItemKind::Aid, 2u),
                mkCorpseItem("[Food] Bullion Cubes Tin", 24, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("[RadFood] Tuna", 18, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("[Resource] Nuclear Material", 10, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("[Scrap] Match Box", 20, 0.4f, TargetInfo::ItemKind::MiscJunk, 1u)
            };
            std::vector<HUDController::QuickContainerItemEntry> engFrame0{
                mkCorpseItem("[Ammo] .303 British", 2, 0.0f, TargetInfo::ItemKind::Ammo, 1u),
                mkCorpseItem("[Chem] Irradiated Blood", 50, 0.5f, TargetInfo::ItemKind::Aid, 2u),
                mkCorpseItem("[Food] Bullion Cubes Tin", 24, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("[RadFood] Tuna", 18, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("...", 1, 0.0f, TargetInfo::ItemKind::None, 1u)
            };
            HUDController::QuickContainerMergeCache cache;
            HUDController::CachedInventorySnapshot snap;
            snap.ownerFormID = 0x7001u;
            snap.generation = 1u;
            snap.items = glowingInv;

            const auto& r0 = HUDController::ResolveMergedQuickContainerWindow(
                cache, 0x7001u, snap, engFrame0, 0, 12, true);
            CHECK(r0.expanded == true);
            CHECK(r0.matchedOrderIndex == 14);
            CHECK(r0.totalItemCount == 6);
            CHECK(r0.windowItems.size() == 6);
            CHECK(r0.windowItems[4].name == "[Scrap] Match Box");
            CHECK(r0.windowItems[5].name == "[Resource] Nuclear Material");

            // Scrolling to bottom (Match Box before Nuclear Material) keeps candidate 14 seamlessly
            std::vector<HUDController::QuickContainerItemEntry> engBottom{
                mkCorpseItem("[Chem] Irradiated Blood", 50, 0.5f, TargetInfo::ItemKind::Aid, 2u),
                mkCorpseItem("[Food] Bullion Cubes Tin", 24, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("[RadFood] Tuna", 18, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("[Scrap] Match Box", 20, 0.4f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("[Resource] Nuclear Material", 10, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u)
            };
            const auto& rScroll = HUDController::ResolveMergedQuickContainerWindow(
                cache, 0x7001u, snap, engBottom, 4, 12, true);
            CHECK(rScroll.expanded == true);
            CHECK(rScroll.matchedOrderIndex == 14);
            CHECK(rScroll.windowItems[4].name == "[Scrap] Match Box");
            CHECK(rScroll.windowItems[5].name == "[Resource] Nuclear Material");
        }

        // 16b. RAIDER #1: Aid -> Weapon -> MiscJunk (Value DESC) -> Armor expands 14 items cleanly
        {
            std::vector<HUDController::QuickContainerItemEntry> raiderInv{
                mkCorpseItem("Wastelander Coat", 125, 3.0f, TargetInfo::ItemKind::Armor, 1u, true),
                mkCorpseItem("White Bottle", 2, 0.5f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Freshly Baked Bread", 18, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("Short .357 Cattleman Revolver", 150, 3.5f, TargetInfo::ItemKind::Weapon, 1u),
                mkCorpseItem("Spark Plug", 50, 0.3f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Canned Oranges", 25, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("Click Master 2000", 123, 1.1f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Duct Tape", 12, 0.1f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Mutant Crisps", 12, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("Radscorpion Stinger", 30, 0.5f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Flip Lighter", 12, 0.5f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Dog Wood", 20, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Pack of Cigarettes", 12, 0.1f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Tea Leaf Ride", 6, 0.5f, TargetInfo::ItemKind::MiscJunk, 1u)
            };
            std::vector<HUDController::QuickContainerItemEntry> engRaiderTop{
                mkCorpseItem("Canned Oranges", 25, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("Freshly Baked Bread", 18, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("Mutant Crisps", 12, 0.5f, TargetInfo::ItemKind::Aid, 1u),
                mkCorpseItem("Short .357 Cattleman Revolver", 150, 3.5f, TargetInfo::ItemKind::Weapon, 1u),
                mkCorpseItem("...", 1, 0.0f, TargetInfo::ItemKind::None, 1u)
            };
            const auto rRaider = HUDController::MergeQuickContainerInventory(engRaiderTop, 0, raiderInv, 12);
            CHECK(rRaider.expanded == true);
            CHECK(rRaider.matchedOrderIndex == 14);
            CHECK(rRaider.totalItemCount == 14);
            CHECK(rRaider.windowItems.size() == 12);
            CHECK(rRaider.windowItems[4].name == "Click Master 2000");
            CHECK(rRaider.windowItems[11].name == "Tea Leaf Ride");
        }

        // 16c. SPARTANIUS RADAI: Aid -> Weapon -> Armor with FallUI auto-tags present only in Scaleform
        {
            std::vector<HUDController::QuickContainerItemEntry> spartInv{
                mkCorpseItem("Spartan Junkspear", 25, 2.0f, TargetInfo::ItemKind::Weapon, 1u, false),
                mkCorpseItem("Junk Spartan Helm", 100, 4.0f, TargetInfo::ItemKind::Armor, 1u, true),
                mkCorpseItem("Junk Legion Underarmour", 5, 3.0f, TargetInfo::ItemKind::Armor, 1u, true),
                mkCorpseItem("[Vegetables] Corn", 6, 0.1f, TargetInfo::ItemKind::Aid, 1u, false),
                mkCorpseItem("Junk Spartan Cape", 50, 3.0f, TargetInfo::ItemKind::Armor, 1u, true),
                mkCorpseItem("[PipeRevolver] Pipe Pistol", 20, 2.35f, TargetInfo::ItemKind::Weapon, 1u, false)
            };
            std::vector<HUDController::QuickContainerItemEntry> engSpart{
                mkCorpseItem("[Vegetables] Corn", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[Ranged] Spartan Junkspear", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[PipeRevolver] Pipe Pistol", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[Helm] Junk Spartan Helm", 0, 0.0f, TargetInfo::ItemKind::None, 1u, true),
                mkCorpseItem("...", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false)
            };
            const auto rSpart = HUDController::MergeQuickContainerInventory(engSpart, 0, spartInv, 12);
            CHECK(rSpart.expanded == true);
            CHECK(rSpart.matchedOrderIndex == 14);
            CHECK(rSpart.totalItemCount == 6);
            CHECK(rSpart.windowItems.size() == 6);
            CHECK(rSpart.windowItems[4].name == "Junk Spartan Cape");
            CHECK(rSpart.windowItems[5].name == "Junk Legion Underarmour");
        }

        // 16d. ZENVRIA SPARTINA: priority-flagged Armor (isBetterThanEquipped / isTaggedForSearch) bubbles to #0
        {
            std::vector<HUDController::QuickContainerItemEntry> zenvInv{
                mkCorpseItem("[SuperSledge] Spartan Junkspear", 25, 2.0f, TargetInfo::ItemKind::Weapon, 1u, false),
                mkCorpseItem("Junk Legion Underarmour", 5, 3.0f, TargetInfo::ItemKind::Armor, 1u, true),
                mkCorpseItem("Junk Legion Helm", 30, 2.0f, TargetInfo::ItemKind::Armor, 1u, true),
                mkCorpseItem("Smiling Spartan Gas Mask", 15, 3.0f, TargetInfo::ItemKind::Armor, 1u, true),
                mkCorpseItem("[Food] Dutch Chocolate Tin", 60, 1.0f, TargetInfo::ItemKind::Aid, 1u, false),
                mkCorpseItem("[10mm] 10mm Pistol", 50, 3.53f, TargetInfo::ItemKind::Weapon, 1u, false)
            };

            // BGSInventoryList tiebreak within Weapon (candidate 18): Spartan Junkspear before 10mm Pistol
            std::vector<HUDController::QuickContainerItemEntry> engZenv18{
                mkCorpseItem("[FullGasMask] Smiling Spartan Gas Mask", 0, 0.0f, TargetInfo::ItemKind::None, 1u, true, true, false),
                mkCorpseItem("[Food] Dutch Chocolate Tin", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[SuperSledge] Spartan Junkspear", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[10mm] 10mm Pistol", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("...", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false)
            };
            const auto rZenv18 = HUDController::MergeQuickContainerInventory(engZenv18, 0, zenvInv, 12);
            CHECK(rZenv18.expanded == true);
            CHECK(rZenv18.matchedOrderIndex == 18);
            CHECK(rZenv18.totalItemCount == 6);
            CHECK(rZenv18.windowItems.size() == 6);
            CHECK(rZenv18.windowItems[0].isBetterThanEquipped == true);
            CHECK(rZenv18.windowItems[4].name == "Junk Legion Underarmour");
            CHECK(rZenv18.windowItems[5].name == "Junk Legion Helm");

            // Value DESC tiebreak within Weapon & Armor (candidate 17)
            std::vector<HUDController::QuickContainerItemEntry> engZenv17{
                mkCorpseItem("[FullGasMask] Smiling Spartan Gas Mask", 0, 0.0f, TargetInfo::ItemKind::None, 1u, true, false, true),
                mkCorpseItem("[Food] Dutch Chocolate Tin", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[10mm] 10mm Pistol", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[SuperSledge] Spartan Junkspear", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("...", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false)
            };
            const auto rZenv17 = HUDController::MergeQuickContainerInventory(engZenv17, 0, zenvInv, 12);
            CHECK(rZenv17.expanded == true);
            CHECK(rZenv17.matchedOrderIndex == 17);
            CHECK(rZenv17.totalItemCount == 6);
            CHECK(rZenv17.windowItems.size() == 6);
            CHECK(rZenv17.windowItems[0].isTaggedForSearch == true);
            CHECK(rZenv17.windowItems[4].name == "Junk Legion Helm");
            CHECK(rZenv17.windowItems[5].name == "Junk Legion Underarmour");

            // Scrolling past #0 so priority item leaves the 5-slot engine window must stay expanded
            HUDController::QuickContainerMergeCache zenvCache;
            HUDController::CachedInventorySnapshot zenvSnap;
            zenvSnap.ownerFormID = 0x7002u;
            zenvSnap.generation = 1u;
            zenvSnap.items = zenvInv;

            const auto& rz0 = HUDController::ResolveMergedQuickContainerWindow(
                zenvCache, 0x7002u, zenvSnap, engZenv18, 0, 12, true);
            CHECK(rz0.expanded == true);
            CHECK(rz0.matchedOrderIndex == 18);
            CHECK(rz0.totalItemCount == 6);
            CHECK(zenvCache.stickyPriorityKeys.size() == 1);

            std::vector<HUDController::QuickContainerItemEntry> engZenvScrolledOut{
                mkCorpseItem("[Food] Dutch Chocolate Tin", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[SuperSledge] Spartan Junkspear", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[10mm] 10mm Pistol", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[Underarmor] Junk Legion Underarmour", 0, 0.0f, TargetInfo::ItemKind::None, 1u, true),
                mkCorpseItem("[Helm] Junk Legion Helm", 0, 0.0f, TargetInfo::ItemKind::None, 1u, true)
            };
            const auto& rzScroll = HUDController::ResolveMergedQuickContainerWindow(
                zenvCache, 0x7002u, zenvSnap, engZenvScrolledOut, 4, 12, true);
            CHECK(rzScroll.expanded == true);
            CHECK(rzScroll.matchedOrderIndex == 18);
            CHECK(rzScroll.totalItemCount == 6);
            CHECK(rzScroll.windowItems.size() == 6);
            CHECK(rzScroll.selectedIndex == 5);
            CHECK(rzScroll.windowItems[0].name == "Smiling Spartan Gas Mask");
            CHECK(zenvCache.stickyFallback == false);

            // Looting another item while priority item is off-screen preserves priority persistence across invGen
            HUDController::CachedInventorySnapshot zenvSnap7 = zenvSnap;
            zenvSnap7.items.push_back(mkCorpseItem("Junk Legion Boots", 20, 2.0f, TargetInfo::ItemKind::Armor, 1u, true));
            HUDController::QuickContainerMergeCache zenvCache7;
            zenvSnap7.generation = 1u;
            const auto& rz7_0 = HUDController::ResolveMergedQuickContainerWindow(
                zenvCache7, 0x7003u, zenvSnap7, engZenv18, 0, 12, true);
            CHECK(rz7_0.expanded == true);
            CHECK(rz7_0.totalItemCount == 7);

            zenvSnap7.generation = 2u;
            zenvSnap7.items.erase(zenvSnap7.items.begin() + 4); // removes [Food] Dutch Chocolate Tin
            std::vector<HUDController::QuickContainerItemEntry> engZenvAfterLoot{
                mkCorpseItem("[SuperSledge] Spartan Junkspear", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[10mm] 10mm Pistol", 0, 0.0f, TargetInfo::ItemKind::None, 1u, false),
                mkCorpseItem("[Underarmor] Junk Legion Underarmour", 0, 0.0f, TargetInfo::ItemKind::None, 1u, true),
                mkCorpseItem("[Helm] Junk Legion Helm", 0, 0.0f, TargetInfo::ItemKind::None, 1u, true),
                mkCorpseItem("[Boots] Junk Legion Boots", 0, 0.0f, TargetInfo::ItemKind::None, 1u, true)
            };
            const auto& rz7_after = HUDController::ResolveMergedQuickContainerWindow(
                zenvCache7, 0x7003u, zenvSnap7, engZenvAfterLoot, 3, 12, true);
            CHECK(rz7_after.expanded == true);
            CHECK(rz7_after.matchedOrderIndex == 18);
            CHECK(rz7_after.totalItemCount == 6);
            CHECK(rz7_after.windowItems.size() == 6);
            CHECK(rz7_after.windowItems[0].name == "Smiling Spartan Gas Mask");
            CHECK(zenvCache7.stickyFallback == false);
        }

        // 16e. NIBBLES THE BRAWLER: single-category bottom slice of multi-category inventory + fallback icon enrichment
        {
            auto legendaryHammer = mkCorpseItem(
                "* Bloodied Hammer", 250, 12.0f, TargetInfo::ItemKind::Weapon, 1u, false);
            legendaryHammer.isLegendary = true;

            std::vector<HUDController::QuickContainerItemEntry> nibInv{
                mkCorpseItem("White Bottle", 2, 0.5f, TargetInfo::ItemKind::MiscJunk, 3u),
                mkCorpseItem("Waste M9", 90, 2.5f, TargetInfo::ItemKind::Weapon, 1u),
                mkCorpseItem("Shotgun Shell", 3, 0.0f, TargetInfo::ItemKind::Ammo, 3u),
                mkCorpseItem("Spark Plug", 50, 0.3f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Combat Rifle", 200, 11.0f, TargetInfo::ItemKind::Weapon, 1u),
                mkCorpseItem("Wallet", 9, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Plasma Cartridge", 5, 0.0f, TargetInfo::ItemKind::Ammo, 86u),
                legendaryHammer,
                mkCorpseItem("Duct Tape", 12, 0.1f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Submachine Gun", 120, 12.0f, TargetInfo::ItemKind::Weapon, 1u),
                mkCorpseItem("Radscorpion Stinger", 30, 0.5f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Click Master 2000", 123, 1.1f, TargetInfo::ItemKind::MiscJunk, 1u)
            };
            // Bottom-5 window contains ONLY MiscJunk (no "..."), while full inv spans Weapon, Ammo, MiscJunk
            std::vector<HUDController::QuickContainerItemEntry> engNibBottom{
                mkCorpseItem("Spark Plug", 50, 0.3f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Radscorpion Stinger", 30, 0.5f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Duct Tape", 12, 0.1f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Wallet", 9, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("White Bottle", 2, 0.5f, TargetInfo::ItemKind::MiscJunk, 3u)
            };
            const auto rNib = HUDController::MergeQuickContainerInventory(
                engNibBottom, 4, nibInv, 12, -1, 7);
            CHECK(rNib.expanded == true);
            CHECK(rNib.matchedOrderIndex == 17);
            CHECK(rNib.totalItemCount == 12);
            CHECK(rNib.windowItems.size() == 12);
            CHECK(rNib.windowItems[0].name == "* Bloodied Hammer");
            CHECK(rNib.windowItems[0].isLegendary == true);
            CHECK(rNib.windowItems[1].name == "Plasma Cartridge");
            CHECK(rNib.windowItems[2].name == "Shotgun Shell");
            CHECK(rNib.windowItems[3].name == "Combat Rifle");
            CHECK(rNib.windowItems[6].name == "Click Master 2000");

            // Fallback (!expanded) still enriches ItemKind icons from kindByCleanUpper (e.g., dropped Machete)
            HUDController::QuickContainerMergeCache fbCache;
            HUDController::CachedInventorySnapshot fbSnap;
            fbSnap.ownerFormID = 0x7004u;
            fbSnap.generation = 1u;
            fbSnap.items = {
                mkCorpseItem("Zeta", 10, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Machete", 25, 2.0f, TargetInfo::ItemKind::Weapon, 1u),
                mkCorpseItem("Alpha", 30, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Gamma", 15, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Beta", 5, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u),
                mkCorpseItem("Omega", 40, 1.0f, TargetInfo::ItemKind::MiscJunk, 1u)
            };
            fbSnap.kindByCleanUpper["MACHETE"] = TargetInfo::ItemKind::Weapon;
            fbSnap.kindByCleanUpper["ALPHA"] = TargetInfo::ItemKind::MiscJunk;
            std::vector<HUDController::QuickContainerItemEntry> engCustomFb{
                mkCorpseItem("[Melee] Machete", 0, 0.0f, TargetInfo::ItemKind::None, 1u),
                mkCorpseItem("Beta", 0, 0.0f, TargetInfo::ItemKind::None, 1u),
                mkCorpseItem("Omega", 0, 0.0f, TargetInfo::ItemKind::None, 1u),
                mkCorpseItem("Zeta", 0, 0.0f, TargetInfo::ItemKind::None, 1u),
                mkCorpseItem("...", 0, 0.0f, TargetInfo::ItemKind::None, 1u)
            };
            const auto& rFb = HUDController::ResolveMergedQuickContainerWindow(
                fbCache, 0x7004u, fbSnap, engCustomFb, 0, 12, true);
            CHECK(rFb.expanded == false);
            CHECK(rFb.windowItems.size() == 4);
            CHECK(rFb.windowItems[0].itemKind == TargetInfo::ItemKind::Weapon);
        }
    }

    // 17. Companion prompt distance filter, loot protection, heal/command exceptions, and HUD suppression
    {
        // 17a. IsHealActionVerb case-insensitive matching
        CHECK(HUDController::IsHealActionVerb("HEAL"));
        CHECK(HUDController::IsHealActionVerb("Heal"));
        CHECK(HUDController::IsHealActionVerb("Use Stimpak"));
        CHECK(HUDController::IsHealActionVerb("STIMPAK"));
        CHECK(HUDController::IsHealActionVerb("Revive"));
        CHECK(!HUDController::IsHealActionVerb("TALK"));
        CHECK(!HUDController::IsHealActionVerb("COMMAND"));
        CHECK(!HUDController::IsHealActionVerb("TRADE"));
        CHECK(!HUDController::IsHealActionVerb(""));

        // 17b. Disabled limit or active Command Mode restores vanilla engine settings and never suppresses
        {
            HUDController::CompanionFilterInput in{};
            in.limitEnabled = false;
            in.targetIsLiveCompanion = true;
            in.targetDistance = 400.0f;
            in.vanillaCommandPickDistance = 2000.0f;
            in.vanillaCommandPickRadius = 24.0f;
            const auto dOff = HUDController::EvaluateCompanionFilter(in);
            CHECK(!dOff.suppressTarget);
            CHECK(dOff.restoreVanillaSettings);
            CHECK_NEAR(dOff.desiredCommandPickDistance, 2000.0f, 0.01f);
            CHECK_NEAR(dOff.desiredCommandPickRadius, 24.0f, 0.01f);

            in.limitEnabled = true;
            in.inCommandMode = true;
            const auto dCmd = HUDController::EvaluateCompanionFilter(in);
            CHECK(!dCmd.suppressTarget);
            CHECK(dCmd.restoreVanillaSettings);
            CHECK_NEAR(dCmd.desiredCommandPickDistance, 2000.0f, 0.01f);
            CHECK_NEAR(dCmd.desiredCommandPickRadius, 24.0f, 0.01f);
        }

        // 17c. Out-of-combat distance filtering & deprioritize radius clamping
        {
            HUDController::CompanionFilterInput in{};
            in.limitEnabled = true;
            in.deprioritizeLoot = true;
            in.maxDistance = 160.0f;
            in.combatMaxDistance = 80.0f;
            in.vanillaCommandPickDistance = 2000.0f;
            in.vanillaCommandPickRadius = 24.0f;
            in.vanillaActivatePickRadius = 16.0f;
            in.targetIsLiveCompanion = true;

            in.targetDistance = 150.0f;
            const auto dNear = HUDController::EvaluateCompanionFilter(in);
            CHECK(!dNear.suppressTarget);
            CHECK(!dNear.restoreVanillaSettings);
            CHECK_NEAR(dNear.desiredCommandPickDistance, 160.0f, 0.01f);
            CHECK_NEAR(dNear.desiredCommandPickRadius, 16.0f, 0.01f);

            in.targetDistance = 161.0f;
            const auto dFar = HUDController::EvaluateCompanionFilter(in);
            CHECK(dFar.suppressTarget);
            CHECK_NEAR(dFar.desiredCommandPickDistance, 160.0f, 0.01f);
            CHECK_NEAR(dFar.desiredCommandPickRadius, 16.0f, 0.01f);

            // Without deprioritizeLoot, vanilla command radius is preserved
            in.deprioritizeLoot = false;
            in.targetDistance = 100.0f;
            const auto dNoDeprio = HUDController::EvaluateCompanionFilter(in);
            CHECK(!dNoDeprio.suppressTarget);
            CHECK_NEAR(dNoDeprio.desiredCommandPickRadius, 24.0f, 0.01f);
        }

        // 17d. Combat distance filtering & 0 = hide completely
        {
            HUDController::CompanionFilterInput in{};
            in.limitEnabled = true;
            in.deprioritizeLoot = true;
            in.maxDistance = 160.0f;
            in.combatMaxDistance = 80.0f;
            in.inCombat = true;
            in.targetIsLiveCompanion = true;

            in.targetDistance = 75.0f;
            CHECK(!HUDController::EvaluateCompanionFilter(in).suppressTarget);

            in.targetDistance = 90.0f;
            const auto dCombatFar = HUDController::EvaluateCompanionFilter(in);
            CHECK(dCombatFar.suppressTarget);
            CHECK_NEAR(dCombatFar.desiredCommandPickDistance, 80.0f, 0.01f);

            // Combat max distance = 0 hides even at point-blank range
            in.combatMaxDistance = 0.0f;
            in.targetDistance = 20.0f;
            const auto dCombatZero = HUDController::EvaluateCompanionFilter(in);
            CHECK(dCombatZero.suppressTarget);
            CHECK_NEAR(dCombatZero.desiredCommandPickDistance, 0.0f, 0.01f);
            CHECK_NEAR(dCombatZero.desiredCommandPickRadius, 0.0f, 0.01f);
        }

        // 17e. Exceptions: bleedout, heal verb, and non-companion (dead/container) are NEVER suppressed
        {
            HUDController::CompanionFilterInput in{};
            in.limitEnabled = true;
            in.deprioritizeLoot = true;
            in.inCombat = true;
            in.combatMaxDistance = 0.0f;
            in.quickLootProtected = true;
            in.targetIsLiveCompanion = true;
            in.targetDistance = 140.0f;

            in.targetIsBleedout = true;
            CHECK(!HUDController::EvaluateCompanionFilter(in).suppressTarget);

            in.targetIsBleedout = false;
            in.targetHasHealVerb = true;
            CHECK(!HUDController::EvaluateCompanionFilter(in).suppressTarget);

            in.targetHasHealVerb = false;
            in.targetIsLiveCompanion = false; // container or dead companion corpse
            CHECK(!HUDController::EvaluateCompanionFilter(in).suppressTarget);
        }

        // 17f. QuickLoot protection & grace window transition state machine
        {
            std::int64_t lastQlMs = 0;
            // Looting a container refreshes timestamp and enables protection
            CHECK(HUDController::UpdateQuickLootCompanionProtection(lastQlMs, true, false, 1000, 600));
            CHECK(lastQlMs == 1000);

            // Transition frame: QuickContainer still visible while PickRef flips to companion -> MUST stay protected!
            CHECK(HUDController::UpdateQuickLootCompanionProtection(lastQlMs, true, true, 1050, 600));

            // Next frames after QuickContainer hides: protected within 600ms, expires after 600ms
            CHECK(HUDController::UpdateQuickLootCompanionProtection(lastQlMs, false, true, 1600, 600));
            CHECK(!HUDController::UpdateQuickLootCompanionProtection(lastQlMs, false, true, 1601, 600));

            // Looking away at non-companion resets grace timestamp immediately
            CHECK(HUDController::UpdateQuickLootCompanionProtection(lastQlMs, true, false, 2000, 600));
            CHECK(!HUDController::UpdateQuickLootCompanionProtection(lastQlMs, false, false, 2050, 600));
            CHECK(lastQlMs == 0);
            CHECK(!HUDController::UpdateQuickLootCompanionProtection(lastQlMs, false, true, 2060, 600));

            HUDController::CompanionFilterInput in{};
            in.limitEnabled = true;
            in.deprioritizeLoot = true;
            in.maxDistance = 160.0f;
            in.quickLootProtected = true;
            in.targetIsLiveCompanion = true;
            in.targetDistance = 50.0f;
            const auto dLoot = HUDController::EvaluateCompanionFilter(in);
            CHECK(dLoot.suppressTarget);
            CHECK_NEAR(dLoot.desiredCommandPickDistance, 0.0f, 0.01f);
            CHECK_NEAR(dLoot.desiredCommandPickRadius, 0.0f, 0.01f);
        }

        // 17g. HUDTransformState::Step suppressTarget handling in Vanilla, Modern, and PowerArmor bypass modes
        {
            HUDController::HUDTransformState st;
            HUDController::HUDFrameInput in{};
            in.enabled = true;
            in.rolloverVisible = true;
            in.nativeClipVisible = true;
            in.hasTarget = true;
            in.currentClipX = 640.0f;
            in.currentClipY = 360.0f;
            in.projectedTarget = { 450.0f, 220.0f, 1.0f, true };
            in.smoothingSpeed = 1.0f;

            // Float normally at 150u, then step back to 165u -> suppressTarget restores baseline pos & hides alpha
            const auto floatOut = st.Step(in);
            CHECK(floatOut.action == HUDController::HUDAction::kApplyFloat);

            in.suppressTarget = true;
            const auto supOut = st.Step(in);
            CHECK(supOut.action == HUDController::HUDAction::kSuppressNative);
            CHECK(supOut.applyPosition);
            CHECK(supOut.applyAlpha);
            CHECK_NEAR(supOut.x, 640.0f, 0.01f);
            CHECK_NEAR(supOut.y, 360.0f, 0.01f);
            CHECK_NEAR(supOut.alpha, 0.0f, 0.001f);
            CHECK(st.IsHidingAlpha());

            // Walk back within range -> alpha restores to 1.0
            in.suppressTarget = false;
            const auto resumeOut = st.Step(in);
            CHECK(resumeOut.action == HUDController::HUDAction::kApplyFloat);
            CHECK(resumeOut.applyAlpha);
            CHECK_NEAR(resumeOut.alpha, 1.0f, 0.001f);
            CHECK(!st.IsHidingAlpha());

            // Power Armor bypass mode: suppressTarget STILL hides alpha, then restores alpha = 1.0 when unsuppressed
            HUDController::HUDTransformState paSt;
            HUDController::HUDFrameInput paIn{};
            paIn.enabled = true;
            paIn.inPowerArmor = true;
            paIn.powerArmorBypass = true;
            paIn.rolloverVisible = true;
            paIn.nativeClipVisible = true;
            paIn.hasTarget = true;
            paIn.suppressTarget = true;
            const auto paSup = paSt.Step(paIn);
            CHECK(paSup.action == HUDController::HUDAction::kSuppressNative);
            CHECK(paSup.applyAlpha);
            CHECK_NEAR(paSup.alpha, 0.0f, 0.001f);

            paIn.suppressTarget = false;
            const auto paRestore = paSt.Step(paIn);
            CHECK(paRestore.action == HUDController::HUDAction::kRestoreBaseline);
            CHECK(paRestore.applyAlpha);
            CHECK_NEAR(paRestore.alpha, 1.0f, 0.001f);
            CHECK(!paSt.IsHidingAlpha());
        }
    }

    // 19. Crime verb detection & post-loot anti-grab / empty-container Activate guards
    {
        // 19a. IsCrimeActionVerb
        CHECK(HUDController::IsCrimeActionVerb("STEAL"));
        CHECK(HUDController::IsCrimeActionVerb("Steal"));
        CHECK(HUDController::IsCrimeActionVerb("  STEAL FROM  "));
        CHECK(HUDController::IsCrimeActionVerb("PICKPOCKET"));
        CHECK(HUDController::IsCrimeActionVerb("Pickpocket Raider"));
        CHECK(HUDController::IsCrimeActionVerb("TRESPASS"));
        CHECK(!HUDController::IsCrimeActionVerb("TAKE"));
        CHECK(!HUDController::IsCrimeActionVerb("EAT"));
        CHECK(!HUDController::IsCrimeActionVerb("TALK"));
        CHECK(!HUDController::IsCrimeActionVerb("STEALTH"));
        CHECK(!HUDController::IsCrimeActionVerb("STEEL"));
        CHECK(!HUDController::IsCrimeActionVerb(""));

        // 19b. ShouldBlockPostLootActivate
        CHECK(!HUDController::ShouldBlockPostLootActivate(1000, 0, 0, true, true, false));
        CHECK(!HUDController::ShouldBlockPostLootActivate(1000, 0, 0, false, true, false));

        // Spam tap allowed after non-empty take, held frame blocked within 700ms
        CHECK(!HUDController::ShouldBlockPostLootActivate(1300, 1000, 0, true, true, false));
        CHECK(HUDController::ShouldBlockPostLootActivate(1300, 1000, 0, false, true, false));
        CHECK(HUDController::ShouldBlockPostLootActivate(1700, 1000, 0, false, true, false));
        CHECK(!HUDController::ShouldBlockPostLootActivate(1701, 1000, 0, false, true, false));

        // Container just emptied -> block both tap and hold within 450ms
        CHECK(HUDController::ShouldBlockPostLootActivate(1200, 1000, 1000, true, true, false));
        CHECK(HUDController::ShouldBlockPostLootActivate(1450, 1000, 1000, true, true, false));
        CHECK(!HUDController::ShouldBlockPostLootActivate(1451, 1000, 1000, true, true, false));

        // Already grabbing -> never block (so player can still release/drop cleanly)
        CHECK(!HUDController::ShouldBlockPostLootActivate(1100, 1000, 1000, false, true, true));

        // Negative delta / out-of-order clock safety
        CHECK(!HUDController::ShouldBlockPostLootActivate(900, 1000, 1000, false, true, false));
    }

    // 18. Custom QuickLoot configurable sort modes, Top Pins, category priority, NaN safety, and selection clamp
    {
        auto mkCustomItem = [](
            std::string_view a_name,
            QuickLootCategory a_cat,
            std::uint32_t a_value,
            float a_weight,
            std::uint32_t a_formId = 0x1000u,
            bool a_quest = false,
            bool a_legendary = false,
            bool a_tagged = false,
            bool a_caps = false)
        {
            HUDController::QuickContainerItemEntry e;
            e.name = std::string(a_name);
            e.baseFormID = a_formId;
            e.quickLootCategory = a_cat;
            e.value = a_value;
            e.weight = a_weight;
            e.isQuestItem = a_quest;
            e.isLegendary = a_legendary;
            e.isTaggedForSearch = a_tagged;
            e.isCaps = a_caps;
            return e;
        };

        // 18a. Top Pin precedence: Quest > Legendary > Tagged for Search > Caps > Normal (and individual toggles)
        {
            std::vector<HUDController::QuickContainerItemEntry> base{
                mkCustomItem("Bottlecap", QuickLootCategory::Misc, 1, 0.0f, 0x0000000Fu, false, false, false, true),
                mkCustomItem("Desk Fan", QuickLootCategory::Junk, 22, 3.0f, 0x2001u, false, false, true, false),
                mkCustomItem("* Furious Pipe Pistol", QuickLootCategory::Pistol, 150, 3.0f, 0x2002u, false, true, false, false),
                mkCustomItem("Holotape Evidence", QuickLootCategory::Holotape, 0, 0.0f, 0x2003u, true, false, false, false),
                mkCustomItem("Vault 111 Key", QuickLootCategory::Key, 0, 0.0f, 0x2004u, false, false, false, false)
            };

            auto allPins = base;
            HUDController::QuickLootSortConfig cfg;
            cfg.sortMode = 0;
            cfg.pinQuestItems = true;
            cfg.pinLegendary = true;
            cfg.pinTaggedForSearch = true;
            cfg.pinCaps = true;
            HUDController::SortQuickContainerItems(allPins, cfg);
            CHECK(allPins[0].name == "Holotape Evidence");
            CHECK(allPins[1].name == "* Furious Pipe Pistol");
            CHECK(allPins[2].name == "Desk Fan");
            CHECK(allPins[3].name == "Bottlecap");
            CHECK(allPins[4].name == "Vault 111 Key");

            // Disable Caps & Quest pins -> Legendary & Tagged stay pinned, Vault Key & Holotape follow category order (Key before Holotape before Misc)
            auto partialPins = base;
            cfg.pinQuestItems = false;
            cfg.pinCaps = false;
            HUDController::SortQuickContainerItems(partialPins, cfg);
            CHECK(partialPins[0].name == "* Furious Pipe Pistol");
            CHECK(partialPins[1].name == "Desk Fan");
            CHECK(partialPins[2].name == "Vault 111 Key");
            CHECK(partialPins[3].name == "Holotape Evidence");
            CHECK(partialPins[4].name == "Bottlecap");
        }

        // 18b. Custom draggable category priority order + secondary sort options (Value Desc, Val/Wt Desc, Alpha A-Z, Weight Asc)
        {
            std::vector<HUDController::QuickContainerItemEntry> sample{
                mkCustomItem("Laser Pistol", QuickLootCategory::Pistol, 80, 4.0f, 0x3001u),
                mkCustomItem("Combat Rifle", QuickLootCategory::Rifle, 200, 12.0f, 0x3002u),
                mkCustomItem("Wrench", QuickLootCategory::Junk, 10, 2.0f, 0x3003u),
                mkCustomItem("Aluminum Can", QuickLootCategory::Junk, 15, 0.1f, 0x3004u),
                mkCustomItem("Dungeon Key", QuickLootCategory::Key, 0, 0.0f, 0x3005u),
                mkCustomItem("Reflex Sight Mod", QuickLootCategory::Mod, 90, 0.5f, 0x3006u),
                mkCustomItem("Submachine Pistol", QuickLootCategory::Pistol, 120, 3.0f, 0x3007u)
            };

            // Put Junk first, then Mod, then Pistol, then Rifle, then Key; Secondary = Value/Weight DESC
            auto items = sample;
            HUDController::QuickLootSortConfig cfg;
            cfg.sortMode = 0;
            cfg.categorySecondarySort = 1; // Value/Weight DESC
            cfg.categoryOrder = {
                static_cast<int>(QuickLootCategory::Junk),
                static_cast<int>(QuickLootCategory::Mod),
                static_cast<int>(QuickLootCategory::Pistol),
                static_cast<int>(QuickLootCategory::Rifle),
                static_cast<int>(QuickLootCategory::Key),
                static_cast<int>(QuickLootCategory::Medicine),
                static_cast<int>(QuickLootCategory::Chem),
                static_cast<int>(QuickLootCategory::Drink),
                static_cast<int>(QuickLootCategory::Food),
                static_cast<int>(QuickLootCategory::Ammo),
                static_cast<int>(QuickLootCategory::Explosive),
                static_cast<int>(QuickLootCategory::HeavyWeapon),
                static_cast<int>(QuickLootCategory::Melee),
                static_cast<int>(QuickLootCategory::PowerArmor),
                static_cast<int>(QuickLootCategory::Armor),
                static_cast<int>(QuickLootCategory::Clothing),
                static_cast<int>(QuickLootCategory::BookNote),
                static_cast<int>(QuickLootCategory::Holotape),
                static_cast<int>(QuickLootCategory::Misc)
            };
            HUDController::SortQuickContainerItems(items, cfg);
            CHECK(items[0].name == "Aluminum Can");      // Junk, 15/0.1 = 150
            CHECK(items[1].name == "Wrench");            // Junk, 10/2 = 5
            CHECK(items[2].name == "Reflex Sight Mod");
            CHECK(items[3].name == "Submachine Pistol"); // Pistol, 120/3 = 40
            CHECK(items[4].name == "Laser Pistol");      // Pistol, 80/4 = 20
            CHECK(items[5].name == "Combat Rifle");      // Rifle (sub-ordered after Pistol)
            CHECK(items[6].name == "Dungeon Key");

            // Secondary = Alphabetical A-Z within categories
            items = sample;
            cfg.categorySecondarySort = 2;
            HUDController::SortQuickContainerItems(items, cfg);
            CHECK(items[0].name == "Aluminum Can");
            CHECK(items[1].name == "Wrench");
            CHECK(items[3].name == "Laser Pistol");
            CHECK(items[4].name == "Submachine Pistol");

            // Secondary = Weight ASC within categories
            items = sample;
            cfg.categorySecondarySort = 3;
            HUDController::SortQuickContainerItems(items, cfg);
            CHECK(items[3].name == "Submachine Pistol"); // 3.0 < 4.0
            CHECK(items[4].name == "Laser Pistol");
        }

        // 18c. Global Sort Modes 1..5, NaN weight sanitization, and Mode 6 (Vanilla no-op)
        {
            const float qNan = std::numeric_limits<float>::quiet_NaN();
            std::vector<HUDController::QuickContainerItemEntry> base{
                mkCustomItem("[Tag] Heavy Brick", QuickLootCategory::Junk, 40, 20.0f, 0x4001u),
                mkCustomItem("Weightless Gem", QuickLootCategory::Misc, 100, 0.0f, 0x4002u),
                mkCustomItem("Alpha Pill", QuickLootCategory::Medicine, 50, 0.2f, 0x4003u),
                mkCustomItem("Zero Val Note", QuickLootCategory::BookNote, 0, qNan, 0x4004u)
            };

            // Mode 1: Value DESC
            auto m1 = base;
            HUDController::QuickLootSortConfig cfg;
            cfg.sortMode = 1;
            HUDController::SortQuickContainerItems(m1, cfg);
            CHECK(m1[0].name == "Weightless Gem");
            CHECK(m1[1].name == "Alpha Pill");
            CHECK(m1[2].name == "[Tag] Heavy Brick");
            CHECK(m1[3].name == "Zero Val Note");

            // Mode 2: Value/Weight DESC (Weightless valuable > finite > 0 value NaN weight)
            auto m2 = base;
            cfg.sortMode = 2;
            HUDController::SortQuickContainerItems(m2, cfg);
            CHECK(m2[0].name == "Weightless Gem");
            CHECK(m2[1].name == "Alpha Pill");        // 250.0
            CHECK(m2[2].name == "[Tag] Heavy Brick"); // 2.0
            CHECK(m2[3].name == "Zero Val Note");

            // Mode 3: Alphabetical A-Z (clean display name strips [Tag] -> "Alpha Pill" < "Heavy Brick" < "Weightless Gem" < "Zero Val Note")
            auto m3 = base;
            cfg.sortMode = 3;
            HUDController::SortQuickContainerItems(m3, cfg);
            CHECK(m3[0].name == "Alpha Pill");
            CHECK(m3[1].name == "[Tag] Heavy Brick");
            CHECK(m3[2].name == "Weightless Gem");
            CHECK(m3[3].name == "Zero Val Note");

            // Mode 4: Weight ASC
            auto m4 = base;
            cfg.sortMode = 4;
            HUDController::SortQuickContainerItems(m4, cfg);
            CHECK(m4[0].weight <= 0.0f);
            CHECK(m4[3].name == "[Tag] Heavy Brick");

            // Mode 5: Weight DESC
            auto m5 = base;
            cfg.sortMode = 5;
            HUDController::SortQuickContainerItems(m5, cfg);
            CHECK(m5[0].name == "[Tag] Heavy Brick");
            CHECK(m5[1].name == "Alpha Pill");

            // Mode 6: Vanilla Passive Mirror preserves input sequence untouched
            auto m6 = base;
            cfg.sortMode = 6;
            HUDController::SortQuickContainerItems(m6, cfg);
            CHECK(m6[0].name == "[Tag] Heavy Brick");
            CHECK(m6[3].name == "Zero Val Note");
        }

        // 18d. StepCustomQuickLootSelection clamping (no wrap-around) & AggregateItemStacks separation
        {
            CHECK(HUDController::StepCustomQuickLootSelection(0, -1, 5) == 0);
            CHECK(HUDController::StepCustomQuickLootSelection(4, 1, 5) == 4);
            CHECK(HUDController::StepCustomQuickLootSelection(2, 1, 5) == 3);
            CHECK(HUDController::StepCustomQuickLootSelection(2, -1, 5) == 1);
            CHECK(HUDController::StepCustomQuickLootSelection(9, 0, 4) == 3);
            CHECK(HUDController::StepCustomQuickLootSelection(2, 1, 0) == 0);

            auto a = mkCustomItem("Knife", QuickLootCategory::Melee, 20, 1.0f, 0x5001u);
            auto b = mkCustomItem("Knife", QuickLootCategory::Melee, 20, 1.0f, 0x5001u);
            b.isTemporary = true;
            auto c = mkCustomItem("Knife", QuickLootCategory::Melee, 20, 1.0f, 0x5002u);
            std::vector<HUDController::QuickContainerItemEntry> stacks{ a, b, c };
            HUDController::AggregateItemStacks(stacks);
            CHECK(stacks.size() == 3);
        }
    }

    // 20. Custom QuickLoot single-item-per-physical-press latch & 80ms debounce
    {
        auto mkTakeIn = [](
            std::uint32_t dev,
            std::uint32_t id,
            bool isFamily,
            bool isJust,
            bool isPressed,
            bool isRel,
            bool canTake,
            bool suppressPress = false)
        {
            HUDController::QuickLootTakeInput in{};
            in.device = dev;
            in.idCode = id;
            in.isTakeFamily = isFamily;
            in.isJustPressed = isJust;
            in.isPressed = isPressed;
            in.isReleased = isRel;
            in.canTake = canTake;
            in.suppressTakeFamilyPress = suppressPress;
            return in;
        };

        HUDController::QuickLootTakeLatchState st{};
        HUDController::QuickLootTakePassState pass{};

        // First clean press takes 1 item and latches
        auto d = HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 1000);
        CHECK(d.acceptTake == true);
        CHECK(d.swallowEvent == true);
        CHECK(st.latched == true);

        // Simulated engine RemoveItem reset emitting QJustPressed again 16ms later while still down
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 1016);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == true);
        CHECK(st.latched == true);

        // Continuous physical hold past 1500ms keeps latch alive via heartbeat (never double-takes)
        for (std::int64_t t = 1500; t <= 3500; t += 500) {
            pass = {};
            d = HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 18, true, false, true, false, true, true), t);
            CHECK(d.acceptTake == false);
            CHECK(d.swallowEvent == true);
            CHECK(st.latched == true);
        }
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 3516);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == true);
        CHECK(st.latched == true);

        // Unrelated key release must NOT unlatch bound key (0, 18)
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 32, false, false, false, true, false, false), 3530);
        CHECK(d.swallowEvent == false);
        CHECK(st.latched == true);

        // Multi-event release in same pass: both QCTakeItem release and Activate release are swallowed
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 18, true, false, false, true, false, false), 3550);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == true);
        CHECK(st.latched == false);
        d = HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 18, true, false, false, true, false, false), 3550);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == true);

        // Press order: Activate arriving BEFORE QCTakeItem in same pass is swallowed without blocking QCTakeItem
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 18, true, true, true, false, false, true), 3700);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == true);
        CHECK(st.latched == false);
        d = HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 3700);
        CHECK(d.acceptTake == true);
        CHECK(d.swallowEvent == true);
        CHECK(st.latched == true);
        pass = {};
        (void)HUDController::StepQuickLootTakeLatch(st, pass, mkTakeIn(0, 18, true, false, false, true, false, false), 3740);

        // Debounce test: bounce press 40ms after a fresh take is suppressed & re-latched
        HUDController::QuickLootTakeLatchState bounceSt{};
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(bounceSt, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 5000);
        CHECK(d.acceptTake == true);
        pass = {};
        (void)HUDController::StepQuickLootTakeLatch(bounceSt, pass, mkTakeIn(0, 18, true, false, false, true, false, false), 5020);
        CHECK(bounceSt.latched == false);
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(bounceSt, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 5040);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == true);
        CHECK(bounceSt.latched == true);

        // After clean release and >= 100ms quiet time since release (and press), next press succeeds
        pass = {};
        (void)HUDController::StepQuickLootTakeLatch(bounceSt, pass, mkTakeIn(0, 18, true, false, false, true, false, false), 5070);
        CHECK(bounceSt.latched == false);
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(bounceSt, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 5170);
        CHECK(d.acceptTake == true);
        CHECK(d.swallowEvent == true);
        CHECK(bounceSt.latched == true);

        // Mechanical switch release-chatter after a normal 150ms hold (> 100ms since press):
        // pressing at 1000ms, releasing at 1150ms, chatter at 1155ms/1165ms are ALL rejected;
        // clean press 100ms after final settle (1270ms) succeeds!
        HUDController::QuickLootTakeLatchState mechSt{};
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(mechSt, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 1000);
        CHECK(d.acceptTake == true);
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(mechSt, pass, mkTakeIn(0, 18, true, false, false, true, false, false), 1150);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == true);
        CHECK(mechSt.latched == false);
        CHECK(mechSt.lastReleaseMs == 1150);

        pass = {};
        d = HUDController::StepQuickLootTakeLatch(mechSt, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 1155);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == true);
        CHECK(mechSt.latched == true);

        pass = {};
        d = HUDController::StepQuickLootTakeLatch(mechSt, pass, mkTakeIn(0, 18, true, false, false, true, false, false), 1159);
        CHECK(mechSt.latched == false);
        CHECK(mechSt.lastReleaseMs == 1159);

        pass = {};
        d = HUDController::StepQuickLootTakeLatch(mechSt, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 1165);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == true);
        CHECK(mechSt.latched == true);

        pass = {};
        d = HUDController::StepQuickLootTakeLatch(mechSt, pass, mkTakeIn(0, 18, true, false, false, true, false, false), 1170);
        CHECK(mechSt.latched == false);
        CHECK(mechSt.lastReleaseMs == 1170);

        pass = {};
        d = HUDController::StepQuickLootTakeLatch(mechSt, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 1270);
        CHECK(d.acceptTake == true);
        CHECK(d.swallowEvent == true);
        CHECK(mechSt.latched == true);

        // Zero debounce (0 ms) disables both press and post-release cooldowns
        HUDController::QuickLootTakeLatchState zeroDb{};
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(zeroDb, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 2000, 0);
        CHECK(d.acceptTake == true);
        pass = {};
        (void)HUDController::StepQuickLootTakeLatch(zeroDb, pass, mkTakeIn(0, 18, true, false, false, true, false, false), 2010, 0);
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(zeroDb, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 2015, 0);
        CHECK(d.acceptTake == true);

        // Stale latch recovery when key-up was lost (no held frames for >= 1500ms)
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(bounceSt, pass, mkTakeIn(0, 18, true, true, true, false, true, true), 7000);
        CHECK(d.acceptTake == true);
        CHECK(d.swallowEvent == true);

        // Activate while not latched and not suppressed does not take or swallow
        HUDController::QuickLootTakeLatchState unlatched{};
        pass = {};
        d = HUDController::StepQuickLootTakeLatch(unlatched, pass, mkTakeIn(0, 18, true, true, true, false, false, false), 8000);
        CHECK(d.acceptTake == false);
        CHECK(d.swallowEvent == false);
    }

    // 21. Suppressed companion & orphan SecondaryActivate -> ReadyWeapon (Draw/Reload/Sheath)
    {
        // SecondaryActivate tap when companion is suppressed and weapon drawn -> Reload on release
        HUDController::SuppressedReadyState st{};
        HUDController::SuppressedReadyInput in{};
        in.isSecondaryActivate = true;
        in.isJustPressed = true;
        in.isPressed = true;
        in.blockCompanionReady = true;
        in.canCustomReadyAction = true;
        in.weaponDrawn = true;

        auto dec = HUDController::StepSuppressedSecondaryReady(st, in, 1000);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.consumedPass == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);
        CHECK(st.held == true);

        // Release arrives on ReadyWeapon because crosshair moved away -> still finishes Reload when drawn
        HUDController::SuppressedReadyInput relIn{};
        relIn.isReadyWeapon = true;
        relIn.isReleased = true;
        relIn.canCustomReadyAction = true;
        relIn.weaponDrawn = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, relIn, 1080);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.consumedPass == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kReload);
        CHECK(st.held == false);

        // Holstered (weaponDrawn == false) short tap R -> swallowed, does NOT draw on tap
        st = {};
        HUDController::SuppressedReadyInput holsteredPress{};
        holsteredPress.isSecondaryActivate = true;
        holsteredPress.isJustPressed = true;
        holsteredPress.isPressed = true;
        holsteredPress.blockCompanionReady = true;
        holsteredPress.canCustomReadyAction = true;
        holsteredPress.weaponDrawn = false;
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredPress, 1500);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.consumedPass == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);
        CHECK(st.held == true);

        HUDController::SuppressedReadyInput holsteredTapRel{};
        holsteredTapRel.isSecondaryActivate = true;
        holsteredTapRel.isReleased = true;
        holsteredTapRel.blockCompanionReady = true;
        holsteredTapRel.canCustomReadyAction = true;
        holsteredTapRel.weaponDrawn = false;
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredTapRel, 1600);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.consumedPass == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);
        CHECK(st.held == false);

        // Holstered (weaponDrawn == false) hold R (>= 0.35s) -> draws weapon on hold; further hold/release emit kNone
        st = {};
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredPress, 1700);
        CHECK(dec.swallowEvent == true);
        HUDController::SuppressedReadyInput holsteredHold = holsteredPress;
        holsteredHold.isJustPressed = false;
        holsteredHold.heldDownSecs = 0.40f;
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredHold, 2100);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.consumedPass == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kDraw);
        CHECK(st.holstered == true);

        holsteredHold.heldDownSecs = 0.50f;
        holsteredHold.weaponDrawn = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredHold, 2200);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);
        CHECK(st.holstered == true);

        holsteredTapRel.weaponDrawn = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredTapRel, 2300);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);
        CHECK(st.held == false);
        CHECK(st.holstered == false);

        // Holstered hold R (>= 0.35s) with canCustomReadyAction == false -> sets holstered flag, returns kNone
        st = {};
        HUDController::SuppressedReadyInput holsteredNoActPress = holsteredPress;
        holsteredNoActPress.blockOrphanSecondary = true;
        holsteredNoActPress.blockCompanionReady = false;
        holsteredNoActPress.canCustomReadyAction = false;
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredNoActPress, 2400);
        CHECK(dec.swallowEvent == true);
        CHECK(st.held == false);

        // Simulated state where st.held was set before canCustomReadyAction became false
        st = {};
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredPress, 2450);
        CHECK(st.held == true);
        HUDController::SuppressedReadyInput holsteredHoldNoAct = holsteredPress;
        holsteredHoldNoAct.isJustPressed = false;
        holsteredHoldNoAct.heldDownSecs = 0.40f;
        holsteredHoldNoAct.canCustomReadyAction = false;
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredHoldNoAct, 2850);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);
        CHECK(st.holstered == true);
        holsteredTapRel.canCustomReadyAction = false;
        dec = HUDController::StepSuppressedSecondaryReady(st, holsteredTapRel, 2900);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);
        CHECK(st.held == false);

        // Hold >= 0.35s with weapon drawn triggers Sheath, then release emits kNone
        st = {};
        dec = HUDController::StepSuppressedSecondaryReady(st, in, 2000);
        CHECK(dec.swallowEvent == true);
        HUDController::SuppressedReadyInput holdIn = in;
        holdIn.isJustPressed = false;
        holdIn.heldDownSecs = 0.40f;
        dec = HUDController::StepSuppressedSecondaryReady(st, holdIn, 2400);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kSheath);
        CHECK(st.holstered == true);
        HUDController::SuppressedReadyInput relDrawn{};
        relDrawn.isSecondaryActivate = true;
        relDrawn.isReleased = true;
        relDrawn.canCustomReadyAction = true;
        relDrawn.weaponDrawn = false;
        dec = HUDController::StepSuppressedSecondaryReady(st, relDrawn, 2500);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);
        CHECK(st.held == false);

        // Orphan SecondaryActivate when no prompt is visible -> blocked & converted to Reload
        st = {};
        HUDController::SuppressedReadyInput orphanPress{};
        orphanPress.isSecondaryActivate = true;
        orphanPress.isJustPressed = true;
        orphanPress.isPressed = true;
        orphanPress.blockOrphanSecondary = true;
        orphanPress.passHasBlockedSecondary = true;
        orphanPress.canCustomReadyAction = true;
        orphanPress.weaponDrawn = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, orphanPress, 3000);
        CHECK(dec.swallowEvent == true);
        CHECK(st.held == true);

        HUDController::SuppressedReadyInput orphanRel{};
        orphanRel.isSecondaryActivate = true;
        orphanRel.isReleased = true;
        orphanRel.blockOrphanSecondary = true;
        orphanRel.canCustomReadyAction = true;
        orphanRel.weaponDrawn = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, orphanRel, 3060);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kReload);
        CHECK(st.held == false);

        // Same-pass ReadyWeapon before orphan SecondaryActivate does not double-fire on press or release
        st = {};
        HUDController::SuppressedReadyInput readyFirst{};
        readyFirst.isReadyWeapon = true;
        readyFirst.isJustPressed = true;
        readyFirst.isPressed = true;
        readyFirst.blockOrphanSecondary = true;
        readyFirst.passHasBlockedSecondary = true;
        readyFirst.canCustomReadyAction = true;
        readyFirst.weaponDrawn = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, readyFirst, 3500);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.consumedPass == true);
        CHECK(st.held == true);
        orphanPress.alreadyHandledThisPass = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, orphanPress, 3500);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.consumedPass == false);
        // Same-pass release: SecondaryActivate release fires Reload once, follow-up ReadyWeapon release is swallowed
        orphanRel.alreadyHandledThisPass = false;
        dec = HUDController::StepSuppressedSecondaryReady(st, orphanRel, 3550);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.consumedPass == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kReload);
        CHECK(st.held == false);
        HUDController::SuppressedReadyInput readyFollowupRel{};
        readyFollowupRel.isReadyWeapon = true;
        readyFollowupRel.isReleased = true;
        readyFollowupRel.alreadyHandledThisPass = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, readyFollowupRel, 3550);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);

        // Even when canCustomReadyAction == false, blocked SecondaryActivate is STILL swallowed
        st = {};
        orphanPress.alreadyHandledThisPass = false;
        orphanPress.canCustomReadyAction = false;
        dec = HUDController::StepSuppressedSecondaryReady(st, orphanPress, 4000);
        CHECK(dec.swallowEvent == true);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);
        CHECK(st.held == false);

        // Normal SecondaryActivate when prompt IS visible is NOT blocked
        HUDController::SuppressedReadyInput visiblePrompt{};
        visiblePrompt.isSecondaryActivate = true;
        visiblePrompt.isJustPressed = true;
        visiblePrompt.isPressed = true;
        visiblePrompt.canCustomReadyAction = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, visiblePrompt, 5000);
        CHECK(dec.swallowEvent == false);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);

        // Normal ReadyWeapon in empty air (blockOrphanSecondary == true, blockCompanionReady == false) is untouched
        HUDController::SuppressedReadyInput airReady{};
        airReady.isReadyWeapon = true;
        airReady.isJustPressed = true;
        airReady.isPressed = true;
        airReady.blockOrphanSecondary = true;
        airReady.canCustomReadyAction = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, airReady, 6000);
        CHECK(dec.swallowEvent == false);
        CHECK(dec.action == HUDController::SuppressedReadyAction::kNone);

        // QCOpenTransferMenu is blocked only when companion Ready is suppressed or held
        st = {};
        HUDController::SuppressedReadyInput qcTrans{};
        qcTrans.isQcTransfer = true;
        qcTrans.isJustPressed = true;
        qcTrans.isPressed = true;
        qcTrans.blockOrphanSecondary = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, qcTrans, 8000);
        CHECK(dec.swallowEvent == false);
        qcTrans.blockCompanionReady = true;
        dec = HUDController::StepSuppressedSecondaryReady(st, qcTrans, 8001);
        CHECK(dec.swallowEvent == true);
    }

    // 22. Granular QuickLoot subcategories & Power Armor workbench exclusion
    {
        // Weapon subcategories
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(10, false, false, false, false, false, false) == QuickLootCategory::Explosive);
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(11, false, false, false, false, false, false) == QuickLootCategory::Explosive);
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(9, true, false, false, false, false, false) == QuickLootCategory::Explosive);
        // Fat Man / Missile Launcher have BOTH HeavyGun + Explosive -> HeavyWeapon
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(9, false, true, true, false, false, false) == QuickLootCategory::HeavyWeapon);
        // Pure explosive gun without HeavyGun -> Explosive
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(9, false, false, true, false, false, false) == QuickLootCategory::Explosive);
        // Melee WEAPON_TYPE (0..6) or melee keyword -> Melee
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(0, false, false, false, false, false, false) == QuickLootCategory::Melee);
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(5, false, false, false, false, false, false) == QuickLootCategory::Melee);
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(9, false, false, false, true, false, false) == QuickLootCategory::Melee);
        // Pistol vs Rifle/Shotgun vs fallback
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(9, false, false, false, false, true, false) == QuickLootCategory::Pistol);
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(9, false, false, false, false, false, true) == QuickLootCategory::Rifle);
        CHECK(HUDController::ClassifyWeaponQuickLootCategory(9, false, false, false, false, false, false) == QuickLootCategory::Rifle);

        // Armor subcategories
        CHECK(HUDController::ClassifyArmorQuickLootCategory(true, true, false, 120, true) == QuickLootCategory::PowerArmor);
        CHECK(HUDController::ClassifyArmorQuickLootCategory(false, true, false, 15, false) == QuickLootCategory::Armor);
        CHECK(HUDController::ClassifyArmorQuickLootCategory(false, false, false, 10, false) == QuickLootCategory::Armor);
        CHECK(HUDController::ClassifyArmorQuickLootCategory(false, false, true, 5, false) == QuickLootCategory::Armor);
        CHECK(HUDController::ClassifyArmorQuickLootCategory(false, false, false, 0, true) == QuickLootCategory::Armor);
        CHECK(HUDController::ClassifyArmorQuickLootCategory(false, false, true, 0, false) == QuickLootCategory::Clothing);
        CHECK(HUDController::ClassifyArmorQuickLootCategory(false, false, false, 0, false) == QuickLootCategory::Clothing);

        // Aid subcategories
        CHECK(HUDController::ClassifyAidQuickLootCategory(true, false, false, false, false, false, false) == QuickLootCategory::Medicine);
        CHECK(HUDController::ClassifyAidQuickLootCategory(false, false, true, false, false, false, false) == QuickLootCategory::Medicine);
        // Alcohol has addiction flag/item AND drink keyword -> must classify as Drink, not Chem
        CHECK(HUDController::ClassifyAidQuickLootCategory(false, true, false, false, true, false, true) == QuickLootCategory::Drink);
        CHECK(HUDController::ClassifyAidQuickLootCategory(false, true, false, false, true, false, false) == QuickLootCategory::Drink);
        CHECK(HUDController::ClassifyAidQuickLootCategory(false, true, false, true, false, false, true) == QuickLootCategory::Chem);
        CHECK(HUDController::ClassifyAidQuickLootCategory(false, false, false, false, false, false, true) == QuickLootCategory::Chem);
        CHECK(HUDController::ClassifyAidQuickLootCategory(false, true, false, false, false, true, false) == QuickLootCategory::Food);
        CHECK(HUDController::ClassifyAidQuickLootCategory(false, true, false, false, false, false, false) == QuickLootCategory::Food);
        CHECK(HUDController::ClassifyAidQuickLootCategory(false, false, false, false, false, false, false) == QuickLootCategory::Medicine);

        // Power Armor furniture exclusion vs Workbenches
        CHECK(HUDController::IsQuickContainerWorkbenchTarget(TargetInfo::ItemKind::Furniture, true, true, false) == false);
        CHECK(HUDController::IsQuickContainerWorkbenchTarget(TargetInfo::ItemKind::Container, true, true, false) == false);
        CHECK(HUDController::IsQuickContainerWorkbenchTarget(TargetInfo::ItemKind::Furniture, true, false, true) == true);
        CHECK(HUDController::IsQuickContainerWorkbenchTarget(TargetInfo::ItemKind::Furniture, true, false, false) == true);
        CHECK(HUDController::IsQuickContainerWorkbenchTarget(TargetInfo::ItemKind::Furniture, false, false, false) == true);
        CHECK(HUDController::IsQuickContainerWorkbenchTarget(TargetInfo::ItemKind::Container, true, false, false) == false);
    }

    std::cout << "[TEST] test_hud_controller PASSED" << std::endl;
    return 0;
}

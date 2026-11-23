#include "check.h"
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
        in.powerArmorBypass = true;

        CHECK(state.Step(in).action == HUDController::HUDAction::kApplyFloat);

        in.inPowerArmor = true;
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

    std::cout << "[TEST] test_hud_controller PASSED" << std::endl;
    return 0;
}

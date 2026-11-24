#include "check.h"
#include <iostream>
#include <limits>
#include "../src/Config.h"
#include "../src/AnchorManager.h"

int main()
{
    std::cout << "[TEST] Running test_anchor..." << std::endl;

    auto& cfg = Config::GetSingleton();
    cfg.ResetDefaults();

    // 1. Door with handle node uses namedNodeWorldPos + doorOffset
    {
        AnchorManager::AnchorInput in;
        in.category = AnchorManager::TargetCategory::kDoor;
        in.refPosition = { 0.0f, 0.0f, 0.0f };
        in.hasBoundCenter = true;
        in.boundCenter = { 5.0f, 5.0f, 50.0f };
        in.hasNamedNode = true;
        in.namedNodeWorldPos = { 10.0f, 20.0f, 100.0f };

        const Vector3 out = AnchorManager::ComputeAnchorPosition(in, cfg);
        CHECK_NEAR(out.x, 10.0f, 0.001f);
        CHECK_NEAR(out.y, 20.0f, 0.001f);
        CHECK_NEAR(out.z, 80.0f, 0.001f);
    }

    // 2. Door without handle node falls back to boundCenter + doorOffset
    {
        AnchorManager::AnchorInput in;
        in.category = AnchorManager::TargetCategory::kDoor;
        in.refPosition = { 0.0f, 0.0f, 0.0f };
        in.hasBoundCenter = true;
        in.boundCenter = { 5.0f, 10.0f, 50.0f };
        in.hasNamedNode = false;

        const Vector3 out = AnchorManager::ComputeAnchorPosition(in, cfg);
        CHECK_NEAR(out.x, 5.0f, 0.001f);
        CHECK_NEAR(out.y, 10.0f, 0.001f);
        CHECK_NEAR(out.z, 30.0f, 0.001f);
    }

    // 3. Door without bound center falls back to refPosition + doorOffset
    {
        AnchorManager::AnchorInput in;
        in.category = AnchorManager::TargetCategory::kDoor;
        in.refPosition = { 100.0f, 200.0f, 50.0f };
        in.hasBoundCenter = false;
        in.hasNamedNode = false;

        const Vector3 out = AnchorManager::ComputeAnchorPosition(in, cfg);
        CHECK_NEAR(out.x, 100.0f, 0.001f);
        CHECK_NEAR(out.y, 200.0f, 0.001f);
        CHECK_NEAR(out.z, 30.0f, 0.001f);
    }

    // 4. Actor with spine bone uses namedNodeWorldPos + npcOffset (default +10 Z)
    {
        AnchorManager::AnchorInput in;
        in.category = AnchorManager::TargetCategory::kActor;
        in.refPosition = { 0.0f, 0.0f, 0.0f };
        in.hasNamedNode = true;
        in.namedNodeWorldPos = { 1.0f, 2.0f, 90.0f };

        const Vector3 out = AnchorManager::ComputeAnchorPosition(in, cfg);
        CHECK_NEAR(out.x, 1.0f, 0.001f);
        CHECK_NEAR(out.y, 2.0f, 0.001f);
        CHECK_NEAR(out.z, 100.0f, 0.001f);
    }

    // 5. Container uses boundCenter + containerOffset (default +15 Z)
    {
        AnchorManager::AnchorInput in;
        in.category = AnchorManager::TargetCategory::kContainer;
        in.hasBoundCenter = true;
        in.boundCenter = { 30.0f, 40.0f, 25.0f };

        const Vector3 out = AnchorManager::ComputeAnchorPosition(in, cfg);
        CHECK_NEAR(out.x, 30.0f, 0.001f);
        CHECK_NEAR(out.y, 40.0f, 0.001f);
        CHECK_NEAR(out.z, 40.0f, 0.001f);
    }

    // 6. Item uses boundCenter + itemOffset (default -8 Z)
    {
        AnchorManager::AnchorInput in;
        in.category = AnchorManager::TargetCategory::kItem;
        in.hasBoundCenter = true;
        in.boundCenter = { 7.0f, 8.0f, 12.0f };

        const Vector3 out = AnchorManager::ComputeAnchorPosition(in, cfg);
        CHECK_NEAR(out.x, 7.0f, 0.001f);
        CHECK_NEAR(out.y, 8.0f, 0.001f);
        CHECK_NEAR(out.z, 4.0f, 0.001f);
    }

    // 7. Furniture / Terminal category uses boundCenter + furnitureOffset (default 0, 0, 10)
    {
        AnchorManager::AnchorInput in;
        in.category = AnchorManager::TargetCategory::kFurnitureTerminal;
        in.hasBoundCenter = true;
        in.boundCenter = { 12.0f, 24.0f, 88.0f };

        const Vector3 out = AnchorManager::ComputeAnchorPosition(in, cfg);
        CHECK_NEAR(out.x, 12.0f, 0.001f);
        CHECK_NEAR(out.y, 24.0f, 0.001f);
        CHECK_NEAR(out.z, 98.0f, 0.001f);

        ConfigData custom = cfg.GetSnapshot();
        custom.furnitureOffset = { 1.0f, -2.0f, 5.0f };
        const Vector3 outCustom = AnchorManager::ComputeAnchorPosition(in, custom);
        CHECK_NEAR(outCustom.x, 13.0f, 0.001f);
        CHECK_NEAR(outCustom.y, 22.0f, 0.001f);
        CHECK_NEAR(outCustom.z, 93.0f, 0.001f);
    }

    // 8. Door & Furniture marker node name matching (case-insensitive)
    CHECK(AnchorManager::IsDoorAnchorNodeName("DoorHandle01"));
    CHECK(AnchorManager::IsDoorAnchorNodeName("Lock_Cylinder"));
    CHECK(AnchorManager::IsDoorAnchorNodeName("BrassKnob"));
    CHECK(AnchorManager::IsDoorAnchorNodeName("DoorLatch"));
    CHECK(!AnchorManager::IsDoorAnchorNodeName("DoorFrame"));
    CHECK(!AnchorManager::IsDoorAnchorNodeName("CollisionMesh"));
    CHECK(!AnchorManager::IsDoorAnchorNodeName(""));

    CHECK(AnchorManager::IsFurnitureMarkerNodeName("FurnitureMarker0"));
    CHECK(AnchorManager::IsFurnitureMarkerNodeName("Marker_Sit_01"));
    CHECK(AnchorManager::IsFurnitureMarkerNodeName("EditorMarker"));
    CHECK(AnchorManager::IsFurnitureMarkerNodeName("AnimObjectSword"));
    CHECK(AnchorManager::IsFurnitureMarkerNodeName("CollisionBox"));
    CHECK(AnchorManager::IsFurnitureMarkerNodeName("BSFurnitureMarkerNode"));
    CHECK(!AnchorManager::IsFurnitureMarkerNodeName("TerminalScreen01"));
    CHECK(!AnchorManager::IsFurnitureMarkerNodeName(""));

    // 9. Transient particle/FX node filtering (never exclude static meshes like ChemistryStation or FireBarrel)
    CHECK(AnchorManager::IsTransientEffectNodeName("BSValueNode"));
    CHECK(AnchorManager::IsTransientEffectNodeName("BSValueNode01"));
    CHECK(AnchorManager::IsTransientEffectNodeName("AddOnNodeFire"));
    CHECK(AnchorManager::IsTransientEffectNodeName("PSysSmoke"));
    CHECK(AnchorManager::IsTransientEffectNodeName("PSys01"));
    CHECK(AnchorManager::IsTransientEffectNodeName("MPS_Embers"));
    CHECK(AnchorManager::IsTransientEffectNodeName("SparkEmitter"));
    CHECK(AnchorManager::IsTransientEffectNodeName("FlameFX"));
    CHECK(AnchorManager::IsTransientEffectNodeName("FireFX"));
    CHECK(AnchorManager::IsTransientEffectNodeName("HeatFX01"));
    CHECK(AnchorManager::IsTransientEffectNodeName("FXBeam"));
    CHECK(AnchorManager::IsTransientEffectNodeName("VFX_Steam"));
    CHECK(AnchorManager::IsTransientEffectNodeName("ParticleSystem01"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("ArmorWorkbench01"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("Anvil"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("Spotlight"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("FireBarrel"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("Terminal_Desktop"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("ChemistryStation"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("ViseClamps"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("BenchLamps"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("Dashboard01"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("TrashCan"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("CashRegister"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("SteamValve"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("FogCondenser"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("TerminalGlow"));
    CHECK(!AnchorManager::IsTransientEffectNodeName("WorkshopAddon"));
    CHECK(!AnchorManager::IsTransientEffectNodeName(""));

    // 10. SelectSafeItemBoundCenter protects loose items from skewed headlamp/FX bounds
    {
        const Vector3 refPos{ 100.0f, 200.0f, 50.0f };

        // 10a. Normal loose item within deviation uses rootGeomCenter
        const Vector3 normalRoot{ 104.0f, 203.0f, 56.0f };
        const Vector3 normalOut = AnchorManager::SelectSafeItemBoundCenter(
            refPos, true, normalRoot, true, { 101.0f, 201.0f, 52.0f }, 40.0f);
        CHECK_NEAR(normalOut.x, normalRoot.x, 0.001f);
        CHECK_NEAR(normalOut.y, normalRoot.y, 0.001f);
        CHECK_NEAR(normalOut.z, normalRoot.z, 0.001f);

        // 10b. Skewed headlamp/FX root bound (120 units away) falls back to close targeted sub-mesh
        const Vector3 skewedRoot{ 220.0f, 200.0f, 50.0f };
        const Vector3 closeTargeted{ 103.0f, 202.0f, 53.0f };
        const Vector3 skewedOut = AnchorManager::SelectSafeItemBoundCenter(
            refPos, true, skewedRoot, true, closeTargeted, 40.0f);
        CHECK_NEAR(skewedOut.x, closeTargeted.x, 0.001f);
        CHECK_NEAR(skewedOut.y, closeTargeted.y, 0.001f);
        CHECK_NEAR(skewedOut.z, closeTargeted.z, 0.001f);

        // 10c. Skewed root bound with no valid targeted center falls back safely to refPos
        const Vector3 noTargetOut = AnchorManager::SelectSafeItemBoundCenter(
            refPos, true, skewedRoot, false, {}, 40.0f);
        CHECK_NEAR(noTargetOut.x, refPos.x, 0.001f);
        CHECK_NEAR(noTargetOut.y, refPos.y, 0.001f);
        CHECK_NEAR(noTargetOut.z, refPos.z, 0.001f);

        // 10d. Both root and targeted absent falls back to refPos
        const Vector3 emptyOut = AnchorManager::SelectSafeItemBoundCenter(
            refPos, false, {}, false, {}, 40.0f);
        CHECK_NEAR(emptyOut.x, refPos.x, 0.001f);
        CHECK_NEAR(emptyOut.y, refPos.y, 0.001f);
        CHECK_NEAR(emptyOut.z, refPos.z, 0.001f);

        // 10e. Intermediate skewed root bound (60 units, > maxDev 40) falls back strictly to refPos
        const Vector3 midSkewedRoot{ 160.0f, 200.0f, 50.0f };
        const Vector3 midOut = AnchorManager::SelectSafeItemBoundCenter(
            refPos, true, midSkewedRoot, false, {}, 40.0f);
        CHECK_NEAR(midOut.x, refPos.x, 0.001f);
        CHECK_NEAR(midOut.y, refPos.y, 0.001f);
        CHECK_NEAR(midOut.z, refPos.z, 0.001f);

        // 10f. NaN rootGeomCenter falls back cleanly to valid targetedCenter or refPos
        const float qNan = std::numeric_limits<float>::quiet_NaN();
        const Vector3 nanRoot{ qNan, 200.0f, 50.0f };
        const Vector3 nanWithTarget = AnchorManager::SelectSafeItemBoundCenter(
            refPos, true, nanRoot, true, closeTargeted, 40.0f);
        CHECK_NEAR(nanWithTarget.x, closeTargeted.x, 0.001f);
        CHECK_NEAR(nanWithTarget.y, closeTargeted.y, 0.001f);
        CHECK_NEAR(nanWithTarget.z, closeTargeted.z, 0.001f);

        const Vector3 nanBoth = AnchorManager::SelectSafeItemBoundCenter(
            refPos, true, nanRoot, true, nanRoot, 40.0f);
        CHECK_NEAR(nanBoth.x, refPos.x, 0.001f);
        CHECK_NEAR(nanBoth.y, refPos.y, 0.001f);
        CHECK_NEAR(nanBoth.z, refPos.z, 0.001f);
    }

    std::cout << "[TEST] test_anchor PASSED" << std::endl;
    return 0;
}

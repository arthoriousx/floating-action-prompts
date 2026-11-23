#include "check.h"
#include <iostream>
#include "../src/Config.h"
#include "../src/TargetInfo.h"

int main()
{
    std::cout << "[TEST] Running test_target_info..." << std::endl;

    // 1. SplitActivateText parses vanilla, bracketed, and plain verb lines
    {
        const auto a1 = TargetInfo::SplitActivateText("E) Take");
        CHECK(a1.keyLabel == "E");
        CHECK(a1.verb == "TAKE");

        const auto a2 = TargetInfo::SplitActivateText("[E] Open");
        CHECK(a2.keyLabel == "E");
        CHECK(a2.verb == "OPEN");

        const auto a3 = TargetInfo::SplitActivateText("Sleep");
        CHECK(a3.keyLabel.empty());
        CHECK(a3.verb == "SLEEP");

        const auto a4 = TargetInfo::SplitActivateText("  ");
        CHECK(a4.keyLabel.empty());
        CHECK(a4.verb.empty());
    }

    // 2. Junk item inline summary & InfoCard rows
    {
        ConfigData cfg;
        TargetInfo::TargetMetadata meta;
        meta.formID = 0x12345;
        meta.category = AnchorManager::TargetCategory::kItem;
        meta.itemKind = TargetInfo::ItemKind::MiscJunk;
        meta.displayName = "Industrial Oil Canister";
        meta.typeLabel = "Misc";
        meta.hasItemStats = true;
        meta.value = 14;
        meta.weight = 3.0f;
        meta.scrapComponents.push_back({ "Aluminum", 2 });
        meta.scrapComponents.push_back({ "Oil", 2 });

        const std::string inlineStr = TargetInfo::FormatInlineSummary(meta, cfg);
        CHECK(inlineStr.find("2x Aluminum") != std::string::npos);
        CHECK(inlineStr.find("2x Oil") != std::string::npos);
        CHECK(inlineStr.find("Val 14") != std::string::npos);
        CHECK(inlineStr.find("Wt 3") != std::string::npos);

        const auto rows = TargetInfo::BuildInfoCardRows(meta, cfg);
        CHECK(rows.size() == 4);
        CHECK(rows[0].label == "Components");
        CHECK(rows[0].value == "2x Aluminum, 2x Oil");
        CHECK(rows[1].label == "Weight");
        CHECK(rows[1].value == "3");
        CHECK(rows[2].label == "Value");
        CHECK(rows[2].value == "14");
        CHECK(rows[3].label == "Val / Wt");

        // Respect toggles
        cfg.showScrapComponents = false;
        const std::string noScrap = TargetInfo::FormatInlineSummary(meta, cfg);
        CHECK(noScrap.find("Aluminum") == std::string::npos);
        CHECK(noScrap.find("Val 14") != std::string::npos);
    }

    // 3. Weapon & Armor inline summary and card rows
    {
        ConfigData cfg;
        TargetInfo::TargetMetadata weap;
        weap.category = AnchorManager::TargetCategory::kItem;
        weap.itemKind = TargetInfo::ItemKind::Weapon;
        weap.displayName = "10mm Pistol";
        weap.typeLabel = "Weapon";
        weap.hasItemStats = true;
        weap.isWeapon = true;
        weap.damage = 18;
        weap.ammoName = "10mm Round";
        weap.value = 50;
        weap.weight = 3.5f;

        const std::string inlineWeap = TargetInfo::FormatInlineSummary(weap, cfg);
        CHECK(inlineWeap.find("DMG 18") != std::string::npos);
        CHECK(inlineWeap.find("10mm Round") != std::string::npos);
        CHECK(inlineWeap.find("Val 50") != std::string::npos);

        const auto rows = TargetInfo::BuildInfoCardRows(weap, cfg);
        CHECK(rows.size() >= 2);
        CHECK(rows[0].label == "Damage");
        CHECK(rows[0].value == "18");
        CHECK(rows[1].label == "Ammo");
        CHECK(rows[1].value == "10mm Round");
    }

    // 4. NPC stats inline & InfoCard rows
    {
        ConfigData cfg;
        TargetInfo::TargetMetadata npc;
        npc.category = AnchorManager::TargetCategory::kActor;
        npc.itemKind = TargetInfo::ItemKind::Npc;
        npc.displayName = "Ivy";
        npc.typeLabel = "NPC";
        npc.isNpc = true;
        npc.equippedWeapon = "Personal Minigun";
        npc.health = 140.0f;
        npc.maxHealth = 140.0f;
        npc.actionPoints = 195.0f;
        npc.maxActionPoints = 195.0f;
        npc.damageResist = 144.0f;
        npc.energyResist = 222.0f;

        const std::string inlineNpc = TargetInfo::FormatInlineSummary(npc, cfg);
        CHECK(inlineNpc.find("HP 140/140") != std::string::npos);
        CHECK(inlineNpc.find("DR 144") != std::string::npos);

        const auto rows = TargetInfo::BuildInfoCardRows(npc, cfg);
        CHECK(rows.size() == 5);
        CHECK(rows[0].label == "Weapon");
        CHECK(rows[0].value == "Personal Minigun");
        CHECK(rows[1].label == "Health");
        CHECK(rows[1].value == "140 / 140");
        CHECK(rows[2].label == "Action Points");
        CHECK(rows[2].value == "195 / 195");
        CHECK(rows[3].label == "Damage Resist");
        CHECK(rows[3].value == "144");
        CHECK(rows[4].label == "Energy Resist");
        CHECK(rows[4].value == "222");

        cfg.showNpcStats = false;
        CHECK(TargetInfo::FormatInlineSummary(npc, cfg).empty());
        CHECK(TargetInfo::BuildInfoCardRows(npc, cfg).empty());
    }

    // 5. Door / plain object without stats yields empty inline & rows
    {
        ConfigData cfg;
        TargetInfo::TargetMetadata door;
        door.category = AnchorManager::TargetCategory::kDoor;
        door.itemKind = TargetInfo::ItemKind::Door;
        door.displayName = "Bunker Door";

        CHECK(TargetInfo::FormatInlineSummary(door, cfg).empty());
        CHECK(TargetInfo::BuildInfoCardRows(door, cfg).empty());
    }

    // 6. Multi-line GetActivateText extracts the last line as verb (e.g. "Dog\nTalk" -> "TALK")
    {
        const auto dog = TargetInfo::SplitActivateText("Dog\nTalk");
        CHECK(dog.keyLabel.empty());
        CHECK(dog.verb == "TALK");

        const auto canister = TargetInfo::SplitActivateText("Industrial Oil Canister {{Aluminum, Oil}}\nTake");
        CHECK(canister.keyLabel.empty());
        CHECK(canister.verb == "TAKE");

        const auto bed = TargetInfo::SplitActivateText("Bed\r\nSleep\r\n");
        CHECK(bed.keyLabel.empty());
        CHECK(bed.verb == "SLEEP");
    }

    // 7. CleanDisplayName strips FIS [Tag], [[Nested]], HTML tags, and {{Component}} while keeping legit parens
    {
        CHECK(TargetInfo::CleanDisplayName("[Scrap] Industrial Oil Canister {{Aluminum, Oil}}", true) ==
              "Industrial Oil Canister");
        CHECK(TargetInfo::CleanDisplayName("[Scrap] Bell Jar {{Glass}}", true) == "Bell Jar");
        CHECK(TargetInfo::CleanDisplayName("[Aid] SP-1 Stimpak", true) == "SP-1 Stimpak");
        CHECK(TargetInfo::CleanDisplayName("Fusion Core (50)", true) == "Fusion Core (50)");
        CHECK(TargetInfo::CleanDisplayName("(Mk II) Combat Armor", true) == "(Mk II) Combat Armor");
        CHECK(TargetInfo::CleanDisplayName("(Empty) Beer Bottle", true) == "(Empty) Beer Bottle");
        CHECK(TargetInfo::CleanDisplayName("[[Scrap]] Desk Fan", true) == "Desk Fan");
        CHECK(TargetInfo::CleanDisplayName("<font color='#ffffff'>[Scrap]</font> Oil Can", true) == "Oil Can");
        CHECK(TargetInfo::CleanDisplayName("[Scrap] Bell Jar {{Glass}}", false) ==
              "[Scrap] Bell Jar {{Glass}}");
    }

    // 8. UTF-8 safe ToUpperAscii, SplitActivateText edge cases, and InfoCard tag cleaning
    {
        CHECK(TargetInfo::ToUpperAscii("Ab\xC3\xB3""bora") == "AB\xC3\xB3""BORA");

        const auto heart = TargetInfo::SplitActivateText("I <3 Dogmeat");
        CHECK(heart.keyLabel.empty());
        CHECK(heart.verb == "I <3 DOGMEAT");

        const auto useOne = TargetInfo::SplitActivateText("Use(1)");
        CHECK(useOne.keyLabel.empty());
        CHECK(useOne.verb == "USE(1)");

        const auto openParen = TargetInfo::SplitActivateText("Open)");
        CHECK(openParen.keyLabel.empty());
        CHECK(openParen.verb == "OPEN)");

        const auto bracketNoVerb = TargetInfo::SplitActivateText("[Take]");
        CHECK(bracketNoVerb.keyLabel.empty());
        CHECK(bracketNoVerb.verb == "[TAKE]");

        const auto bracketWithVerb = TargetInfo::SplitActivateText("[E] Open");
        CHECK(bracketWithVerb.keyLabel == "E");
        CHECK(bracketWithVerb.verb == "OPEN");

        ConfigData cfg;
        cfg.cleanSorterTags = true;
        TargetInfo::TargetMetadata gun;
        gun.isWeapon = true;
        gun.damage = 25;
        gun.ammoName = "[Ammo] .45 Round";
        const auto rows = TargetInfo::BuildInfoCardRows(gun, cfg);
        CHECK(rows.size() >= 2);
        CHECK(rows[1].label == "Ammo");
        CHECK(rows[1].value == ".45 Round");
    }

    // 9. ResolveTranslationFallback resolves internal Scaleform $ keys cleanly
    {
        CHECK(TargetInfo::ResolveTranslationFallback("$TAKE") == "TAKE");
        CHECK(TargetInfo::ResolveTranslationFallback("$CRAFT") == "CRAFT");
        CHECK(TargetInfo::ResolveTranslationFallback("$WORKSHOP") == "WORKSHOP");
        CHECK(TargetInfo::ResolveTranslationFallback("$QUICKCONTAINERTRANSFER") == "TRANSFER");
        CHECK(TargetInfo::ResolveTranslationFallback("$CUSTOM_ACTION_KEY") == "CUSTOM ACTION KEY");
        CHECK(TargetInfo::ResolveTranslationFallback("$QUICKCONTAINERSTORE") == "STORE");
        CHECK(TargetInfo::ResolveTranslationFallback("MANAGE") == "MANAGE");
        CHECK(TargetInfo::ResolveTranslationFallback("  $Take  ") == "TAKE");
    }

    std::cout << "[TEST] test_target_info PASSED" << std::endl;
    return 0;
}

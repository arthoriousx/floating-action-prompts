#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Config.h"
#include "Projection.h"
#include "TargetInfo.h"

#ifndef FAP_UNIT_TEST
#include <atomic>
#include <mutex>
#include "RE/Fallout.h"

namespace RE
{
    struct PickRefUpdateEvent
    {
        ObjectRefHandle pickRef;
    };
    static_assert(sizeof(PickRefUpdateEvent) == 0x4);
}
#endif

namespace HUDController
{
    enum class HUDAction
    {
        kNoOp,
        kApplyFloat,
        kRestoreBaseline,
        kSuppressNative
    };

    struct HUDFrameInput
    {
        bool enabled{ true };
        bool inPowerArmor{ false };
        bool powerArmorBypass{ true };
        bool quickContainerVisible{ false };
        bool rolloverVisible{ false };
        bool nativeClipVisible{ false };
        bool hasTarget{ false };
        Projection::ScreenCoord projectedTarget{};
        float smoothingSpeed{ 0.25f };
        float deltaSeconds{ 1.0f / 60.0f };
        float currentClipX{ 0.0f };
        float currentClipY{ 0.0f };
        float currentClipAlpha{ 1.0f };
        bool hideNativeClip{ false };
        float deadzonePx{ 0.0f };
    };

    struct ButtonHintEntry
    {
        std::string key;
        std::string label;
        bool enabled{ true };
    };

    inline constexpr std::size_t kMaxExpandedInventoryItems = 512;

    struct QuickContainerItemEntry
    {
        std::string name;
        std::uint32_t count{ 1 };
        bool isLegendary{ false };
        bool isFavorite{ false };
        bool isTaggedForSearch{ false };
        bool isEquipped{ false };
        TargetInfo::ItemKind itemKind{ TargetInfo::ItemKind::None };
        std::uint64_t stackSignature{ 0 };
        std::uint32_t value{ 0 };
        float weight{ 0.0f };
    };

    struct QuickContainerMergeResult
    {
        std::vector<QuickContainerItemEntry> windowItems;
        std::int32_t totalItemCount{ 0 };
        std::int32_t windowStartIndex{ 0 };
        std::int32_t selectedIndex{ 0 };
        std::int32_t matchedStartIndex{ -1 };
        int matchedOrderIndex{ -1 };
        bool expanded{ false };
        bool hasMoreAbove{ false };
        bool hasMoreBelow{ false };
    };

    struct QuickContainerOverlay
    {
        bool active{ false };
        bool docked{ false };
        bool isWorkbench{ false };
        float normScreenX{ 0.5f };
        float normScreenY{ 0.5f };
        float edgeFade{ 1.0f };
        std::string headerTitle;
        std::int32_t selectedIndex{ 0 };
        std::int32_t totalItemCount{ 0 };
        std::int32_t windowStartIndex{ 0 };
        bool hasMoreAbove{ false };
        bool hasMoreBelow{ false };
        bool hasExactTotal{ false };
        std::vector<QuickContainerItemEntry> items;
        std::vector<ButtonHintEntry> buttons;
        bool hasSelectedItemMeta{ false };
        std::int64_t selectedItemFirstSeenMs{ 0 };
        TargetInfo::TargetMetadata selectedItemMeta{};
    };

    struct OverlaySnapshot
    {
        bool visible{ false };
        bool quickContainerVisible{ false };
        float normScreenX{ 0.5f };
        float normScreenY{ 0.5f };
        float edgeFade{ 1.0f };
        std::int64_t updateTimeMs{ 0 };
        std::int64_t targetFirstSeenMs{ 0 };
        TargetInfo::TargetMetadata target{};
        std::string actionKey;
        std::string actionVerb;
        std::string secondaryKey;
        std::string secondaryVerb;
        std::vector<ButtonHintEntry> actions;
        QuickContainerOverlay quickContainer{};
    };

    struct HUDFrameOutput
    {
        HUDAction action{ HUDAction::kNoOp };
        float x{ 0.0f };
        float y{ 0.0f };
        float alpha{ 1.0f };
        bool applyPosition{ false };
        bool applyAlpha{ false };
    };

    class HUDTransformState
    {
    public:
        static constexpr int kLossGraceFrames = 2;

        [[nodiscard]] HUDFrameOutput Step(const HUDFrameInput& a_in);
        [[nodiscard]] bool IsHidingAlpha() const noexcept { return (_isFloating || _modifiedAlpha) && _hidingAlpha; }
        [[nodiscard]] bool HasLastScreen() const noexcept { return _hasPrevScreen; }
        [[nodiscard]] Projection::ScreenCoord LastScreen() const noexcept { return _prevScreen; }
        void Reset() noexcept;

    private:
        bool _isFloating{ false };
        bool _hasBaseline{ false };
        float _baseX{ 0.0f };
        float _baseY{ 0.0f };
        bool _hasPrevScreen{ false };
        Projection::ScreenCoord _prevScreen{};
        bool _hasAnchorTarget{ false };
        float _anchorTargetX{ 0.0f };
        float _anchorTargetY{ 0.0f };
        bool _modifiedAlpha{ false };
        bool _hidingAlpha{ false };
        int _lossFrames{ 0 };
    };

    void StageToParentLocalFallback(
        float a_stageX,
        float a_stageY,
        float a_parentX,
        float a_parentY,
        float a_parentScaleX,
        float a_parentScaleY,
        float& a_outLocalX,
        float& a_outLocalY) noexcept;

    void AggregateItemStacks(std::vector<QuickContainerItemEntry>& a_stacks);

    [[nodiscard]] QuickContainerMergeResult MergeQuickContainerInventory(
        const std::vector<QuickContainerItemEntry>& a_engineWindow,
        std::int32_t a_engineSelectedIdx,
        const std::vector<QuickContainerItemEntry>& a_inventoryItems,
        int a_maxVisibleRows,
        int a_preferredOrder = -1,
        std::int32_t a_prevStart = -1);

    struct CachedInventorySnapshot
    {
        std::uint32_t ownerFormID{ 0 };
        std::size_t rawListSize{ 0 };
        std::size_t rawStackCount{ 0 };
        std::uint64_t rawTotalCountSum{ 0 };
        bool skipEquipped{ false };
        bool truncatedByCap{ false };
        std::int64_t timestampMs{ 0 };
        std::uint64_t generation{ 0 };
        std::vector<QuickContainerItemEntry> items;
        std::unordered_map<std::string, TargetInfo::ItemKind> kindByCleanUpper;
    };

    struct QuickContainerMergeCache
    {
        std::uint32_t ownerFormID{ 0 };
        std::uint64_t invGeneration{ 0 };
        std::size_t rawWindowHash{ 0 };
        std::int32_t rawSel{ -1 };
        int maxRows{ 0 };
        bool expandEnabled{ true };
        std::uint32_t stickyOwnerFormID{ 0 };
        std::uint64_t stickyInvGen{ 0 };
        int lockedOrderIndex{ -1 };
        std::int32_t lastMatchedStart{ -1 };
        bool stickyFallback{ false };
        bool everExpanded{ false };
        int consecutiveMisses{ 0 };
        QuickContainerMergeResult result{};
    };

    [[nodiscard]] const QuickContainerMergeResult& ResolveMergedQuickContainerWindow(
        QuickContainerMergeCache& a_cache,
        std::uint32_t a_ownerFormID,
        const CachedInventorySnapshot& a_invSnap,
        const std::vector<QuickContainerItemEntry>& a_rawWindow,
        std::int32_t a_rawSel,
        int a_maxRows,
        bool a_expandEnabled = true);

#ifndef FAP_UNIT_TEST
    class Controller final :
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
        public RE::BSTEventSink<RE::PickRefUpdateEvent>
    {
    public:
        static Controller& GetSingleton()
        {
            static Controller instance;
            return instance;
        }

        void Install();
        void EnsurePickRefSink();
        void ClearPickedRef();
        void ResetState();
        [[nodiscard]] OverlaySnapshot GetOverlaySnapshot() const;
        bool OnHUDAdvance(RE::IMenu* a_hudMenu, float a_timeDelta);

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent& a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::PickRefUpdateEvent& a_event,
            RE::BSTEventSource<RE::PickRefUpdateEvent>* a_source) override;

    private:
        Controller() = default;

    public:
        [[nodiscard]] const QuickContainerMergeResult& ResolveMergedQuickContainerWindow(
            std::uint32_t a_ownerFormID,
            const CachedInventorySnapshot& a_invSnap,
            const std::vector<QuickContainerItemEntry>& a_rawWindow,
            std::int32_t a_rawSel,
            int a_maxRows,
            bool a_expandEnabled = true);

    private:
        void TryHookHUDMenu(RE::IMenu* a_hudMenu);
        [[nodiscard]] RE::NiPointer<RE::TESObjectREFR> GetPickedRef(bool& a_outHasPickEvent) const;
        [[nodiscard]] RE::NiPoint3 ResolveStableAnchorWorldPosition(
            RE::TESObjectREFR* a_ref,
            RE::NiAVObject* a_targeted3D,
            const ConfigData& a_cfg);
        [[nodiscard]] TargetInfo::TargetMetadata ResolveTargetMetadata(RE::TESObjectREFR* a_targetRef);
        [[nodiscard]] const CachedInventorySnapshot& CollectQuickContainerInventory(
            RE::TESObjectREFR* a_invOwner,
            bool a_skipEquipped);
        [[nodiscard]] bool ResolveQuickContainerSelectedItemMeta(
            RE::TESObjectREFR* a_invOwner,
            const QuickContainerItemEntry& a_item,
            std::int32_t a_selectedIndex,
            TargetInfo::TargetMetadata& a_outMeta);
        void InvalidateQuickContainerCache();
        void InvalidateQuickContainerCacheInternal();
        void PerformDeferredResets();
        void ClearOverlaySnapshot();
        static void Hook_AdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time);

        inline static REL::Relocation<decltype(&Hook_AdvanceMovie)> _AdvanceMovie;
        std::atomic<bool> _hooked{ false };
        std::atomic<bool> _installed{ false };
        std::atomic<bool> _pickSinkRegistered{ false };
        std::atomic<bool> _pendingReset{ false };
        std::atomic<bool> _pendingQcInvalidate{ false };

        mutable std::mutex _pickMutex;
        RE::ObjectRefHandle _pickedRefHandle{};
        bool _hasPickEvent{ false };

        HUDTransformState _state{};
        HUDTransformState _qcState{};

        mutable std::mutex _overlayMutex;
        OverlaySnapshot _overlaySnapshot{};

        std::uint32_t _cachedTargetFormID{ 0 };
        std::int64_t _cachedTargetTimeMs{ 0 };
        TargetInfo::TargetMetadata _cachedMetadata{};

        std::uint32_t _cachedAnchorFormID{ 0 };
        const void* _cachedAnchorRoot3D{ nullptr };
        RE::NiPoint3 _cachedAnchorRefPos{};
        RE::NiPoint3 _cachedAnchorRefAngle{};
        std::uint16_t _cachedAnchorRefScale{ 0 };
        Vector3 _cachedAnchorOffset{};
        RE::NiPoint3 _cachedAnchorWorldPos{};
        bool _hasCachedAnchor{ false };

        std::uint32_t _cachedQcOwnerFormID{ 0 };
        std::int32_t _cachedQcSelectedIndex{ -1 };
        std::int32_t _lastQcSelectedIndex{ 0 };
        std::string _cachedQcItemName;
        std::uint32_t _cachedQcItemCount{ 0 };
        bool _cachedQcItemEquipped{ false };
        bool _cachedQcItemLegendary{ false };
        bool _cachedQcItemFavorite{ false };
        std::int64_t _cachedQcItemTimeMs{ 0 };
        bool _cachedQcItemHasMeta{ false };
        TargetInfo::TargetMetadata _cachedQcItemMeta{};

        CachedInventorySnapshot _cachedInvSnapshot{};
        QuickContainerMergeCache _cachedMerge{};

        std::atomic<std::uintptr_t> _hoverTargetKey{ 0 };
        std::atomic<std::int64_t> _hoverFirstSeenMs{ 0 };
        std::atomic<std::size_t> _qcHoverItemHash{ 0 };
        std::atomic<std::int64_t> _qcHoverFirstSeenMs{ 0 };
    };
#endif
}

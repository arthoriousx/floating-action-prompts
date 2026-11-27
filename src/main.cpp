#include "PCH.h"
#include "Version.h"
#include "Config.h"
#include "HUDController.h"
#include "MenuFrameworkUI.h"

namespace
{
    [[nodiscard]] constexpr std::uint32_t PackVersion(
        std::uint32_t a_major,
        std::uint32_t a_minor,
        std::uint32_t a_patch) noexcept
    {
        return ((a_major & 0xFF) << 24) |
               ((a_minor & 0xFF) << 16) |
               ((a_patch & 0xFFF) << 4);
    }

    [[nodiscard]] constexpr F4SE::PluginVersionData MakePluginVersionData() noexcept
    {
        F4SE::PluginVersionData data{};
        data.pluginVersion = PackVersion(
            Plugin::VERSION_MAJOR,
            Plugin::VERSION_MINOR,
            Plugin::VERSION_PATCH);

        for (std::size_t i = 0; i < Plugin::NAME.size() && i < sizeof(data.name) - 1; ++i) {
            data.name[i] = Plugin::NAME[i];
        }

        data.addressIndependence = F4SE::PluginVersionData::kAddressIndependence_Signatures;
        data.structureIndependence =
            F4SE::PluginVersionData::kStructureIndependence_1_10_980Layout |
            F4SE::PluginVersionData::kStructureIndependence_1_11_137Layout;
        return data;
    }

    bool InitializeLogger()
    {
#ifndef NDEBUG
        auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
        auto path = logger::log_directory();
        if (!path) {
            return false;
        }

        *path /= fmt::format(FMT_STRING("{}.log"), Plugin::NAME);
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

        auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
        log->set_level(spdlog::level::trace);
#else
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
#endif

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v"s);
        return true;
    }

    void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) {
            return;
        }

        switch (a_msg->type) {
        case F4SE::MessagingInterface::kPostLoad:
            logger::info("F4SE post load received");
            MenuFrameworkUI::Register();
            break;
        case F4SE::MessagingInterface::kGameDataReady:
            logger::info("Game data ready received");
            Config::GetSingleton().LoadAll();
            HUDController::Controller::GetSingleton().Install();
            break;
        case F4SE::MessagingInterface::kPreLoadGame:
            HUDController::Controller::GetSingleton().ClearPickedRef();
            HUDController::Controller::GetSingleton().ResetState();
            break;
        case F4SE::MessagingInterface::kPostLoadGame:
        case F4SE::MessagingInterface::kNewGame:
            HUDController::Controller::GetSingleton().EnsurePickRefSink();
            HUDController::Controller::GetSingleton().EnsureActivateHook();
            break;
        default:
            break;
        }
    }
}

extern "C" DLLEXPORT constinit F4SE::PluginVersionData F4SEPlugin_Version = MakePluginVersionData();

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
    if (!InitializeLogger()) {
        return false;
    }

    if (a_f4se->IsEditor()) {
        logger::critical("Cannot load in Creation Kit editor");
        return false;
    }

    F4SE::Init(a_f4se);
    const auto& relMod = REL::Module::get();
    const char* runtimeFamily =
        relMod.is_ae() ? "AE" :
        relMod.is_ng() ? "NG" :
        relMod.is_og() ? "OG" : "Unknown";
    logger::info("{} v{} initializing on runtime {} ({})",
        Plugin::NAME,
        Plugin::VERSION_STRING,
        a_f4se->RuntimeVersion().string(),
        runtimeFamily);

    const auto messaging = F4SE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnF4SEMessage)) {
        logger::error("Failed to register F4SE messaging listener");
        return false;
    }

    logger::info("{} loaded successfully", Plugin::NAME);
    return true;
}

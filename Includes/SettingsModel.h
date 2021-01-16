#pragma once

#include <array>
#include <string>

#include "../Includes/Logger.h"

namespace SettingsModel_Constants
{
    static constexpr std::string_view cSaveFilePath{"config.txt"};

    static constexpr std::string_view cSaveLogLevel{"LogLevel"};
    static constexpr std::string_view cSaveAutoDiscoverXLinkKai{"AutoDiscoverXLinkKai"};
    static constexpr std::string_view cSaveChannel{"Channel"};
    static constexpr std::string_view cSaveXLinkIp{"XLinkIp"};
    static constexpr std::string_view cSaveXLinkPort{"XLinkPort"};

    static constexpr Logger::Level    cDefaultLogLevel{Logger::Level::INFO};
    static constexpr bool             cDefaultAutoDiscoverXLinkKai{false};
    static constexpr std::string_view cDefaultXLinkIp{"127.0.0.1"};
    static constexpr std::string_view cDefaultXLinkPort{"34523"};

    enum class EngineStatus
    {
        Idle = 0,
        Running,
        Error
    };

    static constexpr std::array<std::string_view, 3> cEngineStatusTexts{"Idle", "Running", "Error"};

    enum class Command
    {
        StartEngine = 0,
        StopEngine,
        StartSearchNetworks,
        StopSearchNetworks,
        SaveSettings,
        NoCommand
    };
}  // namespace SettingsModel_Constants

class SettingsModel
{
public:
    // Settings
    Logger::Level mLogLevel{SettingsModel_Constants::cDefaultLogLevel};

    std::string mXLinkIp{SettingsModel_Constants::cDefaultXLinkIp};
    std::string mXLinkPort{SettingsModel_Constants::cDefaultXLinkPort};

    // Statuses
    SettingsModel_Constants::EngineStatus mEngineStatus{SettingsModel_Constants::EngineStatus::Idle};

    // Commands
    SettingsModel_Constants::Command mCommand{SettingsModel_Constants::Command::NoCommand};

    // Config

    /**
     * Saves the config in SettingsModel to a file.
     * @param aPath - Path to save it in.
     * @return true if successful.
     */
    bool SaveToFile(std::string_view aPath) const;

    /**
     * Loads the config in a file to a SettingsModel.
     * @param aPath - Path to save it in.
     * @return true if successful.
     */
    bool LoadFromFile(std::string_view aPath);
};

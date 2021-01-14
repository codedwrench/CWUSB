#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <boost/program_options.hpp>
#include <boost/thread.hpp>

#undef timeout

#include "Includes/Logger.h"
#include "Includes/NetConversionFunctions.h"
#include "Includes/SettingsModel.h"
#include "Includes/USBReader.h"
#include "Includes/XLinkKaiConnection.h"

namespace
{
    constexpr std::string_view cLogFileName{"log.txt"};
    constexpr bool             cLogToDisk{true};
    constexpr std::string_view cConfigFileName{"config.txt"};

    // Indicates if the program should be running or not, used to gracefully exit the program.
    bool gRunning{true};
}  // namespace


static void SignalHandler(const boost::system::error_code& aError, int aSignalNumber)
{
    if (!aError) {
        if (aSignalNumber == SIGINT || aSignalNumber == SIGTERM) {
            // Quit gracefully.
            gRunning = false;
        }
    }
}

int main(int argc, char* argv[])
{
    std::string lProgramPath{"./"};

#if not defined(_MSC_VER) && not defined(__MINGW32__)
    // Make robust against sudo path change.
    std::array<char, PATH_MAX> lResolvedPath{};
    if (realpath(argv[0], lResolvedPath.data()) != nullptr) {
        lProgramPath = std::string(lResolvedPath.begin(), lResolvedPath.end());

        // Remove excecutable name from path
        size_t lExcecutableNameIndex{lProgramPath.rfind('/')};
        if (lExcecutableNameIndex != std::string::npos) {
            lProgramPath.erase(lExcecutableNameIndex + 1, lProgramPath.length() - lExcecutableNameIndex - 1);
        }
    }
#endif

    // Handle quit signals gracefully.
    boost::asio::io_service lSignalIoService{};
    boost::asio::signal_set lSignals(lSignalIoService, SIGINT, SIGTERM);
    lSignals.async_wait(&SignalHandler);
    boost::thread lThread{[lIoService = &lSignalIoService] { lIoService->run(); }};
    SettingsModel mSettingsModel{};
    mSettingsModel.LoadFromFile(lProgramPath + cConfigFileName.data());

    Logger::GetInstance().Init(mSettingsModel.mLogLevel, cLogToDisk, lProgramPath + cLogFileName.data());
    Logger::GetInstance().SetLogToScreen(true);

    Logger::GetInstance().Log("PSPXLinkBridge, by CodedWrench", Logger::Level::INFO);

    std::shared_ptr<XLinkKaiConnection> lXLinkKaiConnection{std::make_shared<XLinkKaiConnection>()};
    std::shared_ptr<USBReader>          lUSBReaderConnection{std::make_shared<USBReader>()};

    bool lSuccess{lXLinkKaiConnection->Open(mSettingsModel.mXLinkIp, std::stoi(mSettingsModel.mXLinkPort))};
    lUSBReaderConnection->SetIncomingConnection(lXLinkKaiConnection);

    if (lSuccess) {
        if (lUSBReaderConnection->OpenDevice()) {
            if (lXLinkKaiConnection->StartReceiverThread() && lUSBReaderConnection->StartReceiverThread()) {
                mSettingsModel.mEngineStatus = SettingsModel_Constants::EngineStatus::Running;
                while (gRunning) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            } else {
                Logger::GetInstance().Log("Failed to start receiver threads", Logger::Level::ERROR);
                mSettingsModel.mEngineStatus = SettingsModel_Constants::EngineStatus::Error;
            }
        } else {
            Logger::GetInstance().Log("Failed to open connection to USB", Logger::Level::ERROR);
            mSettingsModel.mEngineStatus = SettingsModel_Constants::EngineStatus::Error;
        }
    } else {
        Logger::GetInstance().Log("Failed to open connection to XLink Kai", Logger::Level::ERROR);
        mSettingsModel.mEngineStatus = SettingsModel_Constants::EngineStatus::Error;
    }


    lSignalIoService.stop();
    if (lThread.joinable()) {
        lThread.join();
    }
    exit(0);
}

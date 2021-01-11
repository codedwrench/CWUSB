#include "../Includes/SettingsModel.h"

/* Copyright (c) 2020 [Rick de Bondt] - SettingsModel.cpp */

using namespace SettingsModel_Constants;
#include <iostream>
std::string BoolToString(bool aBool)
{
    return aBool ? "true" : "false";
}

bool StringToBool(std::string_view aString)
{
    bool lReturn{false};

    if (aString == "true") {
        lReturn = true;
    }

    return lReturn;
}

bool SettingsModel::SaveToFile(std::string_view aPath) const
{
    bool          lReturn{false};
    std::ofstream lFile;
    lFile.open(aPath.data());

    if (lFile.is_open() && lFile.good()) {
        lFile << cSaveLogLevel << ": \"" << Logger::ConvertLogLevelToString(mLogLevel) << "\"" << std::endl;
        lFile << cSaveXLinkIp << ": \"" << mXLinkIp << "\"" << std::endl;
        lFile << cSaveXLinkPort << ": \"" << mXLinkPort << "\"" << std::endl;
        lFile.close();

        if (lFile.good()) {
            lFile.close();
            lReturn = true;
        } else {
            Logger::GetInstance().Log("Could not save config", Logger::Level::ERROR);
        }
    } else {
        Logger::GetInstance().Log(std::string("Could not open/create config file: ") + aPath.data(),
                                  Logger::Level::ERROR);
    }

    return lReturn;
}

bool SettingsModel::LoadFromFile(std::string_view aPath)
{
    bool          lReturn{false};
    std::ifstream lFile;
    lFile.open(aPath.data());

    if (lFile.is_open() && lFile.good()) {
        bool        lContinue{true};
        std::string lLine;

        while (lContinue && !lFile.eof() && lFile.good()) {
            getline(lFile, lLine);
            if (!lFile.eof() && lFile.good()) {
                lLine.erase(remove_if(lLine.begin(), lLine.end(), isspace), lLine.end());
                size_t      lUntilDelimiter = lLine.find(':');
                std::string lOption{lLine.substr(0, lUntilDelimiter)};
                std::string lResult{lLine.substr(lUntilDelimiter + 1, lLine.size() - lUntilDelimiter - 1)};
                try {
                    if (!lResult.empty()) {
                        if (lOption == cSaveLogLevel) {
                            mLogLevel = Logger::ConvertLogLevelStringToLevel(lResult.substr(1, lResult.size() - 2));
                        } else if (lOption == cSaveXLinkIp) {
                            mXLinkIp = lResult.substr(1, lResult.size() - 2);
                        } else if (lOption == cSaveXLinkPort) {
                            mXLinkPort = lResult.substr(1, lResult.size() - 2);
                        } else {
                            Logger::GetInstance().Log(std::string("Option:") + lOption + " unknown",
                                                      Logger::Level::DEBUG);
                        }
                    } else {
                        Logger::GetInstance().Log(std::string("Option:") + lOption + " has no parameter set",
                                                  Logger::Level::ERROR);
                    }
                } catch (std::exception& aException) {
                    Logger::GetInstance().Log(std::string("Option could not be read: ") + aException.what(),
                                              Logger::Level::ERROR);
                }
            } else {
                lContinue = false;
            }
        }
        lFile.close();

        if (lFile.eof()) {
            lReturn = true;
        } else {
            Logger::GetInstance().Log("Could not save config", Logger::Level::ERROR);
        }
    } else {
        Logger::GetInstance().Log(std::string("Could not open/create config file: ") + aPath.data(),
                                  Logger::Level::ERROR);
    }

    return lReturn;
}

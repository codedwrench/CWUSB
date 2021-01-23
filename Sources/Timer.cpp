#include "../Includes/Timer.h"

void Timer::Start()
{
    mStartTime = std::chrono::steady_clock::now();
}

void Timer::Stop(const std::string& aName)
{
    std::chrono::steady_clock::time_point lEndTime{std::chrono::steady_clock::now()};

    mLogFile << std::string(aName) << ","
             << std::chrono::duration_cast<std::chrono::microseconds>(lEndTime - mStartTime).count() << std::endl;
    std::cout << mLogFile.str();
}
#ifndef _autobot_debug_hpp
#define _autobot_debug_hpp

#include <Geode/Geode.hpp>

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

using namespace geode::prelude;

namespace autobot {

class AutoBotDebug {
public:
    static AutoBotDebug* get() {
        static AutoBotDebug* instance = new AutoBotDebug();
        return instance;
    }

    std::filesystem::path getLogDirectory() const {
        auto dir = Mod::get()->getSaveDir() / "autobot-debug";
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        return dir;
    }

    std::filesystem::path getLatestLogPath() const {
        return getLogDirectory() / "autobot-latest.log";
    }

    void beginSession(std::string const& reason) {
        openFresh();
        logEvent("session-start", reason);
    }

    void endSession(std::string const& reason) {
        if (!m_stream.is_open()) return;
        logEvent("session-end", reason);
        m_stream.flush();
        m_stream.close();
    }

    void logEvent(std::string const& tag, std::string const& message) {
        ensureOpen();
        if (!m_stream.is_open()) return;

        m_stream << timestamp() << " | " << tag << " | " << message << '\n';
        if ((++m_lineCount % 32u) == 0u) {
            m_stream.flush();
        }
    }

    void flush() {
        if (m_stream.is_open()) {
            m_stream.flush();
        }
    }

private:
    std::ofstream m_stream;
    std::uint64_t m_lineCount = 0;

    void ensureOpen() {
        if (m_stream.is_open()) return;
        openAppend();
    }

    void openFresh() {
        auto path = getLatestLogPath();
        if (m_stream.is_open()) {
            m_stream.close();
        }
        m_stream.open(path, std::ios::out | std::ios::trunc);
        m_lineCount = 0;
    }

    void openAppend() {
        auto path = getLatestLogPath();
        if (m_stream.is_open()) return;
        m_stream.open(path, std::ios::out | std::ios::app);
    }

    std::string timestamp() const {
        auto now = std::time(nullptr);
        std::tm localTime{};
#ifdef _WIN32
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif

        std::ostringstream out;
        out << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
        return out.str();
    }
};

} // namespace autobot

#endif

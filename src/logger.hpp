#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <windows.h>

namespace logger {

class Logger {
public:
    Logger() = default;
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    bool init(HINSTANCE dll) {
        std::wstring buf(MAX_PATH, L'\0');
        const auto len = ::GetModuleFileNameW(dll, buf.data(),
                                              static_cast<DWORD>(buf.size()));
        if (len == 0 || len == buf.size()) {
            return false;
        }
        buf.resize(len);

        std::error_code ec;
        const auto dir = std::filesystem::path(buf).parent_path();
        const auto path = dir / "death_counter.log";

        stream_.open(path, std::ios::app);
        return stream_.is_open();
    }

    template <typename... Args>
    void log(std::format_string<Args...> fmt, Args&&... args) {
        const std::string line = std::format(fmt, std::forward<Args>(args)...);
        const std::lock_guard lock{mutex_};
        if (stream_.is_open()) {
            stream_ << line << '\n';
            stream_.flush();
        }
    }

private:
    std::ofstream stream_;
    std::mutex     mutex_;
};

inline Logger& instance() {
    static Logger logger;
    return logger;
}

template <typename... Args>
void log(std::format_string<Args...> fmt, Args&&... args) {
    instance().log(fmt, std::forward<Args>(args)...);
}

}

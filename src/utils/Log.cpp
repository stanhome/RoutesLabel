#include "utils/Log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace routes_label::utils {

namespace {
const char* level_tag(LogLevel l) {
    switch (l) {
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::string time_string() {
    using namespace std::chrono;
    auto now   = system_clock::now();
    auto t     = system_clock::to_time_t(now);
    auto ms    = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S") << '.'
        << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}
}  // namespace

void log_write(LogLevel level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mutex());
    auto& os = (level == LogLevel::Info) ? std::cout : std::cerr;
    os << '[' << time_string() << "][" << level_tag(level) << "] " << msg << '\n';
    os.flush();
}

}  // namespace routes_label::utils

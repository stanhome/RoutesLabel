#include "utils/FileSystem.h"

#include <fstream>
#include <stdexcept>

#if defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <climits>
#elif defined(__linux__)
    #include <unistd.h>
    #include <climits>
#endif

namespace routes_label::utils {

std::vector<char> read_binary_file(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    std::streamsize size = ifs.tellg();
    if (size < 0) {
        throw std::runtime_error("Failed to query size: " + path.string());
    }
    ifs.seekg(0, std::ios::beg);
    std::vector<char> buffer(static_cast<size_t>(size));
    if (!ifs.read(buffer.data(), size)) {
        throw std::runtime_error("Failed to read file: " + path.string());
    }
    return buffer;
}

std::filesystem::path executable_dir() {
#if defined(__APPLE__)
    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) {
        throw std::runtime_error("_NSGetExecutablePath: buffer too small");
    }
    std::filesystem::path p = std::filesystem::canonical(raw);
    return p.parent_path();
#elif defined(__linux__)
    char raw[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", raw, sizeof(raw) - 1);
    if (n < 0) {
        throw std::runtime_error("readlink(/proc/self/exe) failed");
    }
    raw[n] = '\0';
    return std::filesystem::path(raw).parent_path();
#else
    #error "Unsupported platform"
#endif
}

std::filesystem::path assets_dir() {
    return executable_dir() / "assets";
}

}  // namespace routes_label::utils

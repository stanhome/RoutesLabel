#pragma once
//
// FileSystem.h
// 跨平台路径与二进制文件读取工具。
//

#include <filesystem>
#include <string>
#include <vector>

namespace routes_label::utils {

// 读取整个二进制文件到 byte vector。失败抛 std::runtime_error。
std::vector<char> read_binary_file(const std::filesystem::path& path);

// 解析当前可执行文件所在目录（mac/Linux 各自实现）。
std::filesystem::path executable_dir();

}  // namespace routes_label::utils

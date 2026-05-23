#pragma once
//
// utils/Json.h
// 把 assets/routes_demo.json 解析为 core::RouteSceneData。
// 解析失败（文件不存在 / 字段缺失 / 类型错误）抛 std::runtime_error，由 Application 层统一捕获。
//

#include "core/Types.h"

#include <filesystem>

namespace routes_label::utils {

// 加载完整 RouteSceneData。校验：
//   - mapViewRect / anchorA / anchorB 必须存在；
//   - routes 数组非空，每条 polyline.size() >= 2；
//   - color 长度为 3。
core::RouteSceneData load_route_scene_from_json(const std::filesystem::path& path);

}  // namespace routes_label::utils

#include "utils/SceneAlgo.h"

namespace routes_label::utils {

algo::Polylines scene_to_polylines(const core::RouteSceneData& scene) {
    algo::Polylines out;
    const size_t n = std::min<size_t>(algo::kRouteCount, scene.routes.size());
    for (size_t i = 0; i < n; ++i) {
        const auto& poly = scene.routes[i].polyline;
        out.routes[i].reserve(poly.size());
        for (const auto& p : poly) {
            out.routes[i].push_back({
                static_cast<float>(p.x),
                static_cast<float>(p.y),
            });
        }
    }
    return out;
}

algo::GridParams default_grid_params_for_scene(const core::RouteSceneData& scene) {
    algo::GridParams p;
    const auto& mr = scene.map_context.mapViewRect;
    p.screen_w = static_cast<float>(mr.w);
    p.screen_h = static_cast<float>(mr.h);
    return p;
}

}  // namespace routes_label::utils

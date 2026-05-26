#pragma once

#include "algo/GridCommon.h"
#include "core/Types.h"

namespace routes_label::utils {

algo::Polylines scene_to_polylines(const core::RouteSceneData& scene);

algo::GridParams default_grid_params_for_scene(const core::RouteSceneData& scene);

}  // namespace routes_label::utils

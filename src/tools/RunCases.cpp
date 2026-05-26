//
// tools/RunCases.cpp
// Headless runner: load assets/cases/*.json, run GridCpu, check optional _assert block.
//

#include "algo/GridCpu.h"
#include "core/Types.h"
#include "utils/Json.h"
#include "utils/SceneAlgo.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using routes_label::algo::GridCpu;
using routes_label::algo::GridParams;
using routes_label::algo::GridResult;
using routes_label::algo::kRouteCount;
using routes_label::algo::LabelResult;
using routes_label::algo::LabelStatus;

struct CaseAssert {
    bool     enabled           = false;
    bool     all_placed        = true;
    bool     no_mutual_overlap = false;
    bool     no_overlap_car_marker = false;
    float    min_label_center_distance = 0.0f;
};

struct CaseRun {
    std::filesystem::path path;
    std::string           id;
    std::string           description;
    CaseAssert            assert_cfg;
};

CaseAssert parse_assert(const nlohmann::json& doc) {
    CaseAssert a;
    if (!doc.contains("_assert") || !doc["_assert"].is_object()) {
        return a;
    }
    const auto& j = doc["_assert"];
    a.enabled = true;
    if (j.contains("all_placed") && j["all_placed"].is_boolean()) {
        a.all_placed = j["all_placed"].get<bool>();
    }
    if (j.contains("no_mutual_overlap") && j["no_mutual_overlap"].is_boolean()) {
        a.no_mutual_overlap = j["no_mutual_overlap"].get<bool>();
    }
    if (j.contains("min_label_center_distance")
        && j["min_label_center_distance"].is_number()) {
        a.min_label_center_distance = j["min_label_center_distance"].get<float>();
    }
    if (j.contains("no_overlap_car_marker") && j["no_overlap_car_marker"].is_boolean()) {
        a.no_overlap_car_marker = j["no_overlap_car_marker"].get<bool>();
    }
    return a;
}

routes_label::algo::AABBf car_marker_aabb(const routes_label::core::RouteSceneData& scene) {
    const auto& r = scene.map_context.carMarkerRect;
    routes_label::algo::AABBf bb;
    if (r.empty()) {
        return bb;
    }
    bb.mn.x = static_cast<float>(r.x);
    bb.mn.y = static_cast<float>(r.y);
    bb.mx.x = static_cast<float>(r.x + r.w);
    bb.mx.y = static_cast<float>(r.y + r.h);
    return bb;
}

std::string read_meta_string(const nlohmann::json& doc, const char* key) {
    if (!doc.contains("_case") || !doc["_case"].is_object()) {
        return {};
    }
    const auto& c = doc["_case"];
    if (c.contains(key) && c[key].is_string()) {
        return c[key].get<std::string>();
    }
    return {};
}

routes_label::algo::AABBf label_aabb(const LabelResult& r, float w, float h) {
    routes_label::algo::AABBf bb;
    bb.mn.x = r.label_center.x - w * 0.5f;
    bb.mn.y = r.label_center.y - h * 0.5f;
    bb.mx.x = r.label_center.x + w * 0.5f;
    bb.mx.y = r.label_center.y + h * 0.5f;
    return bb;
}

float center_distance(const LabelResult& a, const LabelResult& b) {
    const float dx = a.label_center.x - b.label_center.x;
    const float dy = a.label_center.y - b.label_center.y;
    return std::sqrt(dx * dx + dy * dy);
}

constexpr float kOdEps = 0.5f;

struct CheckResult {
    bool        pass = true;
    std::string detail;
};

bool same_point(const routes_label::core::Point& a, const routes_label::core::Point& b) {
    return std::fabs(static_cast<float>(a.x - b.x)) <= kOdEps
        && std::fabs(static_cast<float>(a.y - b.y)) <= kOdEps;
}

CheckResult check_same_origin_destination(const routes_label::core::RouteSceneData& scene,
                                          const nlohmann::json& doc) {
    if (static_cast<int>(scene.routes.size()) != kRouteCount) {
        return { false, "same_od: expected " + std::to_string(kRouteCount) + " routes" };
    }
    const auto& r0 = scene.routes[0].polyline;
    if (r0.size() < 2) {
        return { false, "same_od: route 0 polyline too short" };
    }
    for (int i = 1; i < kRouteCount; ++i) {
        const auto& ri = scene.routes[i].polyline;
        if (ri.size() < 2) {
            return { false, "same_od: route " + std::to_string(i) + " polyline too short" };
        }
        if (!same_point(r0.front(), ri.front())) {
            return { false, "same_od: route " + std::to_string(i)
                            + " start differs from route 0" };
        }
        if (!same_point(r0.back(), ri.back())) {
            return { false, "same_od: route " + std::to_string(i)
                            + " end differs from route 0" };
        }
    }
    if (doc.contains("_od") && doc["_od"].is_object()) {
        const auto& od = doc["_od"];
        if (od.contains("start") && od["start"].is_object()
            && od.contains("end") && od["end"].is_object()) {
            routes_label::core::Point want_s {
                od["start"]["x"].get<double>(), od["start"]["y"].get<double>() };
            routes_label::core::Point want_e {
                od["end"]["x"].get<double>(), od["end"]["y"].get<double>() };
            if (!same_point(r0.front(), want_s) || !same_point(r0.back(), want_e)) {
                return { false, "same_od: polyline endpoints mismatch _od block" };
            }
        }
    }
    return { true, "same_od ok" };
}

CheckResult check_assertions(const GridResult& result,
                             const GridParams& params,
                             const CaseAssert& cfg,
                             const routes_label::algo::AABBf& car_bb) {
    if (!cfg.enabled) {
        return { true, "(no _assert)" };
    }

    int placed = 0;
    for (int r = 0; r < kRouteCount; ++r) {
        if (result.labels[r].status != LabelStatus::Infeasible) {
            ++placed;
        }
    }

    if (cfg.all_placed && placed < kRouteCount) {
        return { false, "all_placed: only " + std::to_string(placed) + "/3 labels" };
    }

    if (cfg.no_mutual_overlap) {
        for (int i = 0; i < kRouteCount; ++i) {
            if (result.labels[i].status == LabelStatus::Infeasible) continue;
            const auto ai = label_aabb(result.labels[i], params.label_w, params.label_h);
            for (int j = i + 1; j < kRouteCount; ++j) {
                if (result.labels[j].status == LabelStatus::Infeasible) continue;
                const auto aj = label_aabb(result.labels[j], params.label_w, params.label_h);
                if (ai.intersects(aj)) {
                    return { false, "no_mutual_overlap: routes " + std::to_string(i)
                                    + " and " + std::to_string(j) };
                }
            }
        }
    }

    if (cfg.min_label_center_distance > 0.0f) {
        float min_d = 1e30f;
        for (int i = 0; i < kRouteCount; ++i) {
            if (result.labels[i].status == LabelStatus::Infeasible) continue;
            for (int j = i + 1; j < kRouteCount; ++j) {
                if (result.labels[j].status == LabelStatus::Infeasible) continue;
                min_d = std::min(min_d, center_distance(result.labels[i], result.labels[j]));
            }
        }
        if (min_d < cfg.min_label_center_distance) {
            return { false, "min_label_center_distance: got " + std::to_string(min_d)
                            + " want >= " + std::to_string(cfg.min_label_center_distance) };
        }
    }

    if (cfg.no_overlap_car_marker) {
        if (car_bb.empty()) {
            return { false, "no_overlap_car_marker: carMarkerRect missing in JSON" };
        }
        for (int i = 0; i < kRouteCount; ++i) {
            if (result.labels[i].status == LabelStatus::Infeasible) continue;
            const auto L = label_aabb(result.labels[i], params.label_w, params.label_h);
            if (L.intersects(car_bb)) {
                return { false, "no_overlap_car_marker: route " + std::to_string(i) };
            }
        }
    }

    return { true, "ok" };
}

std::vector<std::filesystem::path> list_case_files(const std::filesystem::path& cases_dir,
                                                   const std::string& filter) {
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::is_directory(cases_dir)) {
        return out;
    }
    for (const auto& ent : std::filesystem::directory_iterator(cases_dir)) {
        if (!ent.is_regular_file()) continue;
        if (ent.path().extension() != ".json") continue;
        if (!filter.empty() && ent.path().stem().string() != filter) {
            continue;
        }
        out.push_back(ent.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

CaseRun load_case_meta(const std::filesystem::path& path) {
    CaseRun run;
    run.path = path;
    std::ifstream ifs(path);
    nlohmann::json doc;
    ifs >> doc;
    run.id          = read_meta_string(doc, "id");
    run.description = read_meta_string(doc, "description");
    if (run.id.empty()) {
        run.id = path.stem().string();
    }
    run.assert_cfg = parse_assert(doc);
    return run;
}

int run_one(const CaseRun& meta, bool verbose) {
    nlohmann::json doc;
    {
        std::ifstream ifs(meta.path);
        ifs >> doc;
    }
    auto scene = routes_label::utils::load_route_scene_from_json(meta.path);
    const CheckResult od_chk = check_same_origin_destination(scene, doc);
    if (!od_chk.pass) {
        std::cout << "FAIL  " << meta.id;
        if (!meta.description.empty()) {
            std::cout << "  —  " << meta.description;
        }
        std::cout << "\n       " << od_chk.detail << '\n';
        return 1;
    }

    auto polys = routes_label::utils::scene_to_polylines(scene);
    GridParams params = routes_label::utils::default_grid_params_for_scene(scene);
    {
        if (doc.contains("_gridParams") && doc["_gridParams"].is_object()) {
            const auto& gp = doc["_gridParams"];
            if (gp.contains("separationThreshold") && gp["separationThreshold"].is_number()) {
                params.separationThreshold = gp["separationThreshold"].get<float>();
            }
            if (gp.contains("arclength_balance") && gp["arclength_balance"].is_number()) {
                params.arclength_balance = gp["arclength_balance"].get<float>();
            }
            if (gp.contains("nms_grid_distance") && gp["nms_grid_distance"].is_number_integer()) {
                params.nms_grid_distance = gp["nms_grid_distance"].get<int>();
            }
            if (gp.contains("n_grid") && gp["n_grid"].is_number_integer()) {
                params.n_grid = gp["n_grid"].get<int>();
            }
        }
    }

    if (verbose) {
        std::cout << "       params: n_grid=" << params.n_grid
                  << " separation_thr=" << params.separationThreshold
                  << " arclength_bal=" << params.arclength_balance
                  << " nms_dist=" << params.nms_grid_distance << '\n';
    }

    GridCpu cpu;
    const GridResult result = cpu.compute(polys, params, verbose);
    const auto car_bb       = car_marker_aabb(scene);
    const CheckResult chk   = check_assertions(result, params, meta.assert_cfg, car_bb);

    const char* status_tag = chk.pass ? "PASS" : "FAIL";
    std::cout << status_tag << "  " << meta.id;
    if (!meta.description.empty()) {
        std::cout << "  —  " << meta.description;
    }
    std::cout << "  (" << result.compute_ms << " ms)";
    if (!chk.pass || verbose) {
        std::cout << "\n       assert: " << chk.detail;
    }
    std::cout << '\n';

    if (verbose) {
        for (int r = 0; r < kRouteCount; ++r) {
            const auto& L = result.labels[r];
            std::cout << "       route " << r
                      << " status=" << static_cast<int>(L.status)
                      << " anchor=(" << L.anchor.x << "," << L.anchor.y << ")"
                      << " center=(" << L.label_center.x << "," << L.label_center.y << ")\n";
        }
    }

    return chk.pass ? 0 : 1;
}

std::filesystem::path resolve_cases_dir(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--cases-dir") {
        return argv[2];
    }
    if (const char* env = std::getenv("ROUTES_CASES_DIR")) {
        return env;
    }
    // Relative to cwd: prefer build/bin/assets/cases when run from repo
    const std::filesystem::path candidates[] = {
        "assets/cases",
        "build/bin/assets/cases",
        "../assets/cases",
    };
    for (const auto& c : candidates) {
        if (std::filesystem::is_directory(c)) {
            return std::filesystem::canonical(c);
        }
    }
    return "assets/cases";
}

}  // namespace

int main(int argc, char** argv) {
    std::string filter;
    std::filesystem::path cases_dir_override;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: routes_label_cases [--cases-dir DIR] [case_stem] [-v]\n"
                      << "  Runs GridCpu on JSON cases under assets/cases/.\n"
                      << "  Optional _case / _assert blocks in each JSON.\n";
            return 0;
        } else if (arg == "--cases-dir" && i + 1 < argc) {
            cases_dir_override = argv[++i];
        } else if (!arg.empty() && arg[0] != '-') {
            filter = arg;
        }
    }

    const auto cases_dir = cases_dir_override.empty()
        ? resolve_cases_dir(argc, argv)
        : cases_dir_override;
    auto files = list_case_files(cases_dir, filter);
    if (files.empty()) {
        std::cerr << "No case JSON files in " << cases_dir << "\n";
        return 2;
    }

    std::cout << "Cases dir: " << cases_dir << "  (" << files.size() << " file(s))\n";

    int failures = 0;
    for (const auto& path : files) {
        const CaseRun meta = load_case_meta(path);
        failures += run_one(meta, verbose);
    }

    std::cout << (failures == 0 ? "All cases passed.\n" : (std::to_string(failures) + " case(s) failed.\n"));
    return failures > 0 ? 1 : 0;
}

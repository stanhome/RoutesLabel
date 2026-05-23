#include "utils/Json.h"

#include "utils/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace routes_label::utils {

namespace {

using nlohmann::json;

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& field, const std::string& reason) {
    std::ostringstream os;
    os << "[Json] " << path.string() << ": field '" << field << "' " << reason;
    throw std::runtime_error(os.str());
}

core::Point parse_point(const json& j, const std::filesystem::path& path, const std::string& field) {
    if (!j.is_object())                            fail(path, field, "must be an object {x,y}");
    if (!j.contains("x") || !j.contains("y"))      fail(path, field, "missing x/y");
    if (!j["x"].is_number() || !j["y"].is_number()) fail(path, field, "x/y must be numbers");
    return { j["x"].get<double>(), j["y"].get<double>() };
}

core::Rect parse_rect(const json& j, const std::filesystem::path& path, const std::string& field) {
    if (!j.is_object())                                          fail(path, field, "must be an object {x,y,w,h}");
    for (const char* k : { "x", "y", "w", "h" }) {
        if (!j.contains(k) || !j[k].is_number())                 fail(path, field, std::string("missing/invalid ") + k);
    }
    return { j["x"].get<double>(), j["y"].get<double>(),
             j["w"].get<double>(), j["h"].get<double>() };
}

}  // namespace

core::RouteSceneData load_route_scene_from_json(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("[Json] cannot open file: " + path.string());
    }
    json doc;
    try {
        ifs >> doc;
    } catch (const std::exception& e) {
        throw std::runtime_error("[Json] parse error in " + path.string() + ": " + e.what());
    }

    core::RouteSceneData out;

    // ---- mapViewRect ----
    if (!doc.contains("mapViewRect")) fail(path, "mapViewRect", "missing");
    out.map_context.mapViewRect = parse_rect(doc["mapViewRect"], path, "mapViewRect");
    out.map_context.mapCenter   = out.map_context.mapViewRect.center();

    // ---- routes ----
    if (!doc.contains("routes") || !doc["routes"].is_array() || doc["routes"].empty()) {
        fail(path, "routes", "must be a non-empty array");
    }
    const auto& routes_j = doc["routes"];
    out.routes.reserve(routes_j.size());
    out.styles.reserve(routes_j.size());

    for (size_t i = 0; i < routes_j.size(); ++i) {
        const auto& r = routes_j[i];
        const std::string prefix = "routes[" + std::to_string(i) + "]";

        if (!r.contains("routeId") || !r["routeId"].is_number_integer()) {
            fail(path, prefix + ".routeId", "missing or not integer");
        }
        if (!r.contains("polyline") || !r["polyline"].is_array() || r["polyline"].size() < 2) {
            fail(path, prefix + ".polyline", "must be array with >= 2 points");
        }

        core::RouteInput ri;
        ri.routeId = r["routeId"].get<int>();
        ri.polyline.reserve(r["polyline"].size());
        for (size_t k = 0; k < r["polyline"].size(); ++k) {
            ri.polyline.push_back(
                parse_point(r["polyline"][k], path,
                            prefix + ".polyline[" + std::to_string(k) + "]"));
        }
        out.routes.push_back(std::move(ri));

        // 颜色（可选；缺省给纯白）
        core::RouteVisualStyle st;
        st.routeId = out.routes.back().routeId;
        st.color[0] = st.color[1] = st.color[2] = 1.0f;
        if (r.contains("color")) {
            const auto& c = r["color"];
            if (!c.is_array() || c.size() != 3) {
                fail(path, prefix + ".color", "must be a [r,g,b] array of 3 numbers");
            }
            for (int k = 0; k < 3; ++k) {
                if (!c[k].is_number()) fail(path, prefix + ".color", "components must be numbers");
                st.color[k] = c[k].get<float>();
            }
        }
        out.styles.push_back(st);
    }

    // ---- carMarkerRect / obstructingViews（均可选）----
    if (doc.contains("carMarkerRect")) {
        out.map_context.carMarkerRect = parse_rect(doc["carMarkerRect"], path, "carMarkerRect");
    }
    if (doc.contains("obstructingViews") && doc["obstructingViews"].is_array()) {
        const auto& arr = doc["obstructingViews"];
        out.map_context.obstructingViews.reserve(arr.size());
        for (size_t i = 0; i < arr.size(); ++i) {
            out.map_context.obstructingViews.push_back(
                parse_rect(arr[i], path, "obstructingViews[" + std::to_string(i) + "]"));
        }
    }

    LOG_INFO("[Json] loaded " << path.filename().string()
             << ": " << out.routes.size() << " routes, "
             << out.map_context.obstructingViews.size() << " obstructing views");
    return out;
}

}  // namespace routes_label::utils

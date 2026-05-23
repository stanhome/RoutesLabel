#include "renderer/RouteScene.h"

#include "utils/Log.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace routes_label::renderer {

namespace {

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

inline Vec2 sub(const Vec2& a, const Vec2& b) { return { a.x - b.x, a.y - b.y }; }
inline Vec2 add(const Vec2& a, const Vec2& b) { return { a.x + b.x, a.y + b.y }; }
inline Vec2 mul(const Vec2& v, float s)       { return { v.x * s, v.y * s }; }
inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float length(const Vec2& v)             { return std::sqrt(dot(v, v)); }

inline Vec2 normalize_safe(const Vec2& v) {
    const float L = length(v);
    if (L < 1e-6f) return { 0.0f, 0.0f };
    return { v.x / L, v.y / L };
}

// 屏幕坐标系（y 朝下）下，某一段方向 d 的左法线（点向"左侧"，符号约定不影响 ribbon 对称性）。
// 这里取 (-dy, dx)。
inline Vec2 left_normal(const Vec2& d) { return { -d.y, d.x }; }

}  // namespace

RouteScene::RouteScene(core::RouteSceneData data) : data_(std::move(data)) {}

RibbonMesh RouteScene::build_ribbon_mesh(float line_width_px) const {
    RibbonMesh mesh;
    if (line_width_px <= 0.0f) {
        return mesh;
    }
    const float half_w = line_width_px * 0.5f;
    // miter limit：夹角太尖时 1/cos(theta/2) 会爆炸，clamp 到 4 倍 halfWidth。
    const float kMaxMiterScale = 4.0f;

    // 预估容量
    size_t total_verts = 0, total_idx = 0;
    for (const auto& r : data_.routes) {
        if (r.polyline.size() < 2) continue;
        total_verts += r.polyline.size() * 2;
        total_idx   += r.polyline.size() * 2 + 1;  // 每条 polyline 末尾 +1 哨兵
    }
    mesh.vertices.reserve(total_verts);
    mesh.indices.reserve(total_idx);

    for (size_t ri = 0; ri < data_.routes.size(); ++ri) {
        const auto& route = data_.routes[ri];
        const auto& poly  = route.polyline;
        if (poly.size() < 2) continue;

        // 颜色（与 routes 一一对应，缺省白）
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (ri < data_.styles.size()) {
            r = data_.styles[ri].color[0];
            g = data_.styles[ri].color[1];
            b = data_.styles[ri].color[2];
        }

        // 转 float Vec2
        std::vector<Vec2> p;
        p.reserve(poly.size());
        for (const auto& pt : poly) {
            p.push_back({ static_cast<float>(pt.x), static_cast<float>(pt.y) });
        }

        // 每段的单位方向 + 左法线
        const size_t N = p.size();
        std::vector<Vec2> seg_dir(N - 1);
        std::vector<Vec2> seg_nrm(N - 1);
        for (size_t i = 0; i + 1 < N; ++i) {
            seg_dir[i] = normalize_safe(sub(p[i + 1], p[i]));
            seg_nrm[i] = left_normal(seg_dir[i]);
        }

        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

        for (size_t i = 0; i < N; ++i) {
            // 顶点 i 的 ribbon 法线 + miter scale
            Vec2  nrm;
            float scale = 1.0f;
            if (i == 0) {
                nrm   = seg_nrm[0];
            } else if (i == N - 1) {
                nrm   = seg_nrm[N - 2];
            } else {
                // miter join：相邻两段法线和归一化
                const Vec2& na = seg_nrm[i - 1];
                const Vec2& nb = seg_nrm[i];
                Vec2 sum = add(na, nb);
                nrm = normalize_safe(sum);
                if (length(sum) < 1e-6f) {
                    // 完全反向（U 形），退化为 na
                    nrm = na;
                    scale = 1.0f;
                } else {
                    // 1/cos(theta/2) = 1 / (nrm · na)，因为 nrm 是 na 与 nb 的角平分线
                    const float c = dot(nrm, na);
                    if (c > 1e-3f) {
                        scale = std::min(1.0f / c, kMaxMiterScale);
                    } else {
                        scale = kMaxMiterScale;
                    }
                }
            }

            const Vec2 off = mul(nrm, half_w * scale);

            RibbonVertex vL = {};
            vL.pos[0] = p[i].x + off.x;
            vL.pos[1] = p[i].y + off.y;
            vL.color[0] = r; vL.color[1] = g; vL.color[2] = b;

            RibbonVertex vR = {};
            vR.pos[0] = p[i].x - off.x;
            vR.pos[1] = p[i].y - off.y;
            vR.color[0] = r; vR.color[1] = g; vR.color[2] = b;

            mesh.vertices.push_back(vL);
            mesh.vertices.push_back(vR);
        }

        // index buffer：left0, right0, left1, right1, ..., left{N-1}, right{N-1}, RESTART
        for (size_t i = 0; i < N; ++i) {
            mesh.indices.push_back(base + static_cast<uint32_t>(i * 2 + 0));  // left
            mesh.indices.push_back(base + static_cast<uint32_t>(i * 2 + 1));  // right
        }
        // 最后一条不加 restart 也行，但保持对称便于扩展
        mesh.indices.push_back(kPrimitiveRestartIndex);
    }

    LOG_INFO("[RouteScene] ribbon mesh: " << mesh.vertices.size()
             << " vertices, " << mesh.indices.size() << " indices ("
             << data_.routes.size() << " routes, line_width=" << line_width_px << ")");
    return mesh;
}

}  // namespace routes_label::renderer

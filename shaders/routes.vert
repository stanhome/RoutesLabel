#version 450
//
// routes.vert
// 输入：顶点 **map-world 2D 像素**坐标 + RGB 颜色（per-vertex）。
//   - 与 MapContext::mapViewRect 同坐标系：原点左上、y 朝下、单位"地图像素"，
//     范围内坐标与窗口尺寸无关。
// UBO（set=0, binding=0）：mat4 projection = world → clip 的 MVP，
//   由 host 端 MapView::compute() 算出（contain fit-to-window letterbox + ortho）。
//   详见 doc/map-world-space.md。
//

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec3 a_color;

layout(set = 0, binding = 0) uniform Globals {
    mat4 projection;
} u;

layout(location = 0) out vec3 v_color;

void main() {
    gl_Position = u.projection * vec4(a_pos, 0.0, 1.0);
    v_color = a_color;
}

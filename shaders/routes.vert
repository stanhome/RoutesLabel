#version 450
//
// routes.vert
// 输入：顶点屏幕像素坐标 + RGB 颜色（per-vertex）。
// UBO（set=0, binding=0）：mat4 ortho 投影矩阵（pixel-space → clip-space）。
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

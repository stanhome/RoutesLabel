#version 450

// 经典硬编码三角形：3 个 NDC 顶点 + 3 个颜色，由 gl_VertexIndex 索引选取。
// 不依赖 vertex buffer，drawCall 直接 vkCmdDraw(cmd, 3, 1, 0, 0) 即可。

layout(location = 0) out vec3 v_color;

vec2 positions[3] = vec2[](
    vec2( 0.0, -0.6),   // 顶部
    vec2( 0.6,  0.6),   // 右下
    vec2(-0.6,  0.6)    // 左下
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0), // 红
    vec3(0.0, 1.0, 0.0), // 绿
    vec3(0.0, 0.0, 1.0)  // 蓝
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    v_color = colors[gl_VertexIndex];
}

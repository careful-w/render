#version 330 core
// 全屏三角形：由 gl_VertexID 直接生成顶点坐标，无需 VBO/EBO
// 顶点 0 → NDC(-1,-1)  顶点 1 → NDC(3,-1)  顶点 2 → NDC(-1,3)
// 三角形完全覆盖屏幕，让片段着色器为每个像素执行一次光线追踪
void main() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}

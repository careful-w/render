#version 330 core
layout (location = 0) in vec2 aPos;   // 全屏三角形，坐标 [-1,3] 范围

out vec2 vUV;

void main() {
    vUV         = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}

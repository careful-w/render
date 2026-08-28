#version 330 core
layout (location = 0) in vec2 aGrid;   // 网格坐标 [0,1]²

uniform sampler2D uHeightTex;   // 高度场（CPU 波动方程模拟结果）
uniform vec4  uRect;            // 水面内区 (x0, z0, x1, z1)
uniform float uBaseY;           // 静水面高度
uniform mat4  uView;
uniform mat4  uProj;
uniform mat4  uLightSpace;

out vec3 vWorldPos;
out vec2 vUV;
out vec4 vLightPos;

void main() {
    float h  = texture(uHeightTex, aGrid).r;
    vec3 pos = vec3(mix(uRect.x, uRect.z, aGrid.x),
                    uBaseY + h,
                    mix(uRect.y, uRect.w, aGrid.y));
    vWorldPos   = pos;
    vUV         = aGrid;
    vLightPos   = uLightSpace * vec4(pos, 1.0);
    gl_Position = uProj * uView * vec4(pos, 1.0);
}

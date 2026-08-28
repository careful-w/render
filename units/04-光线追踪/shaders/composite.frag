#version 330 core
// 显示遍：把累积纹理中的 HDR 采样和取平均，再做色调映射 + Gamma 校正
out vec4 FragColor;

uniform sampler2D uAccumTex;   // 累积的 HDR 采样总和（GL_RGBA32F）
uniform float     uFrameCount; // 已累积的帧数（= 每像素采样数）
uniform vec2      uResolution;

// ACES 电影级色调映射（拟合近似），把 HDR 亮度平滑压进 [0,1]
vec3 acesToneMap(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp(x * (a * x + b) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    // 蒙特卡洛积分：采样总和 / 采样数 = 期望颜色
    vec3 hdr = texture(uAccumTex, uv).rgb / max(uFrameCount, 1.0);
    hdr *= 0.9;                  // 曝光校准（与截图代码保持一致）
    vec3 col = acesToneMap(hdr);
    col = pow(col, vec3(1.0 / 2.2)); // Gamma 校正（显示器 sRGB）
    FragColor = vec4(col, 1.0);
}

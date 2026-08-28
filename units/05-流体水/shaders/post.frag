#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uSceneTex;

// Narkowicz ACES 近似：高光柔和滚降，暗部更沉稳
vec3 acesTonemap(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 col = texture(uSceneTex, vUV).rgb;

    col = acesTonemap(col * 1.02);       // 轻微提亮后做电影感色调映射
    col = pow(col, vec3(1.0 / 2.2));     // gamma 校正

    // 轻微暗角，把视线聚到水缸上
    vec2 q = vUV * 2.0 - 1.0;
    col *= 1.0 - 0.10 * dot(q, q);

    FragColor = vec4(col, 1.0);
}

#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vLightPos;
out vec4 FragColor;

uniform vec3  uCamPos;
uniform vec3  uColor;
uniform float uAlpha;
uniform int   uMode;   // 0 普通物体  1 棋盘地面  2 玻璃  3 水流柱  4 水花
uniform float uTime;
uniform sampler2DShadow uShadowMap;
uniform float uShadowBias;

const vec3 LIGHT_DIR = normalize(vec3(-0.35, 0.9, 0.45));
const vec3 LIGHT_COL = vec3(1.0, 0.93, 0.82);   // 阳光：暖白
const vec3 AMBIENT   = vec3(0.36, 0.38, 0.42);  // 天光：冷蓝灰（暗面自然偏冷）

// 每像素随机旋转角，用来旋转 PCF 采样核（打散 9 级灰度的分层伪影）
vec2 randRot(vec2 seed) {
    float a = fract(sin(dot(seed, vec2(127.1, 311.7))) * 43758.5453) * 6.2831853;
    return vec2(cos(a), sin(a));
}

// PCF 3×3 软阴影（外面是光照空间裁剪范围外 → 无阴影）
float shadowFactor(vec4 lp, float bias) {
    vec3 pc = lp.xyz / lp.w * 0.5 + 0.5;
    if (pc.z > 1.0) return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    vec2 rot = randRot(pc.xy * 617.0);
    float s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++) {
            vec2 o  = vec2(float(x), float(y));
            vec2 ro = vec2(o.x * rot.x - o.y * rot.y, o.x * rot.y + o.y * rot.x) * texel;
            s += texture(uShadowMap, vec3(pc.xy + ro, pc.z - bias));
        }
    return s / 9.0;
}

void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;   // 玻璃从内侧看时翻转法线
    vec3 v = normalize(uCamPos - vWorldPos);

    float diff = max(dot(n, LIGHT_DIR), 0.0);
    vec3  hv   = normalize(LIGHT_DIR + v);
    float spec = pow(max(dot(n, hv), 0.0), 64.0);

    if (uMode == 2) {
        // 玻璃：正视几乎透明，掠射角（边缘）菲涅尔泛白 + 高光
        float fresnel = pow(1.0 - abs(dot(n, v)), 3.0);
        vec3  col   = mix(uColor, LIGHT_COL * 1.08, fresnel) * (0.55 + 0.45 * diff) + LIGHT_COL * spec * 0.7;
        float alpha = clamp(uAlpha + fresnel * 0.55 + spec * 0.5, 0.0, 1.0);
        FragColor = vec4(col, alpha);
        return;
    }
    if (uMode == 3) {
        // 水流柱：中心几乎全透明、菲涅尔边缘白亮 + 纵向下冲流纹
        // 只随高度滚动，不做绕壁螺旋——真实下落水流不会旋转
        float fres = pow(1.0 - abs(dot(n, v)), 2.0);
        float flow = 0.5 + 0.5 * sin(vWorldPos.y * 45.0 - uTime * 40.0);
        flow *= 0.55 + 0.45 * sin(vWorldPos.y * 83.0 - uTime * 53.0);
        vec3 col = mix(uColor, vec3(0.98, 0.99, 0.98), clamp(fres + flow * 0.3, 0.0, 1.0)) + LIGHT_COL * spec * 0.8;
        float alpha = clamp(uAlpha * (0.22 + fres * 1.8 + flow * 0.4), 0.0, 0.95);
        FragColor = vec4(col, alpha);
        return;
    }
    if (uMode == 4) {
        // 水花水珠：高环境光保持白亮 + 锐利高光
        vec3 col = uColor * (0.85 + 0.15 * diff) + LIGHT_COL * (spec * 1.0 + pow(max(dot(n, hv), 0.0), 300.0) * 0.8);
        FragColor = vec4(col, uAlpha);
        return;
    }

    vec3 base = uColor;
    if (uMode == 1) {
        // 地面淡色棋盘，远处渐暗（伪 AO，增加纵深感）
        vec2 c = floor(vWorldPos.xz * 1.2);
        base = mix(vec3(0.82, 0.80, 0.77), vec3(0.68, 0.66, 0.63), mod(c.x + c.y, 2.0));
        base *= 1.0 - clamp(length(vWorldPos.xz) * 0.22, 0.0, 0.35);
    }

    float sh  = shadowFactor(vLightPos, uShadowBias);
    vec3  col = base * (AMBIENT + LIGHT_COL * 0.65 * diff) * mix(0.5, 1.0, sh)
              + LIGHT_COL * spec * 0.35 * sh;
    FragColor = vec4(col, uAlpha);
}

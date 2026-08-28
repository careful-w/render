#version 330 core
in vec3 vWorldPos;
in vec2 vUV;
in vec4 vLightPos;
out vec4 FragColor;

uniform sampler2D uHeightTex;
uniform vec4 uRect;     // 水面内区 (x0, z0, x1, z1)
uniform vec3 uCamPos;
uniform sampler2DShadow uShadowMap;
uniform float uShadowBias;
uniform int   uShadowPass;   // 1 = 阴影贴图 pass（只写深度，不做光照）

// ---- 光线追踪场景参数（CPU 每帧上传）----
uniform vec4 uFloaterPos[5];    // xyz = 位置, w = 形状（0 盒 / 1 球）
uniform vec4 uFloaterParam[5];  // xyz = 颜色, w = 尺寸
uniform vec3 uLightDirW;        // 指向光源（世界空间）
uniform vec3 uLightColW;        // 光源颜色（暖白）
uniform float uInnerX, uInnerZ; // 缸内半宽/半深

const vec3 LIGHT_DIR = normalize(vec3(-0.35, 0.9, 0.45));

// PCF 3×3 软阴影（阴影贴图）
vec2 randRot(vec2 seed) {
    float a = fract(sin(dot(seed, vec2(127.1, 311.7))) * 43758.5453) * 6.2831853;
    return vec2(cos(a), sin(a));
}
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

// ================= 光线追踪 =================
// 球求交：返回 t（<0 未命中）
float raySphere(vec3 ro, vec3 rd, vec3 c, float r) {
    vec3 oc = ro - c;
    float b  = dot(oc, rd);
    float cc = dot(oc, oc) - r * r;
    float h  = b * b - cc;
    if (h < 0.0) return -1.0;
    float t = -b - sqrt(h);
    return t > 0.0 ? t : -1.0;
}

// 追踪浮体（立方体用外接球近似），返回命中 id
int traceFloaters(vec3 ro, vec3 rd, out float tHit) {
    int id = -1; tHit = 1e9;
    for (int i = 0; i < 5; i++) {
        float r = uFloaterParam[i].w * (uFloaterPos[i].w > 0.5 ? 1.0 : 1.6);
        float t = raySphere(ro, rd, uFloaterPos[i].xyz, r);
        if (t > 0.0 && t < tHit) { tHit = t; id = i; }
    }
    return id;
}

// 追踪软阴影：命中点向光源发一条射线，被浮体遮挡则变暗
float traceShadow(vec3 p) {
    float tHit;
    int id = traceFloaters(p + uLightDirW * 0.03, uLightDirW, tHit);
    return id < 0 ? 1.0 : 0.5;
}

// 与地面 y=0 和玻璃缸壁求交，返回最近命中
float rayGround(vec3 ro, vec3 rd) {
    if (rd.y > -1e-5) return -1.0;
    return -ro.y / rd.y;
}
float rayWalls(vec3 ro, vec3 rd) {
    float t = 1e9;
    float tx = (rd.x > 1e-5) ? ( uInnerX - ro.x) / rd.x : ((rd.x < -1e-5) ? (-uInnerX - ro.x) / rd.x : 1e9);
    float tz = (rd.z > 1e-5) ? ( uInnerZ - ro.z) / rd.z : ((rd.z < -1e-5) ? (-uInnerZ - ro.z) / rd.z : 1e9);
    if (tx > 0.0) t = min(t, tx);
    if (tz > 0.0) t = min(t, tz);
    return t < 1e8 ? t : -1.0;
}

// 反射光线追踪主函数
vec3 traceReflection(vec3 ro, vec3 rd) {
    // 1. 浮体（反射里最显眼）
    float tF;
    int   id   = traceFloaters(ro, rd, tF);
    float tG   = rayGround(ro, rd);
    float tW   = rayWalls(ro, rd);

    if (id >= 0 && (tG < 0.0 || tF < tG) && (tW < 0.0 || tF < tW)) {
        vec3  p    = ro + rd * tF;
        vec3  nHit = normalize(p - uFloaterPos[id].xyz);
        float sh   = traceShadow(p);
        float diff = max(dot(nHit, LIGHT_DIR), 0.0);
        vec3  col  = uFloaterParam[id].xyz * (vec3(0.36, 0.38, 0.42) + uLightColW * 0.65 * diff) * sh;
        // 高光
        vec3  hv   = normalize(LIGHT_DIR - rd);
        col += uLightColW * pow(max(dot(nHit, hv), 0.0), 64.0) * 0.5 * sh;
        return col;
    }

    // 2. 玻璃缸壁（反射出缸壁亮边）
    if (tW > 0.0 && (tG < 0.0 || tW < tG)) {
        return vec3(0.88, 0.92, 0.93) * uLightColW * 0.9;
    }

    // 3. 地面（缸外地板倒影）
    if (tG > 0.0) {
        vec3 gp = ro + rd * tG;
        vec2 c  = floor(gp.xz * 1.2);
        vec3 col = mix(vec3(0.82, 0.80, 0.77), vec3(0.68, 0.66, 0.63), mod(c.x + c.y, 2.0));
        col *= 1.0 - clamp(length(gp.xz) * 0.22, 0.0, 0.35);
        return col * uLightColW * 0.9;
    }

    // 4. 天空：反射角越朝上越天蓝，越平越偏暖白（地平线）
    float skyAmt = clamp(rd.y * 2.0, 0.0, 1.0);
    return mix(vec3(0.95, 0.92, 0.86), vec3(0.64, 0.77, 0.86), skyAmt);
}

void main() {
    if (uShadowPass == 1) {
        FragColor = vec4(1.0);   // 阴影 pass：颜色被丢弃
        return;
    }

    // 中央差分从高度纹理重建法线（纹素间距换算成世界距离）
    vec2  texel = 1.0 / vec2(textureSize(uHeightTex, 0));
    float hL = texture(uHeightTex, vUV - vec2(texel.x, 0.0)).r;
    float hR = texture(uHeightTex, vUV + vec2(texel.x, 0.0)).r;
    float hD = texture(uHeightTex, vUV - vec2(0.0, texel.y)).r;
    float hU = texture(uHeightTex, vUV + vec2(0.0, texel.y)).r;
    float dx = (uRect.z - uRect.x) * texel.x;
    float dz = (uRect.w - uRect.y) * texel.y;
    vec3  n  = normalize(vec3(-(hR - hL) / (2.0 * dx), 1.0, -(hU - hD) / (2.0 * dz)));

    vec3  v       = normalize(uCamPos - vWorldPos);
    float fresnel = pow(1.0 - max(dot(n, v), 0.0), 3.0);

    float hgt = texture(uHeightTex, vUV).r;

    // 深浅水色随波高变化（清水：色浅、饱和度低，参考真实清水照片）
    vec3 deep    = vec3(0.08, 0.26, 0.34);
    vec3 shallow = vec3(0.34, 0.56, 0.58);
    vec3 col = mix(deep, shallow, clamp(0.5 + hgt * 10.0, 0.0, 1.0));

    // 光线追踪反射：浮体/缸壁/地面/天空的倒影（替代原来单色天空反射）
    vec3 reflDir = reflect(-v, n);
    vec3 rtCol   = traceReflection(vWorldPos + n * 0.02, reflDir);
    col = mix(col, rtCol, clamp(fresnel * 1.55, 0.0, 1.0));

    // 阴影：漂浮物/水管的影子落在水面波纹上
    float sh = shadowFactor(vLightPos, uShadowBias);
    col *= mix(0.45, 1.0, sh);

    float diff = max(dot(n, LIGHT_DIR), 0.0);
    col *= vec3(0.58, 0.60, 0.64) + uLightColW * 0.45 * diff;   // 天光冷 + 阳光暖

    vec3  hv      = normalize(LIGHT_DIR + v);
    float spec    = pow(max(dot(n, hv), 0.0), 120.0);
    float glitter = pow(max(dot(n, hv), 0.0), 400.0) * 1.3;   // 波光粼粼：极尖锐高光闪烁
    float sheen   = pow(max(dot(n, hv), 0.0), 12.0) * 0.15;   // 宽幅柔光
    col += uLightColW * (spec + glitter + sheen) * sh;

    // 波浪陡处泛白，近似冲击点周围的泡沫
    float slope = 1.0 - n.y;
    float foam  = smoothstep(0.015, 0.10, slope + abs(hgt) * 0.6);
    col = mix(col, uLightColW * 0.95, foam * 0.55);

    float alpha = clamp(0.50 + fresnel * 0.40 + foam * 0.3, 0.0, 0.92);
    FragColor = vec4(col, alpha);
}

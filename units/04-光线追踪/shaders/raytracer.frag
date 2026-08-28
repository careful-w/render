#version 330 core
// 路径追踪片段着色器：为当前像素生成相机光线，递归反弹累积颜色
// 每帧每像素投 uSpp 条采样光线（随机抖动），由 CPU 端累积纹理取平均实现收敛与抗锯齿
// 漫反射面用 NEE 直接采样太阳（重要性采样），直接光零方差，白噪点消失
out vec4 FragColor;

// ============================ Uniform ============================
uniform vec2  uResolution; // 窗口分辨率（像素）
uniform vec3  uCamPos;     // 相机位置
uniform vec3  uCamTarget;  // 观察目标点
uniform float uFov;        // 垂直视场角（度）
uniform float uSeed;       // 帧号，用作随机种子（每帧变化）
uniform int   uMaxBounce;  // 最大光线反弹次数
uniform int   uSpp;        // 每帧每像素采样数（抗噪：越大收敛越快）

const float PI  = 3.14159265359;
const float INF = 1e30;
const float EPS = 1e-3;    // 交点偏移量，避免光线自相交

// 太阳：有角半径的方向光源（面积光），是产生软阴影的关键
// 高悬在相机后方上空（不出现在画面里），给球体顶部带来镜面高光
const vec3  SUN_DIR      = vec3(-0.199, 0.895, -0.398); // 指向太阳的单位向量
const float SUN_COS_HALF = 0.99619;                      // cos(5°)
const vec3  SUN_RADIANCE   = vec3(1.0, 0.95, 0.85) * 40.0; // 太阳盘亮度（镜面高光直接反射可见）
const vec3  SUN_IRRADIANCE = vec3(1.0, 0.95, 0.85) * 4.0;  // 太阳辐照度（NEE 直接光，配合显示端曝光校准）

// ======================= 随机数（PCG 哈希） =======================
// 纯数学运算的伪随机数生成器，不依赖噪声纹理，每次调用产生 [0,1) 均匀分布
uint gState;

float rnd() {
    gState = gState * 747796405u + 2891336453u;
    uint w = ((gState >> ((gState >> 28u) + 4u)) ^ gState) * 277803737u;
    w = (w >> 22u) ^ w;
    return float(w) / 4294967295.0;
}

// 用像素坐标 + 帧号播种，保证：同一帧内各像素序列不同、不同帧之间完全不同
void initRng(vec2 pixel) {
    gState = uint(pixel.x * 1973.0 + pixel.y * 9277.0 + uSeed * 26699.0) | 1u;
}

// 单位球面上均匀随机方向（漫反射重要性采样用）
vec3 randomUnitVector() {
    float z = rnd() * 2.0 - 1.0;
    float a = rnd() * 2.0 * PI;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return vec3(r * cos(a), r * sin(a), z);
}

// ============================ 场景结构 ============================
struct Ray  { vec3 o; vec3 d; bool shadow; }; // 光线：起点 + 方向 + 阴影光线标志（直接光测试）
struct Hit  { float t; vec3 p; vec3 n; int id; bool front; }; // 交点信息

// 材质类型
const int MAT_LAMBERT = 0; // 漫反射
const int MAT_METAL   = 1; // 金属（镜面/磨砂反射）
const int MAT_GLASS   = 2; // 玻璃（折射 + 菲涅尔反射）

const int NUM = 24; // 球体总数（含地面大球）

// 0 中央大玻璃球（半透明 + 高反射，画面主角）
// 1 左侧银色金属球  2 右侧金球（仿 RTIOW 布局 r=1：相机在 (13,2,3) 看向原点，x 轴接近纵深，
//   ±4 的 x 间距才能在屏幕上左中右散开；银球 z=1.5 避免被玻璃球挡住；球心 y = 半径贴地）
// 3 彩色斑点球  4 红色小球  5 蓝色小球（三个小球在玻璃球前左侧，避开其屏幕投影）
// 6 远处绿色球  7 远处玻璃球（透过它能看到其它球）
// 8 远处深色球  9 地面（超大球体模拟平面，经典 RTIOW 手法）
// 10~23 散布小球：铺满画面左右（画面横向 ≈ z 轴），复刻 RTIOW 封面构图
vec3  sphCenter[NUM] = vec3[](
    vec3(0.0,   1.0,    0.0),
    vec3(-4.0,  1.0,    1.5),
    vec3(4.0,   1.0,    0.0),
    vec3(0.6,   0.2,    1.7),
    vec3(1.05,  0.2,    1.25),
    vec3(0.2,   0.2,    1.35),
    vec3(-2.2,  0.3,   -2.3),
    vec3(1.5,   0.3,   -2.0),
    vec3(-0.8,  0.45,  -3.0),
    vec3(0.0, -1000.0,  0.0),
    vec3(-0.4,  0.2,    1.1),   // 10 棕
    vec3(-1.2,  0.2,    1.8),   // 11 黄
    vec3(1.8,   0.2,    0.8),   // 12 小银（金属）
    vec3(-0.5,  0.15,   2.6),   // 13 紫
    vec3(2.6,   0.2,    1.6),   // 14 青
    vec3(1.2,   0.15,   2.2),   // 15 小玻璃
    vec3(-2.5,  0.2,    0.9),   // 16 橙
    vec3(-0.3,  0.15,  -1.9),   // 17 粉
    vec3(2.2,   0.2,   -0.7),   // 18 深棕
    vec3(-3.0,  0.2,   -1.2),   // 19 小金（金属）
    vec3(0.8,   0.15,  -2.6),   // 20 浅蓝
    vec3(-1.8,  0.2,   -3.4),   // 21 绯红
    vec3(3.2,   0.2,    1.0),   // 22 米白
    vec3(-3.6,  0.2,    2.4)    // 23 橄榄
);
float sphRadius[NUM] = float[](
    1.0, 1.0, 1.0, 0.2, 0.2, 0.2, 0.3, 0.3, 0.45, 1000.0,
    0.2, 0.2, 0.2, 0.15, 0.2, 0.15, 0.2, 0.15, 0.2, 0.2, 0.15, 0.2, 0.2, 0.2
);
int   sphMat[NUM]    = int[](
    MAT_GLASS, MAT_METAL, MAT_METAL, MAT_LAMBERT, MAT_LAMBERT,
    MAT_LAMBERT, MAT_LAMBERT, MAT_GLASS, MAT_LAMBERT, MAT_LAMBERT,
    MAT_LAMBERT, MAT_LAMBERT, MAT_METAL, MAT_LAMBERT, MAT_LAMBERT,
    MAT_GLASS, MAT_LAMBERT, MAT_LAMBERT, MAT_LAMBERT, MAT_METAL,
    MAT_LAMBERT, MAT_LAMBERT, MAT_LAMBERT, MAT_LAMBERT
);

// ======================= 光线-球体求交 =======================
// 解二次方程 |o + t*d - c|^2 = r^2，返回最近的根
bool hitSphere(Ray r, int id, float tMin, float tMax, out float t) {
    vec3  oc    = r.o - sphCenter[id];
    float a     = dot(r.d, r.d);
    float halfB = dot(oc, r.d);
    float c     = dot(oc, oc) - sphRadius[id] * sphRadius[id];
    float disc  = halfB * halfB - a * c;   // 判别式 < 0 则无交点
    if (disc < 0.0) return false;
    float sq   = sqrt(disc);
    float root = (-halfB - sq) / a;        // 优先取较近交点
    if (root < tMin || root > tMax) {
        root = (-halfB + sq) / a;
        if (root < tMin || root > tMax) return false;
    }
    t = root;
    return true;
}

// 遍历场景所有物体，找最近交点；法线始终调整为面向入射方向
bool intersectScene(Ray r, out Hit h) {
    float tNear = INF;
    int   id    = -1;
    for (int i = 0; i < NUM; i++) {
        float t;
        if (hitSphere(r, i, EPS, tNear, t)) {
            tNear = t;
            id    = i;
        }
    }
    if (id < 0) return false;
    h.t     = tNear;
    h.p     = r.o + r.d * tNear;
    h.n     = (h.p - sphCenter[id]) / sphRadius[id];
    h.front = dot(r.d, h.n) < 0.0;         // 从外部击中？
    if (!h.front) h.n = -h.n;              // 从内部击中（折射光线）则翻转法线
    h.id    = id;
    return true;
}

// ============================ 颜色函数 ============================
// 3D 值噪声：光滑插值的格子哈希，用于彩色斑点纹理
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f); // 平滑插值
    return mix(
        mix(mix(hash13(i), hash13(i + vec3(1,0,0)), f.x),
            mix(hash13(i + vec3(0,1,0)), hash13(i + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash13(i + vec3(0,0,1)), hash13(i + vec3(1,0,1)), f.x),
            mix(hash13(i + vec3(0,1,1)), hash13(i + vec3(1,1,1)), f.x), f.y),
        f.z);
}

// 彩色斑点球的反照率：三通道噪声叠加成彩色大理石纹
vec3 speckleAlbedo(vec3 p) {
    vec3 c1 = vec3(vnoise(p * 4.0),        vnoise(p * 4.0 + 31.7),   vnoise(p * 4.0 + 73.1));
    vec3 c2 = vec3(vnoise(p * 13.0 + 11.3), vnoise(p * 13.0 + 47.9), vnoise(p * 13.0 + 89.5));
    vec3 n  = 0.6 * c1 + 0.4 * c2;
    return clamp(0.2 + 0.8 * n, 0.0, 1.0);
}

// 地面：暖色棋盘格（RTIOW 风格中灰反照率，控制多次反弹放大与动态比）
vec3 groundColor(vec3 p) {
    vec2 c   = floor(p.xz * 1.5);
    float chk = mod(c.x + c.y, 2.0);
    return mix(vec3(0.65, 0.60, 0.52), vec3(0.35, 0.30, 0.25), chk);
}

// 各球基础反照率（漫反射球用更饱和的颜色，避免强天光下发白）
vec3 albedoOf(int id) {
    if (id == 1)  return vec3(0.90, 0.90, 0.95);  // 银
    if (id == 2)  return vec3(0.95, 0.65, 0.25);  // 金
    if (id == 4)  return vec3(0.85, 0.12, 0.10);  // 红
    if (id == 5)  return vec3(0.12, 0.28, 0.80);  // 蓝
    if (id == 6)  return vec3(0.25, 0.60, 0.25);  // 绿
    if (id == 8)  return vec3(0.62, 0.55, 0.50);  // 深灰棕
    if (id == 10) return vec3(0.55, 0.35, 0.20);  // 棕
    if (id == 11) return vec3(0.85, 0.70, 0.15);  // 黄
    if (id == 12) return vec3(0.85, 0.85, 0.90);  // 小银
    if (id == 13) return vec3(0.50, 0.25, 0.60);  // 紫
    if (id == 14) return vec3(0.20, 0.55, 0.50);  // 青
    if (id == 16) return vec3(0.90, 0.45, 0.10);  // 橙
    if (id == 17) return vec3(0.90, 0.50, 0.55);  // 粉
    if (id == 18) return vec3(0.45, 0.30, 0.20);  // 深棕
    if (id == 19) return vec3(0.90, 0.70, 0.30);  // 小金
    if (id == 20) return vec3(0.40, 0.60, 0.85);  // 浅蓝
    if (id == 21) return vec3(0.70, 0.15, 0.20);  // 绯红
    if (id == 22) return vec3(0.85, 0.83, 0.78);  // 米白
    if (id == 23) return vec3(0.45, 0.55, 0.25);  // 橄榄
    return vec3(0.5);
}

// ============================ 材质散射 ============================
// 太阳盘上随机采样一个方向（面积光采样，软阴影的直接光）
vec3 sunDiskDir() {
    vec3 basis = normalize(cross(SUN_DIR, vec3(0.0, 1.0, 0.0)));
    if (dot(basis, basis) < 0.01) basis = vec3(1.0, 0.0, 0.0);
    vec3  bit = cross(SUN_DIR, basis);
    float ang = sqrt(rnd()) * radians(5.0); // 盘内均匀（sqrt 保证面密度均匀）
    float th  = rnd() * 2.0 * PI;
    return normalize(cos(ang) * SUN_DIR + sin(ang) * (cos(th) * basis + sin(th) * bit));
}

// 根据材质类型决定光线的下一次反弹方向与能量衰减
// 返回 false 表示光线被完全吸收（路径终止）
bool scatter(Ray r, Hit h, out vec3 atten, out Ray scattered) {
    vec3 rd = normalize(r.d);

    // ---- 漫反射：一半采样太阳直接光（NEE），一半余弦半球间接光 ----
    if (sphMat[h.id] == MAT_LAMBERT) {
        vec3 alb;
        if (h.id == 9)      alb = groundColor(h.p);        // 地面棋盘格
        else if (h.id == 3) alb = speckleAlbedo(h.p * 6.0); // 彩色斑点球
        else                alb = albedoOf(h.id);

        if (rnd() < 0.5) {
            // 直接光：朝太阳盘采样（阴影光线），命中天空=受光，撞物=被遮挡（软阴影）
            vec3 d = sunDiskDir();
            float cosT = dot(d, h.n);
            if (cosT <= 0.0) return false;    // 太阳在表面背面
            scattered = Ray(h.p, d, true);
            atten = alb * (2.0 * cosT / PI);  // BRDF ρ/π·cos ÷0.5 分支概率，命中太阳时贡献 SUN_IRRADIANCE
            return true;
        }
        // 间接光：余弦加权半球采样（重要性采样，收敛更快）
        vec3 dir = normalize(h.n + randomUnitVector());
        scattered = Ray(h.p, dir, false);
        atten = alb * 2.0;                    // ÷0.5 分支概率
        return true;
    }

    // ---- 金属：反射方向 + fuzz 随机扰动（磨砂感） ----
    if (sphMat[h.id] == MAT_METAL) {
        vec3  refl = reflect(rd, h.n);
        // 大银/大金接近镜面（参考图里金属球清晰反射周围环境），小金属球略磨砂
        float fuzz = (h.id == 1) ? 0.03 : ((h.id == 2) ? 0.05 : 0.1);
        scattered  = Ray(h.p, normalize(refl + fuzz * randomUnitVector()), false);
        if (dot(scattered.d, h.n) <= 0.0) return false; // 反射进表面 → 被吸收
        atten = albedoOf(h.id); // 金属反射率 = 颜色（能量守恒）
        return true;
    }

    // ---- 玻璃：折射 + 菲涅尔反射（按概率二选一） ----
    float ior        = 1.5;                                  // 玻璃折射率
    float ri         = h.front ? (1.0 / ior) : ior;          // 入射/出射介质比
    float cosTheta   = min(dot(-rd, h.n), 1.0);
    float sinTheta   = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    bool  cannotRefract = ri * sinTheta > 1.0;               // 全内反射

    // Schlick 菲涅尔近似：入射角越大，反射比例越高
    float r0      = (1.0 - ri) / (1.0 + ri);
    r0            = r0 * r0;
    float fresnel = r0 + (1.0 - r0) * pow(1.0 - cosTheta, 5.0);

    if (cannotRefract || fresnel > rnd()) {
        scattered = Ray(h.p, reflect(rd, h.n), false);       // 反射
    } else {
        // 折射（斯涅尔定律的向量形式）
        vec3 rOutPerp = ri * (rd + cosTheta * h.n);
        vec3 rOutPar  = -sqrt(max(0.0, 1.0 - dot(rOutPerp, rOutPerp))) * h.n;
        scattered = Ray(h.p, rOutPerp + rOutPar, false);     // 折射
    }
    atten = vec3(1.0); // 无色玻璃不吸收能量
    return true;
}

// ============================ 天空 ============================
// withSun=false：漫反射间接光线看到的天空要去掉太阳盘——
// NEE 已经精确算过太阳直接光，再让随机光线撞到太阳盘就是双重计数（能量偏亮 + 火花噪点）
vec3 skyColor(vec3 d, bool withSun) {
    // 地平线白 → 天顶蓝 的渐变（RTIOW 经典天空），用幂函数让蓝色更快显现
    float t = pow(0.5 * (d.y + 1.0), 0.75);
    vec3 col = mix(vec3(0.95, 0.95, 1.0), vec3(0.35, 0.55, 1.0), t);
    // 太阳：有角半径的光盘，柔和边缘。仅镜面路径（金属/玻璃）反射太阳盘产生高光
    if (withSun) {
        float sunDot = dot(normalize(d), SUN_DIR);
        float sun    = smoothstep(SUN_COS_HALF - 0.015, SUN_COS_HALF + 0.015, sunDot);
        col += sun * SUN_RADIANCE;
    }
    // 地平线附近略微压暗，增强纵深
    col *= 1.0 - 0.15 * exp(-10.0 * max(0.0, d.y));
    return col;
}

// ======================= 路径追踪主循环 =======================
// 迭代式递归：光线不断反弹，累积吞吐量（throughput）与天空贡献
vec3 radiance(Ray r) {
    vec3 color       = vec3(0.0);
    vec3 throughput  = vec3(1.0);
    bool fromDiffuse = false;                // 上一跳是否漫反射（决定天空是否含太阳盘，防双重计数）
    int  bounce      = 0;
    for (int i = 0; i < 24; i++) {           // 循环上限>反弹数：阴影光线穿玻璃会额外占迭代
        Hit h;
        if (!intersectScene(r, h)) {         // 未命中任何物体 → 命中天空（光源）
            if (r.shadow) color += throughput * SUN_IRRADIANCE;          // 阴影光线：太阳未被遮挡
            else          color += throughput * skyColor(r.d, !fromDiffuse);
            break;
        }
        if (r.shadow) {
            // 阴影光线撞到玻璃：近似让阳光穿透（每面衰减 6%），玻璃球影心留亮斑（廉价焦散）
            if (sphMat[h.id] == MAT_GLASS) {
                throughput *= 0.94;
                r = Ray(h.p, r.d, true);
                continue;
            }
            break;                           // 撞到不透明物 → 被遮挡，直接光为 0
        }
        vec3 atten;
        Ray  scattered;
        if (!scatter(r, h, atten, scattered)) break; // 被吸收
        fromDiffuse = (sphMat[h.id] == MAT_LAMBERT) && !scattered.shadow;
        throughput *= atten;                 // 能量随反弹衰减
        r = scattered;
        if (++bounce >= uMaxBounce) break;   // 达到最大反弹次数
    }
    return color;
}

// ============================ 主函数 ============================
void main() {
    // 每帧每像素投 uSpp 条光线取平均：倍增收敛速度，消除单采样闪烁
    vec3 col = vec3(0.0);
    for (int s = 0; s < uSpp; s++) {
        initRng(gl_FragCoord.xy + vec2(37.0 * float(s), 17.0 * float(s)));

        // 亚像素随机抖动：配合多帧累积实现抗锯齿
        vec2 jitter = vec2(rnd(), rnd()) - 0.5;
        // NDC：除以 res.y 统一纵横比，x 方向天然乘以 aspect（勿再乘，否则画面拉长）
        vec2 ndc = (2.0 * (gl_FragCoord.xy + jitter) - uResolution) / uResolution.y;

        // 相机基向量（lookAt）
        vec3  forward = normalize(uCamTarget - uCamPos);
        vec3  right   = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
        vec3  up      = cross(right, forward);
        float tanHalf = tan(radians(uFov) * 0.5);

        vec3 dir = normalize(forward
                   + ndc.x * tanHalf * right
                   + ndc.y * tanHalf * up);

        col += radiance(Ray(uCamPos, dir, false));
    }
    col /= float(uSpp);

    // 裁剪极端亮度抑制高光噪点闪烁
    FragColor = vec4(min(col, vec3(15.0)), 1.0);
}

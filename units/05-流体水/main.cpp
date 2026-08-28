#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

/**
 * 05 · 流体水（高度场波动方程 + 浮体动力学）
 *
 * 原理：
 *   水面用 128×128 的高度场表示，CPU 上求解二维波动方程：
 *       v += c²·∇²h·dt   （拉普拉斯算子 = 四邻域均值 - 自身）
 *       h += v·dt
 *   水流从玻璃缸上方的水管冲下，在冲击点持续压低水面（对 v 施加负扰动），
 *   涟漪由波动方程自然扩散、碰到玻璃壁反射（Neumann 边界）。
 *
 *   漂浮物 = 弹簧浮力（拉向水面）+ 波面梯度推力（顺波往低处滑）
 *          + 冲击点径向推流，姿态随水面法线倾斜，随波起伏摇摆。
 *
 *   渲染：高度场每帧上传 R32F 纹理，水面网格在顶点着色器中位移，
 *   片段着色器用中央差分重建法线做菲涅尔/高光/泡沫；玻璃缸用
 *   "背面 → 水流柱 → 水面 → 正面"的顺序做透明混合。
 *
 *   真实感：2048² 阴影贴图（光源正交投影，浮体影子落在波纹上，
 *   PCF 3×3 软阴影）+ 后处理 pass（ACES 色调映射 / gamma / 暗角）。
 *
 * 操作：
 *   鼠标左键拖拽  绕水缸旋转        滚轮   推近/拉远
 *   空格          开/关水龙头注水    R      重置（空缸 + 视角）
 *   P             保存截图           ESC    退出
 *
 * 启动时水缸是空的：浮体躺在缸底，按空格开闸注水，水位 0.10m/s 缓慢上涨
 * （约 7 秒注满），浮体随水位上升自然浮起；关闭水龙头后水位保持、水面恢复平静。
 *
 * 命令行参数：
 *   OpenGL.exe --snapshot 300   隐藏窗口模拟 300 帧后自动截图退出（用于验证）
 */

// 从文件读取 shader 源码
std::string readShaderFile(const char* filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cout << "ERROR::SHADER::FILE_NOT_FOUND: " << filePath << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// 编译单个着色器
unsigned int compileShader(GLenum type, const char* path) {
    std::string src = readShaderFile(path);
    if (src.empty()) return 0;
    const char* code = src.c_str();

    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &code, NULL);
    glCompileShader(shader);

    int  success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::COMPILE(" << path << ")\n" << infoLog << std::endl;
    }
    return shader;
}

// 创建着色器程序（顶点 + 片段）
unsigned int createProgram(const char* vertexPath, const char* fragmentPath) {
    unsigned int vs = compileShader(GL_VERTEX_SHADER, vertexPath);
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fragmentPath);
    if (!vs || !fs) return 0;

    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    int  success;
    char infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM\n" << infoLog << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// ============================ 场景常量 ============================
const int   SIM_N   = 128;            // 高度场分辨率
const float TANK_X  = 1.0f;           // 玻璃缸半宽（x）
const float TANK_Z  = 0.7f;           // 玻璃缸半深（z）
const float TANK_H  = 1.2f;           // 玻璃缸高度
const float WALL_T  = 0.04f;          // 玻璃壁厚
const float INNER_X = TANK_X - WALL_T; // 水面内区半宽
const float INNER_Z = TANK_Z - WALL_T;
const float MAX_WATER_Y = 0.72f;      // 最高水位（注满时）
const float FILL_RATE   = 0.10f;      // 注水速率 m/s（约 7 秒注满）
const float IMPACT_X = 0.45f;         // 水流冲击点
const float IMPACT_Z = 0.0f;
const float NOZZLE_Y = 1.72f;         // 水管出水口高度

// ============================ 相机状态 ============================
glm::vec3 camTarget(0.2f, 0.65f, 0.0f);
float radius = 4.2f;
float yaw    = 0.65f;
float pitch  = 0.32f;
float fov    = 45.0f;
glm::vec3 camPos(0.0f);

float waterLevel = 0.0f;   // 当前水位（启动时水缸是空的，注水后慢慢上涨）
bool  pouring    = false;  // 是否正在注水（启动时水龙头关闭，按空格开/关）

// 鼠标交互状态
bool   leftPressed = false;
double lastX = 0.0, lastY = 0.0;

// 帧缓冲尺寸
int  fbWidth = 1280, fbHeight = 720;
bool fbResized = false;

// ============================ 回调函数 ============================
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    fbWidth = width; fbHeight = height; fbResized = true;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        leftPressed = (action == GLFW_PRESS);
        if (leftPressed) glfwGetCursorPos(window, &lastX, &lastY);
    }
}

void cursor_pos_callback(GLFWwindow* window, double x, double y) {
    if (!leftPressed) return;
    double dx = x - lastX, dy = y - lastY;
    lastX = x; lastY = y;
    yaw   -= (float)dx * 0.005f;
    pitch += (float)dy * 0.005f;
    pitch  = glm::clamp(pitch, -0.1f, 1.45f); // 不许钻到地面下
}

void scroll_callback(GLFWwindow* window, double xOffset, double yOffset) {
    radius *= (float)std::pow(0.9, yOffset);
    radius  = glm::clamp(radius, 1.8f, 12.0f);
}

// ============================ 波动方程模拟 ============================
std::vector<float> simH(SIM_N * SIM_N, 0.0f);   // 水面高度偏移
std::vector<float> simV(SIM_N * SIM_N, 0.0f);   // 垂直速度
std::vector<float> simLap(SIM_N * SIM_N, 0.0f); // 拉普拉斯缓存

inline int simIdx(int x, int y) { return y * SIM_N + x; }

// 世界坐标 → 网格浮点坐标
inline float worldToGridX(float wx) { return (wx + INNER_X) / (2.0f * INNER_X) * (SIM_N - 1); }
inline float worldToGridZ(float wz) { return (wz + INNER_Z) / (2.0f * INNER_Z) * (SIM_N - 1); }

// 在世界坐标 (wx, wz) 处以高斯权重扰动垂直速度（水流冲击）
void disturb(float wx, float wz, float cellRadius, float amount) {
    float gx = worldToGridX(wx), gz = worldToGridZ(wz);
    int r = (int)std::ceil(cellRadius);
    for (int j = -r; j <= r; j++) {
        for (int i = -r; i <= r; i++) {
            int x = (int)gx + i, y = (int)gz + j;
            if (x < 1 || x >= SIM_N - 1 || y < 1 || y >= SIM_N - 1) continue;
            float d2 = (float)(i * i + j * j) / (cellRadius * cellRadius);
            simV[simIdx(x, y)] += amount * std::exp(-3.0f * d2);
        }
    }
}

// 波动方程步进：v += c²·∇²h·dt，h += v·dt
void stepWave(float dt) {
    const float K = 2200.0f;   // 波速²（CFL 稳定：K·dt² ≪ 4）
    // 注水时阻尼弱保持汹涌；关闭水龙头后阻尼强，水面 3~4 秒内恢复平静
    const float DAMP = pouring ? 0.9990f : 0.9940f;

    for (int y = 1; y < SIM_N - 1; y++)
        for (int x = 1; x < SIM_N - 1; x++)
            simLap[simIdx(x, y)] = 0.25f * (simH[simIdx(x - 1, y)] + simH[simIdx(x + 1, y)] +
                                            simH[simIdx(x, y - 1)] + simH[simIdx(x, y + 1)]) -
                                   simH[simIdx(x, y)];

    double sum = 0.0;
    for (int y = 1; y < SIM_N - 1; y++) {
        for (int x = 1; x < SIM_N - 1; x++) {
            int i = simIdx(x, y);
            simV[i] = (simV[i] + K * simLap[i] * dt) * DAMP;
            // 波谷不低于缸底（浅水时波膜不能穿到缸底下面）
            simH[i] = glm::clamp(simH[i] + simV[i] * dt, -std::min(0.13f, waterLevel), 0.13f);
            sum += simH[i];
        }
    }

    // 均值守恒：持续注水扰动会让平均水位漂移，拉回 0
    float mean = (float)(sum / ((SIM_N - 2) * (SIM_N - 2)));
    for (int y = 1; y < SIM_N - 1; y++)
        for (int x = 1; x < SIM_N - 1; x++)
            simH[simIdx(x, y)] -= mean;

    // Neumann 边界（复制邻格）：波在玻璃壁上反射
    for (int x = 0; x < SIM_N; x++) {
        simH[simIdx(x, 0)]         = simH[simIdx(x, 1)];
        simH[simIdx(x, SIM_N - 1)] = simH[simIdx(x, SIM_N - 2)];
    }
    for (int y = 0; y < SIM_N; y++) {
        simH[simIdx(0, y)]         = simH[simIdx(1, y)];
        simH[simIdx(SIM_N - 1, y)] = simH[simIdx(SIM_N - 2, y)];
    }
}

// 双线性采样水面高度（世界坐标）
float waterHeightAt(float wx, float wz) {
    float gx = glm::clamp(worldToGridX(wx), 0.0f, (float)(SIM_N - 1) - 0.001f);
    float gz = glm::clamp(worldToGridZ(wz), 0.0f, (float)(SIM_N - 1) - 0.001f);
    int   x0 = (int)gx, z0 = (int)gz;
    float fx = gx - x0, fz = gz - z0;
    float h00 = simH[simIdx(x0, z0)],     h10 = simH[simIdx(x0 + 1, z0)];
    float h01 = simH[simIdx(x0, z0 + 1)], h11 = simH[simIdx(x0 + 1, z0 + 1)];
    return waterLevel + glm::mix(glm::mix(h00, h10, fx), glm::mix(h01, h11, fx), fz);
}

// 水面梯度（dh/dx, dh/dz），决定浮体被波浪推向哪边
glm::vec2 waterGradAt(float wx, float wz) {
    float e = 2.0f * INNER_X / (SIM_N - 1);   // 一个网格的世界宽度
    return glm::vec2(
        (waterHeightAt(wx + e, wz) - waterHeightAt(wx - e, wz)) / (2.0f * e),
        (waterHeightAt(wx, wz + e) - waterHeightAt(wx, wz - e)) / (2.0f * e));
}

// 落水点固定：波纹从落水点向四周同心散开（真实水流落点不会绕圈游走，
// 游走会让凹坑跟着转，看起来像旋涡）
glm::vec2 impactPos(float t) {
    return glm::vec2(IMPACT_X, IMPACT_Z);
}

// ============================ 水花粒子 ============================
struct Splash {
    glm::vec3 pos, vel;
    float     size, life;
};
std::vector<Splash> splashes;

float frand() { return (float)std::rand() / (float)RAND_MAX; }

// 水流砸在水面上，从冲击点向四周喷出随机小水珠
void spawnSplashes(float wx, float wz, float surfaceY, int count) {
    for (int i = 0; i < count && splashes.size() < 400; i++) {
        float ang   = frand() * 6.2831853f;
        float horiz = 0.35f + 0.85f * frand();
        Splash s;
        s.pos  = {wx + (frand() - 0.5f) * 0.06f, surfaceY + 0.03f, wz + (frand() - 0.5f) * 0.06f};
        s.vel  = {std::cos(ang) * horiz, 1.0f + 1.7f * frand(), std::sin(ang) * horiz};
        s.size = 0.007f + 0.012f * frand();
        s.life = 0.0f;
        splashes.push_back(s);
    }
}

void updateSplashes(float dt) {
    for (size_t i = 0; i < splashes.size();) {
        Splash& s = splashes[i];
        s.vel.y -= 7.5f * dt;   // 重力
        s.pos   += s.vel * dt;
        s.life  += dt;
        bool hitWall = std::abs(s.pos.x) > INNER_X || std::abs(s.pos.z) > INNER_Z;
        bool landed  = s.pos.y <= waterHeightAt(s.pos.x, s.pos.z);
        if (landed && !hitWall && pouring)
            disturb(s.pos.x, s.pos.z, 1.2f, -0.10f);   // 水珠落回水面激起小涟漪（关闭水龙头后不再扰动，让水面平稳恢复）
        if (landed || hitWall || s.life > 2.0f) {
            s = splashes.back();
            splashes.pop_back();
        } else {
            i++;
        }
    }
}

// ============================ 漂浮物 ============================
struct Floater {
    glm::vec3 pos, vel;
    glm::vec3 color;
    float     size;      // 立方体半边长 / 球半径
    int       shape;     // 0 立方体  1 球
    glm::vec3 nSmooth;   // 平滑后的水面法线（姿态用）
    float     spin, spinV;
};
std::vector<Floater> floaters;

void initFloaters() {
    floaters.clear();
    // 启动时水缸是空的：浮体躺在缸底（y = 壁厚 + 自身尺寸），注水后自然浮起
    // 位置、颜色、尺寸、形状（0 立方体 / 1 球）
    floaters.push_back({{-0.45f, WALL_T + 0.11f,  0.25f}, glm::vec3(0), {0.72f, 0.52f, 0.32f}, 0.11f,  0, {0,1,0}, 0.0f,  0.25f}); // 木块
    floaters.push_back({{ 0.05f, WALL_T + 0.085f, -0.32f}, glm::vec3(0), {0.80f, 0.28f, 0.24f}, 0.085f, 0, {0,1,0}, 0.8f, -0.35f}); // 红块
    floaters.push_back({{-0.15f, WALL_T + 0.09f,  0.02f}, glm::vec3(0), {0.95f, 0.80f, 0.25f}, 0.09f,  1, {0,1,0}, 0.0f,  0.0f});  // 黄球
    floaters.push_back({{ 0.30f, WALL_T + 0.065f, 0.35f}, glm::vec3(0), {0.30f, 0.70f, 0.65f}, 0.065f, 1, {0,1,0}, 0.0f,  0.0f});  // 青球
    floaters.push_back({{-0.62f, WALL_T + 0.075f, -0.28f}, glm::vec3(0), {0.92f, 0.92f, 0.90f}, 0.075f, 1, {0,1,0}, 0.0f,  0.0f});  // 白球
}

void updateFloaters(float dt) {
    for (auto& f : floaters) {
        float     h = waterHeightAt(f.pos.x, f.pos.z);
        glm::vec2 g = waterGradAt(f.pos.x, f.pos.z);

        // 浮力：弹簧拉向水面（半潜姿态）+ 垂直阻尼
        float target = h + f.size * 0.25f;
        f.vel.y += (target - f.pos.y) * 40.0f * dt;
        f.vel.y *= std::exp(-6.0f * dt);

        // 波面坡度推力：物体顺波往低处滑
        f.vel.x += -g.x * 6.0f * dt;
        f.vel.z += -g.y * 6.0f * dt;

        // 水流冲击点的径向推流（离得越近推得越猛，要胜过凹坑对浮体的吸引）
        if (pouring) {
            glm::vec2 d(f.pos.x - IMPACT_X, f.pos.z - IMPACT_Z);
            float dist2 = glm::dot(d, d) + 0.06f;
            glm::vec2 push = d / dist2 * 0.5f * dt;
            f.vel.x += push.x;
            f.vel.z += push.y;
        }

        // 水平阻尼（水的粘滞）
        float hd = std::exp(-1.4f * dt);
        f.vel.x *= hd;
        f.vel.z *= hd;

        f.pos += f.vel * dt;
        f.spin += f.spinV * dt + (f.vel.x - f.vel.z) * 0.3f * dt;

        // 缸底碰撞：水没涨起来时浮体躺在缸底（球/方块都按自身尺寸触底）
        float floorY = WALL_T + f.size;
        if (f.pos.y < floorY) { f.pos.y = floorY; if (f.vel.y < 0.0f) f.vel.y = 0.0f; }

        // 玻璃壁碰撞：反弹并损失能量
        float bx = INNER_X - f.size, bz = INNER_Z - f.size;
        if (f.pos.x >  bx) { f.pos.x =  bx; f.vel.x *= -0.35f; }
        if (f.pos.x < -bx) { f.pos.x = -bx; f.vel.x *= -0.35f; }
        if (f.pos.z >  bz) { f.pos.z =  bz; f.vel.z *= -0.35f; }
        if (f.pos.z < -bz) { f.pos.z = -bz; f.vel.z *= -0.35f; }

        // 姿态：向水面法线倾斜（平滑过渡，避免抖动）
        glm::vec3 n = glm::normalize(glm::vec3(-g.x, 1.0f, -g.y));
        f.nSmooth   = glm::normalize(glm::mix(f.nSmooth, n, 1.0f - std::exp(-5.0f * dt)));
    }

    // 浮体互相推开（简单的球形排斥）
    for (size_t i = 0; i < floaters.size(); i++) {
        for (size_t j = i + 1; j < floaters.size(); j++) {
            glm::vec2 d(floaters[j].pos.x - floaters[i].pos.x,
                        floaters[j].pos.z - floaters[i].pos.z);
            float minD = (floaters[i].size + floaters[j].size) * 1.1f;
            float len  = glm::length(d);
            if (len < minD && len > 1e-5f) {
                glm::vec2 push = d / len * (minD - len) * 0.5f;
                floaters[i].pos.x -= push.x; floaters[i].pos.z -= push.y;
                floaters[j].pos.x += push.x; floaters[j].pos.z += push.y;
            }
        }
    }
}

// ============================ 网格生成 ============================
// 单位立方体（24 顶点带法线），中心在原点、边长 1
unsigned int createCubeVAO(int& indexCount) {
    const float P = 0.5f;
    float verts[] = {
        // +x                    // -x
        P,-P,-P, 1,0,0,  P, P,-P, 1,0,0,  P, P, P, 1,0,0,  P,-P, P, 1,0,0,
       -P,-P,-P,-1,0,0, -P,-P, P,-1,0,0, -P, P, P,-1,0,0, -P, P,-P,-1,0,0,
        // +y                    // -y
       -P, P,-P, 0,1,0, -P, P, P, 0,1,0,  P, P, P, 0,1,0,  P, P,-P, 0,1,0,
       -P,-P,-P, 0,-1,0, P,-P,-P, 0,-1,0, P,-P, P, 0,-1,0, -P,-P, P, 0,-1,0,
        // +z                    // -z
       -P,-P, P, 0,0,1,  P,-P, P, 0,0,1,  P, P, P, 0,0,1, -P, P, P, 0,0,1,
       -P,-P,-P, 0,0,-1, -P, P,-P, 0,0,-1, P, P,-P, 0,0,-1, P,-P,-P, 0,0,-1,
    };
    unsigned int indices[36];
    for (int f = 0; f < 6; f++) {
        int b = f * 4, o = f * 6;
        indices[o] = b; indices[o+1] = b+1; indices[o+2] = b+2;
        indices[o+3] = b; indices[o+4] = b+2; indices[o+5] = b+3;
    }
    unsigned int vao, vbo, ebo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    indexCount = 36;
    return vao;
}

// UV 球（单位半径）
unsigned int createSphereVAO(int& indexCount) {
    const int STACKS = 18, SECTORS = 28;
    std::vector<float> verts;
    std::vector<unsigned int> indices;
    for (int i = 0; i <= STACKS; i++) {
        float phi = 3.14159265f * i / STACKS;
        for (int j = 0; j <= SECTORS; j++) {
            float theta = 2.0f * 3.14159265f * j / SECTORS;
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            verts.insert(verts.end(), {x, y, z, x, y, z});
        }
    }
    for (int i = 0; i < STACKS; i++) {
        for (int j = 0; j < SECTORS; j++) {
            unsigned int a = i * (SECTORS + 1) + j, b = a + SECTORS + 1;
            indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    unsigned int vao, vbo, ebo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    indexCount = (int)indices.size();
    return vao;
}

// 水流管：上粗下细的圆台侧面（真实水流下落加速会变细），无盖
unsigned int createStreamVAO(int& indexCount) {
    const int   SIDES = 20;
    const float R_TOP = 0.5f, R_BOT = 0.33f;
    std::vector<float> verts;
    std::vector<unsigned int> indices;
    for (int j = 0; j <= 1; j++) {                    // 0 顶环  1 底环
        float y = 0.5f - (float)j;
        float r = j ? R_BOT : R_TOP;
        for (int i = 0; i <= SIDES; i++) {
            float a = 2.0f * 3.14159265f * i / SIDES;
            float c = std::cos(a), s = std::sin(a);
            // 法线近似径向，略朝上补偿锥度
            verts.insert(verts.end(), {c * r, y, s * r, c, 0.17f, s});
        }
    }
    for (int i = 0; i < SIDES; i++) {
        unsigned int a = i, b = i + SIDES + 1;
        indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
    unsigned int vao, vbo, ebo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    indexCount = (int)indices.size();
    return vao;
}

// 水面网格：(N+1)² 个顶点，只存 [0,1]² 网格坐标，位移在顶点着色器完成
unsigned int createWaterVAO(int& indexCount) {
    const int N = SIM_N;
    std::vector<float> verts;
    std::vector<unsigned int> indices;
    for (int j = 0; j <= N; j++)
        for (int i = 0; i <= N; i++)
            verts.insert(verts.end(), {(float)i / N, (float)j / N});
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            unsigned int a = j * (N + 1) + i, b = a + N + 1;
            indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    unsigned int vao, vbo, ebo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    indexCount = (int)indices.size();
    return vao;
}

// ============================ 屏幕外渲染目标 ============================
// 场景先渲染到 FBO 再 blit 到屏幕：隐藏窗口下也能可靠截图
unsigned int sceneFBO = 0, sceneTex = 0, sceneRBO = 0;

void createSceneTarget(int w, int h) {
    glGenTextures(1, &sceneTex);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenRenderbuffers(1, &sceneRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    glGenFramebuffers(1, &sceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneRBO);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER 不完整" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// 阴影贴图（光源视角正交投影，2048² 深度）
const int  SHADOW_SIZE = 2048;
unsigned int shadowFBO = 0, shadowTex = 0;

void createShadowTarget() {
    glGenTextures(1, &shadowTex);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = {1, 1, 1, 1};   // 阴影贴图外区域视为被照亮
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glGenFramebuffers(1, &shadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::阴影贴图 FBO 不完整" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// 后处理渲染目标（色调映射 / gamma / 暗角）
unsigned int postFBO = 0, postTex = 0;

void createPostTarget(int w, int h) {
    glGenTextures(1, &postTex);
    glBindTexture(GL_TEXTURE_2D, postTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &postFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, postFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::后处理 FBO 不完整" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// 全屏三角形（后处理 pass 用）
unsigned int createFullscreenVAO() {
    float verts[] = {-1, -1,  3, -1,  -1, 3};
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return vao;
}

// ============================ 截图（BMP） ============================
void saveScreenshot(const char* path, int w, int h, unsigned int fbo) {
    std::vector<unsigned char> rgb((size_t)w * h * 3);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // RGB → BGR（BMP 要求），行序均为底部在前，无需翻转
    for (size_t i = 0; i < rgb.size(); i += 3) std::swap(rgb[i], rgb[i + 2]);

    int rowSize  = (w * 3 + 3) & ~3;
    int fileSize = 54 + rowSize * h;
    unsigned char header[54];
    std::memset(header, 0, sizeof(header));
    header[0] = 'B'; header[1] = 'M';
    int v;
    v = fileSize;     std::memcpy(header + 2,  &v, 4);
    v = 54;           std::memcpy(header + 10, &v, 4);
    v = 40;           std::memcpy(header + 14, &v, 4);
    v = w;            std::memcpy(header + 18, &v, 4);
    v = h;            std::memcpy(header + 22, &v, 4);
    short s;
    s = 1;            std::memcpy(header + 26, &s, 2);
    s = 24;           std::memcpy(header + 28, &s, 2);
    v = rowSize * h;  std::memcpy(header + 34, &v, 4);

    std::ofstream file(path, std::ios::binary);
    file.write((char*)header, 54);
    std::vector<unsigned char> pad(rowSize - w * 3, 0);
    for (int y = 0; y < h; y++) {
        file.write((char*)&rgb[(size_t)y * w * 3], w * 3);
        file.write((char*)pad.data(), rowSize - w * 3);
    }
    std::cout << "截图已保存: " << path << std::endl;
}

// ============================ 初始化窗口 ============================
GLFWwindow* initWindow(bool visible = true) {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(fbWidth, fbHeight, "05-流体水", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        exit(-1);
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        exit(-1);
    }

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);
    return window;
}

// ============================ 场景着色器 uniform ============================
struct SceneUniforms {
    GLint model, view, proj, normalMat, camPos, color, alpha, mode, time;
    GLint lightSpace, shadowMap, shadowBias;
};

// 漂浮物的模型矩阵（主渲染与阴影贴图 pass 共用，保证两遍完全一致）
glm::mat4 floaterModel(const Floater& f) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), f.pos);
    if (f.shape == 0) {
        // 立方体：姿态随水面法线倾斜 + 缓慢自转
        glm::vec3 up = f.nSmooth;
        glm::vec3 t  = glm::normalize(glm::cross(glm::vec3(0, 0, 1), up));
        glm::vec3 b  = glm::cross(up, t);
        glm::mat4 rot(1.0f);
        rot[0] = glm::vec4(t, 0.0f); rot[1] = glm::vec4(up, 0.0f); rot[2] = glm::vec4(b, 0.0f);
        model = model * rot * glm::rotate(glm::mat4(1.0f), f.spin, glm::vec3(0, 1, 0));
        model = glm::scale(model, glm::vec3(f.size * 2.0f));
    } else {
        model = glm::scale(model, glm::vec3(f.size));
    }
    return model;
}

// 绘制一个网格（设置模型矩阵、法线矩阵、材质参数）
void drawMesh(unsigned int vao, int indexCount, const glm::mat4& model,
              const SceneUniforms& u, const glm::vec3& color, float alpha, int mode) {
    glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(model)));
    glUniformMatrix4fv(u.model, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix3fv(u.normalMat, 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3f(u.color, color.r, color.g, color.b);
    glUniform1f(u.alpha, alpha);
    glUniform1i(u.mode, mode);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
}

// 玻璃缸的 5 块玻璃板（4 壁 + 底），复用立方体网格缩放
std::vector<glm::mat4> tankSlabModels() {
    std::vector<glm::mat4> m;
    auto slab = [&](glm::vec3 center, glm::vec3 size) {
        m.push_back(glm::scale(glm::translate(glm::mat4(1.0f), center), size));
    };
    slab({0.0f, WALL_T * 0.5f, 0.0f},        {TANK_X * 2, WALL_T, TANK_Z * 2});                 // 底
    slab({ TANK_X - WALL_T * 0.5f, TANK_H * 0.5f, 0.0f}, {WALL_T, TANK_H, TANK_Z * 2});          // +x 壁
    slab({-TANK_X + WALL_T * 0.5f, TANK_H * 0.5f, 0.0f}, {WALL_T, TANK_H, TANK_Z * 2});          // -x 壁
    slab({0.0f, TANK_H * 0.5f,  TANK_Z - WALL_T * 0.5f}, {2 * (TANK_X - WALL_T), TANK_H, WALL_T}); // +z 壁
    slab({0.0f, TANK_H * 0.5f, -TANK_Z + WALL_T * 0.5f}, {2 * (TANK_X - WALL_T), TANK_H, WALL_T}); // -z 壁
    return m;
}

// ============================ 主函数 ============================
int main(int argc, char** argv) {
    // 命令行参数：--snapshot N → 隐藏窗口模拟 N 帧后自动截图退出
    int  snapshotFrames = 0;
    bool snapshotMode   = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) {
            snapshotMode   = true;
            snapshotFrames = std::atoi(argv[i + 1]);
        }
    }

    GLFWwindow* window = initWindow(!snapshotMode);

    // 快照验证模式：自动打开水龙头注水（窗口模式启动时水缸是空的，由用户按空格开闸）
    if (snapshotMode) pouring = true;

    // 着色器程序
    unsigned int sceneProgram  = createProgram("shaders/scene.vert", "shaders/scene.frag");
    unsigned int waterProgram  = createProgram("shaders/water.vert", "shaders/water.frag");
    unsigned int shadowProgram = createProgram("shaders/shadow.vert", "shaders/shadow.frag");
    unsigned int postProgram   = createProgram("shaders/post.vert", "shaders/post.frag");

    SceneUniforms su;
    su.model      = glGetUniformLocation(sceneProgram, "uModel");
    su.view       = glGetUniformLocation(sceneProgram, "uView");
    su.proj       = glGetUniformLocation(sceneProgram, "uProj");
    su.normalMat  = glGetUniformLocation(sceneProgram, "uNormalMat");
    su.camPos     = glGetUniformLocation(sceneProgram, "uCamPos");
    su.color      = glGetUniformLocation(sceneProgram, "uColor");
    su.alpha      = glGetUniformLocation(sceneProgram, "uAlpha");
    su.mode       = glGetUniformLocation(sceneProgram, "uMode");
    su.time       = glGetUniformLocation(sceneProgram, "uTime");
    su.lightSpace = glGetUniformLocation(sceneProgram, "uLightSpace");
    su.shadowMap  = glGetUniformLocation(sceneProgram, "uShadowMap");
    su.shadowBias = glGetUniformLocation(sceneProgram, "uShadowBias");

    GLint wHeightLoc  = glGetUniformLocation(waterProgram, "uHeightTex");
    GLint wRectLoc    = glGetUniformLocation(waterProgram, "uRect");
    GLint wBaseYLoc   = glGetUniformLocation(waterProgram, "uBaseY");
    GLint wViewLoc    = glGetUniformLocation(waterProgram, "uView");
    GLint wProjLoc    = glGetUniformLocation(waterProgram, "uProj");
    GLint wCamLoc     = glGetUniformLocation(waterProgram, "uCamPos");
    GLint wLightLoc   = glGetUniformLocation(waterProgram, "uLightSpace");
    GLint wShadowLoc  = glGetUniformLocation(waterProgram, "uShadowMap");
    GLint wBiasLoc    = glGetUniformLocation(waterProgram, "uShadowBias");
    GLint wShadowPass = glGetUniformLocation(waterProgram, "uShadowPass");
    // 水面光线追踪参数（反射求交 / 追踪软阴影）
    GLint wFloaterPosLoc   = glGetUniformLocation(waterProgram, "uFloaterPos");
    GLint wFloaterParamLoc = glGetUniformLocation(waterProgram, "uFloaterParam");
    GLint wLightDirWLoc    = glGetUniformLocation(waterProgram, "uLightDirW");
    GLint wLightColWLoc    = glGetUniformLocation(waterProgram, "uLightColW");
    GLint wInnerXLoc       = glGetUniformLocation(waterProgram, "uInnerX");
    GLint wInnerZLoc       = glGetUniformLocation(waterProgram, "uInnerZ");

    GLint sdModelLoc = glGetUniformLocation(shadowProgram, "uModel");
    GLint sdLightLoc = glGetUniformLocation(shadowProgram, "uLightSpace");
    GLint postTexLoc = glGetUniformLocation(postProgram, "uSceneTex");

    // 网格
    int cubeIdx, sphereIdx, waterIdx, streamIdx;
    unsigned int cubeVAO   = createCubeVAO(cubeIdx);
    unsigned int sphereVAO = createSphereVAO(sphereIdx);
    unsigned int waterVAO  = createWaterVAO(waterIdx);
    unsigned int streamVAO = createStreamVAO(streamIdx);

    // 高度场纹理（每帧上传模拟结果）
    unsigned int heightTex;
    glGenTextures(1, &heightTex);
    glBindTexture(GL_TEXTURE_2D, heightTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, SIM_N, SIM_N, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    createSceneTarget(fbWidth, fbHeight);
    createShadowTarget();
    createPostTarget(fbWidth, fbHeight);
    unsigned int fsVAO = createFullscreenVAO();
    initFloaters();

    glEnable(GL_DEPTH_TEST);

    auto tankSlabs = tankSlabModels();

    // 光源与阴影贴图矩阵（与着色器里 LIGHT_DIR 一致：LIGHT_DIR 指向光源，
    // 所以光源位置 = 目标点 + LIGHT_DIR × 距离，位于斜上方天空）
    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.35f, 0.9f, 0.45f));
    const glm::vec3 lightCol = glm::vec3(1.0f, 0.90f, 0.76f);   // 暖色阳光，替代刺眼的纯白
    glm::vec3 lightTarget(0.2f, 0.65f, 0.0f);
    glm::vec3 lightPos   = lightTarget + lightDir * 6.0f;
    glm::mat4 lightView  = glm::lookAt(lightPos, lightTarget, glm::vec3(0, 1, 0));
    glm::mat4 lightProj  = glm::ortho(-3.2f, 3.2f, -3.2f, 3.2f, 0.5f, 12.0f);
    glm::mat4 lightSpace = lightProj * lightView;

    glm::mat4 groundModel = glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, -0.01f, 0.0f}),
                                       {14.0f, 0.02f, 14.0f});

    // 模拟/FPS 计时
    double lastFrame = glfwGetTime();
    double simAccum  = 0.0;
    float  simTime   = 0.0f;
    const float SIM_DT = 1.0f / 120.0f;   // 固定物理步长
    int    frames = 0, totalFrames = 0;
    double lastFps = lastFrame;

    while (!glfwWindowShouldClose(window)) {
        // ---- 输入 ----
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GL_TRUE);

        static bool spaceHeld = false, pHeld = false, rHeld = false;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !spaceHeld) pouring = !pouring;
        spaceHeld = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
        if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS && !pHeld)
            saveScreenshot("screenshot.bmp", fbWidth, fbHeight, postFBO);
        pHeld = (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS);
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !rHeld) {
            std::fill(simH.begin(), simH.end(), 0.0f);
            std::fill(simV.begin(), simV.end(), 0.0f);
            splashes.clear();
            initFloaters();
            waterLevel = 0.0f;   // 重置为启动状态：空缸、水龙头关闭
            pouring    = false;
            camTarget = glm::vec3(0.2f, 0.65f, 0.0f);
            radius = 4.2f; yaw = 0.65f; pitch = 0.32f;
        }
        rHeld = (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS);

        // 由球坐标反推相机位置
        camPos = camTarget + radius * glm::vec3(
            std::cos(pitch) * std::sin(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::cos(yaw));

        // ---- 物理模拟（固定步长，快照模式用固定帧时长保证可复现）----
        double now = glfwGetTime();
        double dt  = snapshotMode ? (1.0 / 60.0) : std::min(now - lastFrame, 0.05);
        lastFrame  = now;
        simAccum  += dt;
        // 刚关闭水龙头：衰减速度场，让回弹温和，避免环状驻波看起来像漩涡
        static bool wasPoured = true;
        if (!pouring && wasPoured) {
            for (float& v : simV) v *= 0.5f;
        }
        wasPoured = pouring;

        while (simAccum >= SIM_DT) {
            simAccum -= SIM_DT;
            simTime  += SIM_DT;
            if (pouring) {
                // 注水：水位缓慢上涨（满后不再涨），同时水流砸击水面
                if (waterLevel < MAX_WATER_Y)
                    waterLevel = std::min(waterLevel + FILL_RATE * SIM_DT, MAX_WATER_Y);
                // 水流冲击：持续压低水面，强度带轻微脉动 + 冲击点游走，波浪更汹涌
                float pulse = 1.0f + 0.35f * std::sin(simTime * 9.0f) + 0.25f * std::sin(simTime * 23.7f);
                glm::vec2 ip = impactPos(simTime);
                disturb(ip.x, ip.y, 3.0f, -0.85f * pulse);
                // 每步喷 2 颗水珠（120Hz ≈ 240 颗/秒）
                spawnSplashes(ip.x, ip.y, waterHeightAt(ip.x, ip.y), 2);
            }
            stepWave(SIM_DT);
            updateFloaters(SIM_DT);
            updateSplashes(SIM_DT);
        }

        // 上传高度场纹理
        glBindTexture(GL_TEXTURE_2D, heightTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SIM_N, SIM_N, GL_RED, GL_FLOAT, simH.data());

        // 窗口尺寸变化 → 重建渲染目标
        if (fbResized) {
            glDeleteTextures(1, &sceneTex);
            glDeleteRenderbuffers(1, &sceneRBO);
            glDeleteFramebuffers(1, &sceneFBO);
            glDeleteTextures(1, &postTex);
            glDeleteFramebuffers(1, &postFBO);
            createSceneTarget(fbWidth, fbHeight);
            createPostTarget(fbWidth, fbHeight);
            fbResized = false;
        }

        // ---- 阴影贴图 pass：地面/水管/浮体/水面从光源视角写深度 ----
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);

        glUseProgram(shadowProgram);
        glUniformMatrix4fv(sdLightLoc, 1, GL_FALSE, glm::value_ptr(lightSpace));
        auto shadowDraw = [&](unsigned int vao, int idx, const glm::mat4& model) {
            glUniformMatrix4fv(sdModelLoc, 1, GL_FALSE, glm::value_ptr(model));
            glBindVertexArray(vao);
            glDrawElements(GL_TRIANGLES, idx, GL_UNSIGNED_INT, 0);
        };

        // 地面：光源在上方，只画顶面
        glCullFace(GL_BACK);
        shadowDraw(cubeVAO, cubeIdx, groundModel);

        // 其余物体画背面深度（彼得潘算法，缓解阴影痤疮）；浮体画正面保证影子完整
        glCullFace(GL_FRONT);
        shadowDraw(cubeVAO, cubeIdx,
                   glm::scale(glm::translate(glm::mat4(1.0f), {(IMPACT_X + 1.9f) * 0.5f, 1.92f, 0.0f}),
                              {1.9f - IMPACT_X, 0.085f, 0.085f}));
        shadowDraw(cubeVAO, cubeIdx,
                   glm::scale(glm::translate(glm::mat4(1.0f), {IMPACT_X, 1.80f, 0.0f}), {0.10f, 0.17f, 0.10f}));
        shadowDraw(cubeVAO, cubeIdx,
                   glm::scale(glm::translate(glm::mat4(1.0f), {1.9f, 0.96f, 0.0f}), {0.07f, 1.92f, 0.07f}));
        glCullFace(GL_BACK);
        for (const auto& f : floaters)
            shadowDraw(f.shape == 0 ? cubeVAO : sphereVAO, f.shape == 0 ? cubeIdx : sphereIdx,
                       floaterModel(f));

        // 水面：与主 pass 使用同一高度场位移，浮体影子才能准确落在波纹上
        if (waterLevel > 0.005f) {
            glUseProgram(waterProgram);
            glUniform1i(wShadowPass, 1);
            glUniformMatrix4fv(wViewLoc, 1, GL_FALSE, glm::value_ptr(lightView));
            glUniformMatrix4fv(wProjLoc, 1, GL_FALSE, glm::value_ptr(lightProj));
            glUniform4f(wRectLoc, -INNER_X, -INNER_Z, INNER_X, INNER_Z);
            glUniform1f(wBaseYLoc, waterLevel);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, heightTex);
            glUniform1i(wHeightLoc, 0);
            glBindVertexArray(waterVAO);
            glDrawElements(GL_TRIANGLES, waterIdx, GL_UNSIGNED_INT, 0);
            glUniform1i(wShadowPass, 0);
        }
        glDisable(GL_CULL_FACE);

        // ---- 渲染到离屏 FBO ----
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        glViewport(0, 0, fbWidth, fbHeight);
        glClearColor(0.84f, 0.85f, 0.86f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(fov), (float)fbWidth / fbHeight, 0.1f, 100.0f);

        // 阴影贴图绑定到纹理单元 1，供场景/水面着色器采样
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowTex);

        glUseProgram(sceneProgram);
        glUniformMatrix4fv(su.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(su.proj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3f(su.camPos, camPos.x, camPos.y, camPos.z);
        glUniform1f(su.time, simTime);
        glUniformMatrix4fv(su.lightSpace, 1, GL_FALSE, glm::value_ptr(lightSpace));
        glUniform1i(su.shadowMap, 1);
        glUniform1f(su.shadowBias, 0.002f);

        // 1. 不透明物体：地面、水管、漂浮物
        drawMesh(cubeVAO, cubeIdx, groundModel, su, glm::vec3(0.75f), 1.0f, 1);   // 棋盘地面

        glm::vec3 pipeColor(0.52f, 0.55f, 0.58f);
        drawMesh(cubeVAO, cubeIdx,
                 glm::scale(glm::translate(glm::mat4(1.0f), {(IMPACT_X + 1.9f) * 0.5f, 1.92f, 0.0f}),
                            {1.9f - IMPACT_X, 0.085f, 0.085f}),
                 su, pipeColor, 1.0f, 0);          // 水管横臂
        drawMesh(cubeVAO, cubeIdx,
                 glm::scale(glm::translate(glm::mat4(1.0f), {IMPACT_X, 1.80f, 0.0f}), {0.10f, 0.17f, 0.10f}),
                 su, pipeColor, 1.0f, 0);          // 出水嘴
        drawMesh(cubeVAO, cubeIdx,
                 glm::scale(glm::translate(glm::mat4(1.0f), {1.9f, 0.96f, 0.0f}), {0.07f, 1.92f, 0.07f}),
                 su, pipeColor, 1.0f, 0);          // 立柱

        for (const auto& f : floaters) {
            glm::mat4 model = floaterModel(f);
            drawMesh(f.shape == 0 ? cubeVAO : sphereVAO, f.shape == 0 ? cubeIdx : sphereIdx,
                     model, su, f.color, 1.0f, 0);
        }

        // 2. 透明物体（关深度写入，手动排序：玻璃背面 → 水流柱 → 水面 → 玻璃正面）
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        glm::vec3 glassTint(0.70f, 0.82f, 0.83f);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);   // 只画背面（缸的远侧内壁）
        for (const auto& m : tankSlabs) drawMesh(cubeVAO, cubeIdx, m, su, glassTint, 0.08f, 2);
        glDisable(GL_CULL_FACE);

        // 水体：水面以下的半透明浅色方块（清水：色浅透明度高，水下物体清晰可见）
        // 高度随水位动态变化，空缸时（水位低于缸底 + 5cm）不画
        float bodyTop = waterLevel - 0.05f;
        if (bodyTop > WALL_T) {
            float bodyH = bodyTop - WALL_T;
            drawMesh(cubeVAO, cubeIdx,
                     glm::scale(glm::translate(glm::mat4(1.0f),
                                {0.0f, WALL_T + bodyH * 0.5f, 0.0f}),
                                {2.0f * INNER_X, bodyH, 2.0f * INNER_Z}),
                     su, glm::vec3(0.28f, 0.50f, 0.56f), 0.15f, 0);
        }

        if (pouring) {
            // 水流柱：锥形圆管从出水嘴落到冲击点，长度随水面波动
            glm::vec2 ip   = impactPos(simTime);
            float     hitY = waterHeightAt(ip.x, ip.y);
            float     wob  = 0.008f * std::sin(simTime * 21.0f);
            drawMesh(streamVAO, streamIdx,
                     glm::scale(glm::translate(glm::mat4(1.0f),
                                {ip.x + wob, (NOZZLE_Y + hitY) * 0.5f, ip.y}),
                                {0.16f, NOZZLE_Y - hitY, 0.16f}),
                     su, glm::vec3(0.80f, 0.88f, 0.93f), 0.45f, 3);
        }

        // 水面（空缸时不画）
        if (waterLevel > 0.005f) {
            glUseProgram(waterProgram);
            glUniformMatrix4fv(wViewLoc, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(wProjLoc, 1, GL_FALSE, glm::value_ptr(proj));
            glUniform3f(wCamLoc, camPos.x, camPos.y, camPos.z);
            glUniformMatrix4fv(wLightLoc, 1, GL_FALSE, glm::value_ptr(lightSpace));
            glUniform1i(wShadowLoc, 1);
            glUniform1f(wBiasLoc, 0.0015f);
            glUniform4f(wRectLoc, -INNER_X, -INNER_Z, INNER_X, INNER_Z);
            glUniform1f(wBaseYLoc, waterLevel);
            // 光线追踪参数：浮体位置/颜色/尺寸 + 暖色光源 + 缸内尺寸
            for (int i = 0; i < 5 && i < (int)floaters.size(); i++) {
                glUniform4f(wFloaterPosLoc + i, floaters[i].pos.x, floaters[i].pos.y, floaters[i].pos.z,
                            (float)floaters[i].shape);
                glUniform4f(wFloaterParamLoc + i, floaters[i].color.r, floaters[i].color.g, floaters[i].color.b,
                            floaters[i].size);
            }
            glUniform3f(wLightDirWLoc, lightDir.x, lightDir.y, lightDir.z);
            glUniform3f(wLightColWLoc, lightCol.r, lightCol.g, lightCol.b);
            glUniform1f(wInnerXLoc, INNER_X);
            glUniform1f(wInnerZLoc, INNER_Z);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, heightTex);
            glUniform1i(wHeightLoc, 0);
            glBindVertexArray(waterVAO);
            glDrawElements(GL_TRIANGLES, waterIdx, GL_UNSIGNED_INT, 0);
        }

        // 玻璃正面（缸的近侧外壁）前先画水花：沿运动方向拉伸的水珠，随生命周期变淡
        glUseProgram(sceneProgram);
        for (const auto& s : splashes) {
            float spd = glm::length(s.vel);
            glm::vec3 up = (spd > 1e-4f) ? s.vel / spd : glm::vec3(0, 1, 0);
            glm::vec3 t  = glm::normalize(glm::cross(std::abs(up.y) > 0.99f ? glm::vec3(1, 0, 0)
                                                                            : glm::vec3(0, 1, 0), up));
            glm::vec3 b  = glm::cross(up, t);
            glm::mat4 m(1.0f);
            m[0] = glm::vec4(t, 0.0f); m[1] = glm::vec4(up, 0.0f);
            m[2] = glm::vec4(b, 0.0f); m[3] = glm::vec4(s.pos, 1.0f);
            float stretch = 1.0f + 0.4f * spd;   // 速度越快拉得越长
            m = glm::scale(m, glm::vec3(s.size * 0.65f, s.size * stretch, s.size * 0.65f));
            float alpha = glm::clamp(0.9f - 0.5f * s.life, 0.25f, 0.9f);
            drawMesh(sphereVAO, sphereIdx, m, su, glm::vec3(0.90f, 0.95f, 1.0f), alpha, 4);
        }

        // 玻璃正面（缸的近侧外壁）
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        for (const auto& m : tankSlabs) drawMesh(cubeVAO, cubeIdx, m, su, glassTint, 0.08f, 2);
        glDisable(GL_CULL_FACE);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        // ---- 后处理：ACES 色调映射 + gamma + 暗角 ----
        glBindFramebuffer(GL_FRAMEBUFFER, postFBO);
        glViewport(0, 0, fbWidth, fbHeight);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(postProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneTex);
        glUniform1i(postTexLoc, 0);
        glBindVertexArray(fsVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ---- blit 后处理结果到默认帧缓冲 ----
        glBindFramebuffer(GL_READ_FRAMEBUFFER, postFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, fbWidth, fbHeight, 0, 0, fbWidth, fbHeight,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 标题栏 FPS
        frames++; totalFrames++;
        now = glfwGetTime();
        if (now - lastFps >= 0.5) {
            char title[128];
            snprintf(title, sizeof(title), "05-流体水 | 注水:%s | 水位:%.2fm | FPS:%.0f",
                     pouring ? "开" : "关", waterLevel, frames / (now - lastFps));
            glfwSetWindowTitle(window, title);
            frames  = 0;
            lastFps = now;
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

        // 快照模式：模拟到目标帧数后保存并退出
        if (snapshotMode && totalFrames >= snapshotFrames) {
            std::cout << "snapshot: frames=" << totalFrames << " waterLevel=" << waterLevel << std::endl;
            saveScreenshot("screenshot.bmp", fbWidth, fbHeight, postFBO);
            break;
        }
    }

    // 清理
    glDeleteProgram(sceneProgram);
    glDeleteProgram(waterProgram);
    glDeleteProgram(shadowProgram);
    glDeleteProgram(postProgram);
    glDeleteTextures(1, &heightTex);
    glDeleteTextures(1, &sceneTex);
    glDeleteRenderbuffers(1, &sceneRBO);
    glDeleteFramebuffers(1, &sceneFBO);
    glDeleteTextures(1, &shadowTex);
    glDeleteFramebuffers(1, &shadowFBO);
    glDeleteTextures(1, &postTex);
    glDeleteFramebuffers(1, &postFBO);
    glDeleteVertexArrays(1, &fsVAO);
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteVertexArrays(1, &waterVAO);
    glDeleteVertexArrays(1, &streamVAO);
    glfwTerminate();
    return 0;
}

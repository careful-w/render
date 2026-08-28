#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

/**
 * 04 · 实时光线追踪（渐进式路径追踪）
 *
 * 原理：
 *   光线追踪完全在片段着色器中完成——整个窗口只是一个全屏三角形，
 *   每个像素对应一条从相机发出的光线，在 GPU 上逐像素求交、散射、多次反弹。
 *
 *   为了"实时"：每帧只给每个像素投 1 条新采样光线，用浮点纹理把历史结果
 *   累积起来（glBlendFunc(GL_ONE, GL_ONE)），显示时除以帧数取平均。
 *   画面从噪点迅速收敛成干净的光线追踪图像；移动相机时清空重新累积。
 *
 * 操作：
 *   鼠标左键拖拽  绕目标点旋转
 *   滚轮          推近/拉远
 *   W/A/S/D       平移目标点
 *   +/-           调整光线反弹次数（1~16）
 *   [ / ]         调整每帧采样数（1~8，越大噪点越少但越慢）
 *   R             重置视角
 *   P             保存当前画面截图（screenshot.bmp）
 *   ESC           退出
 *
 * 命令行参数：
 *   OpenGL.exe --snapshot 300   隐藏窗口渲染 300 帧后自动截图退出（用于验证）
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

// ============================ 相机状态 ============================
// 默认 RTIOW（Ray Tracing in One Weekend）经典机位
glm::vec3 camTarget(0.0f, 0.0f, 0.0f);
glm::vec3 camPos(13.0f, 2.0f, 3.0f);
float radius = glm::length(camPos - camTarget);
float yaw    = std::atan2(camPos.x - camTarget.x, camPos.z - camTarget.z);
float pitch  = std::asin((camPos.y - camTarget.y) / radius);

float fov       = 20.0f;   // 垂直视场角（度）
int   maxBounce = 8;       // 最大反弹次数
int   spp       = 2;       // 每帧每像素采样数（1~8，越大噪点越少但越慢）

bool         resetAccum = true; // 相机变化时置 true，清空累积重新收敛
unsigned int frameCount = 0;    // 当前已累积的采样帧数

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
    pitch  = glm::clamp(pitch, -1.5f, 1.5f); // 防止翻转
    resetAccum = true;
}

void scroll_callback(GLFWwindow* window, double xOffset, double yOffset) {
    radius *= (float)std::pow(0.9, yOffset);
    radius  = glm::clamp(radius, 2.0f, 60.0f);
    resetAccum = true;
}

// ============================ 输入处理 ============================
void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);

    // 相机基向量（与着色器中的计算保持一致）
    glm::vec3 forward = glm::normalize(camTarget - camPos);
    glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    float panSpeed    = 0.05f;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { camTarget += forward * panSpeed; resetAccum = true; }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { camTarget -= forward * panSpeed; resetAccum = true; }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { camTarget -= right   * panSpeed; resetAccum = true; }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { camTarget += right   * panSpeed; resetAccum = true; }

    // 调整反弹次数
    if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS) {
        maxBounce = std::min(maxBounce + 1, 16); resetAccum = true;
    }
    if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS) {
        maxBounce = std::max(maxBounce - 1, 1); resetAccum = true;
    }

    // 调整每帧采样数（[ / ] 键）：抗噪主力
    if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS) {
        spp = std::max(spp - 1, 1);
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS) {
        spp = std::min(spp + 1, 8);
    }

    // 重置视角
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
        camTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        camPos    = glm::vec3(13.0f, 2.0f, 3.0f);
        radius    = glm::length(camPos - camTarget);
        yaw       = std::atan2(camPos.x - camTarget.x, camPos.z - camTarget.z);
        pitch     = std::asin((camPos.y - camTarget.y) / radius);
        fov       = 20.0f;
        resetAccum = true;
    }

    // 由球坐标反推相机位置（拖拽/滚轮后生效）
    camPos = camTarget + radius * glm::vec3(
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::cos(yaw));
}

// 累积帧缓冲（全局，供截图函数使用）
unsigned int accumFBO = 0, accumTex = 0;

// ============================ 初始化窗口 ============================
GLFWwindow* initWindow(bool visible = true) {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(fbWidth, fbHeight, "04-实时光线追踪", NULL, NULL);
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

// ============================ 截图（BMP） ============================
// 从累积纹理读取 HDR 采样和，平均 + Gamma 校正后保存为 24 位 BMP
void saveScreenshot(const char* path, int w, int h) {
    std::vector<float> pixels((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::vector<unsigned char> bgr((size_t)w * h * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int src = (y * w + x) * 4;
            int dst = (y * w + x) * 3;   // BMP 与 glReadPixels 都是底部行在前，无需翻转
            for (int c = 0; c < 3; c++) {
                float v = pixels[src + c] / (float)frameCount;
                v *= 0.9f;  // 曝光校准（与显示着色器一致）
                v = (v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f); // ACES 色调映射
                v = std::pow(v, 1.0f / 2.2f);  // Gamma
                bgr[dst + (2 - c)] = (unsigned char)(glm::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); // RGBA → BGR
            }
        }
    }

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
        file.write((char*)&bgr[(size_t)y * w * 3], w * 3);
        file.write((char*)pad.data(), rowSize - w * 3);
    }
    std::cout << "截图已保存: " << path << std::endl;
}

// ============================ 累积帧缓冲 ============================
// 32 位浮点纹理存储 HDR 采样总和，配合 GL_ONE/GL_ONE 混合逐帧累加
void createAccumTarget(unsigned int &fbo, unsigned int &tex, int w, int h) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER 不完整" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================ 主函数 ============================
int main(int argc, char** argv) {
    // 命令行参数：--snapshot N → 隐藏窗口渲染 N 帧后自动截图退出
    int  snapshotFrames = 0;
    bool snapshotMode   = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) {
            snapshotMode  = true;
            snapshotFrames = std::atoi(argv[i + 1]);
        }
    }

    GLFWwindow* window = initWindow(!snapshotMode);

    // 空 VAO：全屏三角形顶点由 gl_VertexID 生成，不需要任何顶点数据
    unsigned int emptyVAO;
    glGenVertexArrays(1, &emptyVAO);
    glBindVertexArray(emptyVAO);

    // 着色器程序
    unsigned int rayProgram     = createProgram("shaders/fullscreen.vert", "shaders/raytracer.frag");
    unsigned int displayProgram = createProgram("shaders/fullscreen.vert", "shaders/composite.frag");

    // uniform 位置
    GLint resLoc       = glGetUniformLocation(rayProgram, "uResolution");
    GLint camPosLoc    = glGetUniformLocation(rayProgram, "uCamPos");
    GLint camTargetLoc = glGetUniformLocation(rayProgram, "uCamTarget");
    GLint fovLoc       = glGetUniformLocation(rayProgram, "uFov");
    GLint seedLoc      = glGetUniformLocation(rayProgram, "uSeed");
    GLint bounceLoc    = glGetUniformLocation(rayProgram, "uMaxBounce");
    GLint sppLoc       = glGetUniformLocation(rayProgram, "uSpp");

    GLint frameCountLoc = glGetUniformLocation(displayProgram, "uFrameCount");
    GLint accumTexLoc   = glGetUniformLocation(displayProgram, "uAccumTex");
    GLint dispResLoc    = glGetUniformLocation(displayProgram, "uResolution");

    // 累积用浮点帧缓冲（HDR，全局变量 accumFBO/accumTex）
    createAccumTarget(accumFBO, accumTex, fbWidth, fbHeight);

    // FPS 统计
    int    frames   = 0;
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        processInput(window);

        // P 键保存当前画面截图
        static bool pPressed = false;
        if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS && !pPressed) {
            saveScreenshot("screenshot.bmp", fbWidth, fbHeight);
        }
        pPressed = (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS);

        // 窗口尺寸变化 → 重建累积纹理
        if (fbResized) {
            glDeleteTextures(1, &accumTex);
            glDeleteFramebuffers(1, &accumFBO);
            createAccumTarget(accumFBO, accumTex, fbWidth, fbHeight);
            fbResized  = false;
            resetAccum = true;
        }

        // 相机/参数变化 → 清空累积，重新收敛
        if (resetAccum) {
            frameCount = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            resetAccum = false;
        }

        // ---- 第 1 遍：路径追踪，每个像素追加 uSpp 个新采样 ----
        glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
        glViewport(0, 0, fbWidth, fbHeight);
        glUseProgram(rayProgram);
        glUniform2f(resLoc,       (float)fbWidth, (float)fbHeight);
        glUniform3f(camPosLoc,    camPos.x, camPos.y, camPos.z);
        glUniform3f(camTargetLoc, camTarget.x, camTarget.y, camTarget.z);
        glUniform1f(fovLoc,       fov);
        glUniform1f(seedLoc,      (float)frameCount);
        glUniform1i(bounceLoc,    maxBounce);
        glUniform1i(sppLoc,       spp);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE); // HDR 采样求和
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisable(GL_BLEND);
        frameCount++;

        // ---- 第 2 遍：取平均 + 色调映射 + Gamma，显示到屏幕 ----
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbWidth, fbHeight);
        glUseProgram(displayProgram);
        glUniform1f(frameCountLoc, (float)frameCount);
        glUniform2f(dispResLoc,    (float)fbWidth, (float)fbHeight);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, accumTex);
        glUniform1i(accumTexLoc, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 标题栏显示采样数 / 每帧采样 / 反弹数 / FPS
        frames++;
        double now = glfwGetTime();
        if (now - lastTime >= 0.5) {
            double fps = frames / (now - lastTime);
            char title[160];
            snprintf(title, sizeof(title),
                     "04-实时光线追踪 | 累积:%u | 采样/帧:%d | 反弹:%d | FPS:%.0f",
                     frameCount, spp, maxBounce, fps);
            glfwSetWindowTitle(window, title);
            frames   = 0;
            lastTime = now;
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

        // 快照模式：累积到目标帧数后保存并退出
        if (snapshotMode && frameCount >= (unsigned int)snapshotFrames) {
            saveScreenshot("screenshot.bmp", fbWidth, fbHeight);
            break;
        }
    }

    // 清理
    glDeleteProgram(rayProgram);
    glDeleteProgram(displayProgram);
    glDeleteFramebuffers(1, &accumFBO);
    glDeleteTextures(1, &accumTex);
    glDeleteVertexArrays(1, &emptyVAO);
    glfwTerminate();
    return 0;
}

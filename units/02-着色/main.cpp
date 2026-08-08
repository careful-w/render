#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <vector>
#include <cstdlib>
#include <ctime>

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

// 三角形1的顶点数据
float vertices[] = {
    0.5f, 0.5f, 0.0f,   // 右上角
    0.5f, -0.5f, 0.0f,  // 右下角
    -0.5f, 0.5f, 0.0f  // 左上角
};

// 三角形2的顶点数据
float vertices2[] = {
    0.5f, -0.5f, 0.0f,  // 右下角
    -0.5f, -0.5f, 0.0f, // 左下角
    -0.5f, 0.5f, 0.0f   // 左上角
};

// 索引数据
unsigned int indices[] = {
    0,1,3,
    1,2,3
};

// 三角形1的位置偏移（通过 uniform voffset 控制）
glm::vec3 offset = glm::vec3(0.0f, 0.0f, 0.0f);

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

// 键盘输入处理：W/S 控制 X 轴偏移，A/D 控制 Y 轴偏移
void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        offset.x += 0.1f;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        offset.x -= 0.1f;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        offset.y += 0.1f;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        offset.y -= 0.1f;
}

// 初始化窗口
GLFWwindow* inte() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    GLFWwindow* window = glfwCreateWindow(800,600,"window",NULL,NULL);
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
    return window;
}

// 创建着色器（从文件路径加载）
unsigned int createSimpleShader(const char* vertexPath, const char* fragmentPath) {
    std::string vertSrc = readShaderFile(vertexPath);
    std::string fragSrc = readShaderFile(fragmentPath);
    if (vertSrc.empty() || fragSrc.empty()) {
        std::cout << "ERROR::SHADER::FILE_READ_FAILED" << std::endl;
        return 0;
    }
    const char* vSrc = vertSrc.c_str();
    const char* fSrc = fragSrc.c_str();

    // 顶点着色器创建和编译
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vSrc, NULL);
    glCompileShader(vertexShader);

    // 片段着色器创建和编译
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fSrc, NULL);
    glCompileShader(fragmentShader);

    // 编译错误检查
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX(" << vertexPath << ")\n" << infoLog << std::endl;
    }

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT(" << fragmentPath << ")\n" << infoLog << std::endl;
    }

    // 创建程序并链接
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // 链接错误检查
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM\n" << infoLog << std::endl;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}

// 初始化顶点数据（VAO + VBO + EBO）
void initVertexData(unsigned int &VAO, unsigned int &VBO,unsigned int &EBO,
                    const float* pVertices, int vertexDataSize,
                    unsigned int* pIndices, int indexDataSize) {
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexDataSize, pVertices, GL_STATIC_DRAW);

    // 顶点属性：layout(location=0)，每个顶点3个float
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}


int main() {
    GLFWwindow* window = inte();

    // ─── 三角形1：支持 uniform 着色和移动 ───
    unsigned int VBO, VAO, EBO;
    initVertexData(VAO,VBO,EBO,vertices,sizeof(vertices),indices,sizeof(indices));

    unsigned int shaderProgram = createSimpleShader("units/02-着色/shaders/triangle.vert", "units/02-着色/shaders/triangle.frag");
    glUseProgram(shaderProgram);
    // 获取 uniform 位置（只需获取一次，不需要每帧重复）
    GLint colorLoc = glGetUniformLocation(shaderProgram, "uColor");
    GLint voffsetLoc = glGetUniformLocation(shaderProgram, "voffset");

    // ─── 三角形2：固定着色（红色） ───
    unsigned int VBO2, VAO2, EBO2;
    initVertexData(VAO2,VBO2,EBO2,vertices2,sizeof(vertices2),indices,sizeof(indices));

    unsigned int shaderProgram2 = createSimpleShader("units/02-着色/shaders/triangle2.vert", "units/02-着色/shaders/triangle2.frag");
    glUseProgram(shaderProgram2);

    // 动态颜色（三角形1的 RGB 渐变）
    float colors[] = {0.0f, 0.0f, 0.0f};

    while(!glfwWindowShouldClose(window))
    {
        processInput(window);

        // 清屏
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 绘制三角形1（动态着色 + 键盘移动）
        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glUniform3f(colorLoc, colors[0], colors[1], colors[2]);
        glUniform3f(voffsetLoc, offset.x, offset.y, offset.z);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 颜色渐变循环
        colors[0] = (colors[0]+0.005f)>1.0f ? 0.0f : (colors[0]+0.005f);
        colors[1] = (colors[1]+0.007f)>1.0f ? 0.0f : (colors[1]+0.007f);
        colors[2] = (colors[2]+0.009f)>1.0f ? 0.0f : (colors[2]+0.009f);

        // 绘制三角形2（固定着色，纯色填充）
        glUseProgram(shaderProgram2);
        glBindVertexArray(VAO2);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 清理
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shaderProgram);

    glDeleteVertexArrays(1, &VAO2);
    glDeleteBuffers(1, &VBO2);
    glDeleteBuffers(1, &EBO2);
    glDeleteProgram(shaderProgram2);

    glfwTerminate();
    return 0;
}

/**
 *练习内容片段着色器将纹理采样颜色与顶点插值颜色进行混合。通过键盘上的上/ 下键调整混合因子
 */ 

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

//点位置
float vertices[] = {
    //     ---- 位置 ----       ---- 颜色 ----     - 纹理坐标 -
    0.5f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f,   // 右上
    0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f,   // 右下
   -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f,   // 左下
   -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f    // 左上
};

//点索引
unsigned int indices[] = {
    0,1,3,
    1,2,3
};

float mixValue = 0.2f;

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

// 加载纹理（自动适配 RGB/RGBA 通道）
unsigned int loadTexture(const char* path) {
    unsigned int texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // 设置纹理环绕参数
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // 设置纹理过滤参数
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 加载图像，创建纹理并生成mipmaps
    int width, height, nrChannels;
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);
    if (data) {
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        std::cout << "加载纹理失败: " << path << std::endl;
    }
    stbi_image_free(data);
    return texture;
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        mixValue += 0.01f;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        mixValue -= 0.01f;
    // 限制范围
    if (mixValue > 1.0f) mixValue = 1.0f;
    if (mixValue < 0.0f) mixValue = 0.0f;
}

//初始化窗口
GLFWwindow* inte() {
    //初始化glfw
    glfwInit();
    //设置opengl版本
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    //创建窗口
    GLFWwindow* window = glfwCreateWindow(800,600,"练习1：纹理+颜色混合",NULL,NULL);
    if (!window) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        exit(-1);
    }
    //设置到当前上下文
    glfwMakeContextCurrent(window);
    //加载GLLoad
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        exit(-1);
    }
    //Framebuffer缓冲帧
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    return window;
}
//创建着色器（从文件路径加载）
unsigned int createSimpleShader(const char* vertexPath, const char* fragmentPath) {
    // 读取文件
    std::string vertSrc = readShaderFile(vertexPath);
    std::string fragSrc = readShaderFile(fragmentPath);
    if (vertSrc.empty() || fragSrc.empty()) {
        std::cout << "ERROR::SHADER::FILE_READ_FAILED" << std::endl;
        return 0;
    }
    const char* vSrc = vertSrc.c_str();
    const char* fSrc = fragSrc.c_str();

    //顶点着色器创建和编译
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vSrc, NULL);
    glCompileShader(vertexShader);

    //片段着色器创建和编译
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fSrc, NULL);
    glCompileShader(fragmentShader);

    //编译错误检查
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

    //创建程序
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    //链接错误检查
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM\n" << infoLog << std::endl;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}

/**
 *
 * @param VAO 顶点缓冲对象
 * @param VBO 顶点属性配置
 * @param EBO 索引缓冲对象
 * @param pVertices 顶点坐标数据
 * @param vertexDataSize 缓冲对象数据大小
 * @param pIndices 顶点坐标数据
 * @param indexDataSize 缓冲对象数据大小
 */
void initVertexData(unsigned int &VAO, unsigned int &VBO,unsigned int &EBO,
                    const float* pVertices, int vertexDataSize,
                    unsigned int* pIndices, int indexDataSize) {
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    //绑定缓冲对象数据
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexDataSize, pVertices, GL_STATIC_DRAW);

    //索引缓存对象绑定数据
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexDataSize, pIndices, GL_STATIC_DRAW);

    //设置顶点属性
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);//点位置
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));//颜色
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));//贴图坐标
    glEnableVertexAttribArray(2);

    //解除绑定
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}


int main() {
    GLFWwindow* window = inte();
    //初始化数据
    unsigned int VBO, VAO, EBO;
    initVertexData(VAO,VBO,EBO,vertices,sizeof(vertices),indices,sizeof(indices));

    // 加载纹理
    unsigned int texture1 = loadTexture("image/container.jpg");

    // 创建着色器程序
    unsigned int shaderProgram = createSimpleShader("units/03-纹理/练习/shaders/triangle.vert", "units/03-纹理/练习/shaders/triangle.frag");
    glUseProgram(shaderProgram);

    glUniform1i(glGetUniformLocation(shaderProgram, "texture1"), 0);

    GLint mixValueLoc = glGetUniformLocation(shaderProgram, "mixValue");
    while(!glfwWindowShouldClose(window))
    {
        processInput(window);//输入事件
        //清空颜色屏幕
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform1f(mixValueLoc, mixValue);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture1);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        //按照索引去绘制
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        // 检查并调用事件，交换缓冲
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    return 0;
}

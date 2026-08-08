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

//点位置
float vertices[] = {
    0.5f, 0.5f, 0.0f,   // 右上角
    0.5f, -0.5f, 0.0f,  // 右下角
    -0.5f, 0.5f, 0.0f  // 左上角
};

float vertices2[] = {
    0.5f, -0.5f, 0.0f,  // 右下角
    -0.5f, -0.5f, 0.0f, // 左下角
    -0.5f, 0.5f, 0.0f   // 左上角
};


//点索引
unsigned int indices[] = {
    0,1,3,
    1,2,3
};
//移动方向
glm::vec3 offset = glm::vec3(0.0f, 0.0f, 0.0f);

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

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
    GLFWwindow* window = glfwCreateWindow(800,600,"window",NULL,NULL);
    if (!window) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();//终止
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
    // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    // glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexDataSize, pIndices, GL_STATIC_DRAW);

    //设置顶点属性
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    //解除绑定
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}


int main() {
    GLFWwindow* window = inte();
    //初始化数据
    unsigned int VBO, VAO, EBO;
    initVertexData(VAO,VBO,EBO,vertices,sizeof(vertices),indices,sizeof(indices));



    // 创建着色器程序
    unsigned int shaderProgram = createSimpleShader("shaders/triangle.vert", "shaders/triangle.frag");
    glUseProgram(shaderProgram);

    // unsigned int VBO2, VAO2, EBO2;
    // initVertexData(VAO2,VBO2,EBO2,vertices2,sizeof(vertices2),indices,sizeof(indices));
    //
    // unsigned int shaderProgram2 = createSimpleShader("shaders/triangle2.vert", "shaders/triangle2.frag");
    // glUseProgram(shaderProgram2);

    float colors[] = {0.0f, 0.0f, 0.0f};
    GLint colorLoc = glGetUniformLocation(shaderProgram, "uColor");

    GLint voffset = glGetUniformLocation(shaderProgram, "voffset");

    while(!glfwWindowShouldClose(window))
    {
        processInput(window);//输入事件


        //清空颜色屏幕
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);

        glUniform3f(colorLoc, colors[0], colors[1], colors[2]);

        glUniform3f(voffset, offset.x, offset.y, offset.z);

        glDrawArrays(GL_TRIANGLES, 0, 3);

        /*colors[0] = (colors[0]+0.005f)>1.0f ? 0.0f : (colors[0]+0.005f);
        colors[1] = (colors[1]+0.007f)>1.0f ? 0.0f : (colors[1]+0.007f);
        colors[2] = (colors[2]+0.009f)>1.0f ? 0.0f : (colors[2]+0.009f);*/
        // glUseProgram(shaderProgram2);
        // glBindVertexArray(VAO2);
        //按照索引去绘制
        //glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        //按照点去绘制
        // glDrawArrays(GL_TRIANGLES, 0, 3);
        // 检查并调用事件，交换缓冲
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shaderProgram);

    // glDeleteVertexArrays(1, &VAO2);
    // glDeleteBuffers(1, &VBO2);
    // glDeleteBuffers(1, &EBO2);
    // glDeleteProgram(shaderProgram2);

    glfwTerminate();
    return 0;
}

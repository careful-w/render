#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>

//顶点着色器
const char *vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
    "}\0";
//片段着色器
const char *fragmentShaderSource = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);\n"
    "}\n\0";
//点位置
float vertices[] = {
    0.5f,  0.5f, 0.0f,  // top right
    0.5f, -0.5f, 0.0f,  // bottom right
    -0.5f, -0.5f, 0.0f,  // bottom left
    -0.5f,  0.5f, 0.0f   // top left
};
//点索引
unsigned int indices[] = {
    0,1,3,
    1,2,3
};

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);
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
//创建着色器
unsigned int createSimpleShader() {
    //顶点着色器创建和编译
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);//创建着色器对象
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);//绑定着色器代码
    glCompileShader(vertexShader);//编译着色器

    //片段着色器创建和编译
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    //如果编译失败抛出异常
    int success;
    char infoLog[512];
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT_SHADER\n" << infoLog << std::endl;
    }

    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX_SHADER\n" << infoLog << std::endl;
    }

    //创建程序
    unsigned int shaderProgram = glCreateProgram();
    //将着色器挂靠进程序
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    //判断连接是否成功
    glGetShaderiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shaderProgram, 512, NULL, infoLog);
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
    glBufferData(GL_ARRAY_BUFFER, vertexDataSize, vertices, GL_STATIC_DRAW);

    //索引缓存对象绑定数据
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexDataSize, indices, GL_STATIC_DRAW);

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
    initVertexData(VBO,VAO,EBO,vertices,sizeof(vertices),indices,sizeof(indices));

    // 创建着色器程序
    unsigned int shaderProgram = createSimpleShader();
    glUseProgram(shaderProgram);

    while(!glfwWindowShouldClose(window))
    {
        processInput(window);//输入事件
        //清空颜色屏幕
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

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

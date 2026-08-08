cmake_minimum_required(VERSION 3.20)
project(OpenGL)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成 compile_commands.json，供 clangd 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# MinGW g++ 使用 libstdc++，不需要 -stdlib 标志
# 修复MinGW汇编器不支持新版dwarf的问题
add_compile_options(-gdwarf-2)

# 设置vcpkg安装路径
set(VCPKG_INSTALLED_DIR "G:/vcpkg-master/installed" CACHE PATH "Vcpkg installed directory")
set(VCPKG_TARGET_TRIPLET "x64-mingw-static" CACHE STRING "Vcpkg target triplet")

# 设置各个库的DIR路径
set(glad_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/glad" CACHE PATH "glad config directory" FORCE)
set(glfw3_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/glfw3" CACHE PATH "glfw3 config directory" FORCE)
set(glm_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/glm" CACHE PATH "glm config directory" FORCE)

# 添加vcpkg安装路径到CMAKE_PREFIX_PATH
list(APPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

# 查找vcpkg安装的库（一键找到所有依赖）
find_package(glfw3 REQUIRED)
find_package(glad REQUIRED)
find_package(glm REQUIRED)

# 添加可执行文件
add_executable(OpenGL main.cpp)

# 显式添加vcpkg include路径（clangd需要）
target_include_directories(OpenGL PRIVATE
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)

# 链接所有库（vcpkg安装的库直接用官方目标名）
target_link_libraries(OpenGL PRIVATE
        glfw
        glad::glad  # vcpkg安装的GLAD目标名
        glm::glm    # vcpkg安装的GLM目标名
)

# Windows系统额外链接opengl32（必加）
if (WIN32)
    target_link_libraries(OpenGL PRIVATE opengl32)
endif()

# 将 shaders 目录复制到构建目录，确保运行时能找到
add_custom_command(TARGET OpenGL POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:OpenGL>/shaders"
    COMMENT "Copying shaders to build directory"
)
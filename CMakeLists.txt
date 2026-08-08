cmake_minimum_required(VERSION 3.20)

# vcpkg 集成：通过环境变量 VCPKG_ROOT 自动加载工具链文件
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file")
endif()

project(OpenGL)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# 可执行文件
add_executable(OpenGL main.cpp)

target_link_libraries(OpenGL PRIVATE
    glfw
    glad::glad
    glm::glm
)

if(WIN32)
    target_link_libraries(OpenGL PRIVATE opengl32)
endif()

# 复制 shaders 到输出目录
add_custom_command(TARGET OpenGL POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:OpenGL>/shaders"
    COMMENT "Copying shaders to build directory"
)

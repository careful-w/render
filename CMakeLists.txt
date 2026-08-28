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

# 可执行文件（当前运行单元：04-光线追踪，源码即根目录 main.cpp）
add_executable(OpenGL main.cpp)

target_link_libraries(OpenGL PRIVATE
    glfw
    glad::glad
    glm::glm
)

if(WIN32)
    target_link_libraries(OpenGL PRIVATE opengl32)
endif()

# 收集资源文件
file(GLOB_RECURSE ASSET_SOURCES
    "${CMAKE_SOURCE_DIR}/shaders/*"
    "${CMAKE_SOURCE_DIR}/image/*"
    "${CMAKE_SOURCE_DIR}/units/04-光线追踪/shaders/*"
)

# 自定义 target：依赖所有 shader/image 源文件，文件变化时自动重新复制
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:OpenGL>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/image"
    "$<TARGET_FILE_DIR:OpenGL>/image"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/units/04-光线追踪/shaders"
    "$<TARGET_FILE_DIR:OpenGL>/shaders"
    COMMENT "Copying shaders and images to build directory"
    DEPENDS ${ASSET_SOURCES}
)

# 确保 OpenGL 链接前先复制资源
add_dependencies(OpenGL copy_assets)

cmake_minimum_required(VERSION 3.10)
project(v4l2_demo VERSION 1.0.0 LANGUAGES CXX)

# 生成 compile_commands.json，便于 clang/clangd 等工具正确解析头文件路径
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 公共库源文件
set(COMMON_SOURCES
    src/common/v4l2_utils.cpp
)

# 创建公共库
add_library(v4l2_common STATIC ${COMMON_SOURCES})
target_include_directories(v4l2_common
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/common
)

# Demo 1: UYVY422 视频流捕获
add_executable(demo1_uyvy422
    src/demos/demo1_uyvy422/main.cpp
)
target_link_libraries(demo1_uyvy422 v4l2_common)
target_include_directories(demo1_uyvy422
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
)

# 可以在这里添加更多 demo
# add_executable(demo2_xxx ...)
# target_link_libraries(demo2_xxx v4l2_common)

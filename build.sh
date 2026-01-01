#!/bin/bash

# V4L2 Demo 项目构建脚本

set -e

# 创建构建目录
if [ ! -d "build" ]; then
    mkdir build
fi

cd build

# 运行 CMake
echo "运行 CMake..."
cmake ..

# 编译
echo "开始编译..."
make -j$(nproc)

echo ""
echo "编译完成！"
echo "可执行文件位于: build/bin/"
echo ""
echo "运行 Demo 1:"
echo "  cd build/bin && ./demo1_uyvy422"


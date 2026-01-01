# V4L2 Demo 项目

这是一个用于学习 Linux V4L2 (Video4Linux2) 框架的 C++ 演示项目。

## 项目结构

```
v4l2-demo/
├── CMakeLists.txt          # CMake 构建文件
├── README.md               # 项目说明文档
├── src/
│   ├── common/             # 公共工具代码
│   │   ├── v4l2_utils.h    # V4L2 设备封装类头文件
│   │   └── v4l2_utils.cpp  # V4L2 设备封装类实现
│   └── demos/              # Demo 程序目录
│       └── demo1_uyvy422/  # Demo 1: UYVY422 视频流捕获
│           └── main.cpp
└── output/                 # 输出目录（保存的帧图片）
```

## 编译要求

- Ubuntu 24.04 (或其他 Linux 发行版)
- CMake 3.10 或更高版本
- g++ 编译器（支持 C++17）
- Linux 内核头文件（用于 V4L2）

## 编译步骤

1. 创建构建目录：
```bash
mkdir build
cd build
```

2. 运行 CMake：
```bash
cmake ..
```

3. 编译：
```bash
make
```

4. 编译完成后，可执行文件位于 `build/bin/` 目录下。

## Demo 说明

### Demo 1: UYVY422 视频流捕获

**功能：**
- 使用本机前置摄像头（默认使用 `/dev/video0`）
- 获取 UYVY422 格式的视频流
- 每帧打印基本信息（帧数、帧率、尺寸等）
- 每秒保存一帧到 `output/` 目录
- 最多保存 20 张图片，循环覆盖

**运行：**
```bash
cd build/bin
./demo1_uyvy422
```

**输出：**
- 控制台输出：每帧的详细信息
- 文件输出：`output/frame_000.raw` 到 `output/frame_019.raw`（循环覆盖）

**注意事项：**
- 需要摄像头设备权限（可能需要将用户添加到 `video` 组）
- 确保设备支持 UYVY422 格式
- 按 `Ctrl+C` 退出程序

## 添加新的 Demo

1. 在 `src/demos/` 目录下创建新的 demo 目录，例如 `demo2_xxx/`
2. 创建 `main.cpp` 文件
3. 在 `CMakeLists.txt` 中添加新的可执行文件配置：
```cmake
add_executable(demo2_xxx
    src/demos/demo2_xxx/main.cpp
)
target_link_libraries(demo2_xxx v4l2_common)
```

## 代码风格

本项目遵循 Google C++ 风格指南：
- 使用 2 空格缩进
- 类名使用 PascalCase
- 函数和变量名使用 snake_case
- 常量使用 k 前缀
- 详细的注释和文档

## 依赖

- Linux V4L2 API（系统自带）
- 标准 C++ 库

## 许可证

本项目仅用于学习和演示目的。


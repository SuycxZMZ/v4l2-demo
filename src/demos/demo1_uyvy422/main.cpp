#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include "v4l2_utils.h"

using v4l2_demo::V4L2Device;
using v4l2_demo::DeviceInfo;
using v4l2_demo::FindVideoDevices;
using v4l2_demo::PixelFormatToString;

// 配置参数
namespace {
// 视频格式配置
constexpr uint32_t kVideoWidth = 640;
constexpr uint32_t kVideoHeight = 480;

// 帧保存配置
constexpr int kMaxSavedFrames = 20;  // 最多保存 20 张图片
constexpr int kSaveIntervalSeconds = 1;  // 每秒保存一帧
constexpr const char* kOutputDirectory = "output";  // 输出目录

// 优先选择的格式列表（按优先级排序）
// 优先选择未压缩格式，然后是压缩格式
constexpr uint32_t kPreferredFormats[] = {
    V4L2_PIX_FMT_YUYV,   // YUYV 4:2:2 (未压缩)
    V4L2_PIX_FMT_UYVY,   // UYVY 4:2:2 (未压缩)
    V4L2_PIX_FMT_YUV420, // YUV 4:2:0 (未压缩)
    V4L2_PIX_FMT_MJPEG,  // Motion-JPEG (压缩)
    V4L2_PIX_FMT_JPEG,   // JPEG (压缩)
};
constexpr size_t kPreferredFormatsCount = sizeof(kPreferredFormats) / sizeof(kPreferredFormats[0]);

// 帧统计信息
struct FrameStats {
  uint64_t total_frames;      // 总帧数
  uint64_t saved_frames;      // 已保存帧数
  time_t start_time;          // 开始时间
  time_t last_save_time;      // 上次保存时间
  uint32_t current_frame_index;  // 当前保存的帧索引（用于循环覆盖）
};
}  // namespace

// 创建输出目录
// @return 成功返回 true，失败返回 false
bool CreateOutputDirectory() {
  struct stat st;
  if (stat(kOutputDirectory, &st) != 0) {
    // 目录不存在，创建它
    if (mkdir(kOutputDirectory, 0755) != 0) {
      fprintf(stderr, "无法创建输出目录 %s: %s\n", kOutputDirectory,
              strerror(errno));
      return false;
    }
    printf("已创建输出目录: %s\n", kOutputDirectory);
  }
  return true;
}

// 根据像素格式获取文件扩展名
// @param pixel_format 像素格式
// @return 文件扩展名（不含点号）
std::string GetFileExtension(uint32_t pixel_format) {
  if (pixel_format == V4L2_PIX_FMT_MJPEG || pixel_format == V4L2_PIX_FMT_JPEG) {
    return "jpg";
  }
  // 其他格式使用 raw
  return "raw";
}

// 生成输出文件名
// @param frame_index 帧索引（0-19，用于循环覆盖）
// @param pixel_format 像素格式
// @return 文件路径
std::string GenerateOutputFilename(int frame_index, uint32_t pixel_format) {
  std::ostringstream oss;
  std::string ext = GetFileExtension(pixel_format);
  oss << kOutputDirectory << "/frame_" << std::setfill('0') << std::setw(3)
      << frame_index << "." << ext;
  return oss.str();
}

// 保存帧数据到文件
// @param frame_data 帧数据指针
// @param frame_size 帧数据大小
// @param frame_index 帧索引
// @param pixel_format 像素格式
// @return 成功返回 true，失败返回 false
bool SaveFrameToFile(const void* frame_data, size_t frame_size,
                     int frame_index, uint32_t pixel_format) {
  std::string filename = GenerateOutputFilename(frame_index, pixel_format);
  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    fprintf(stderr, "无法打开文件 %s 进行写入\n", filename.c_str());
    return false;
  }

  file.write(reinterpret_cast<const char*>(frame_data), frame_size);
  file.close();

  if (!file.good()) {
    fprintf(stderr, "写入文件 %s 失败\n", filename.c_str());
    return false;
  }

  printf("已保存帧到: %s (大小: %zu 字节, 格式: %s)\n", filename.c_str(),
         frame_size, PixelFormatToString(pixel_format).c_str());
  return true;
}

// 打印帧信息
// @param stats 帧统计信息
// @param frame_data 帧数据指针
// @param frame_size 帧数据大小
// @param width 视频宽度
// @param height 视频高度
// @param pixel_format 像素格式
void PrintFrameInfo(const FrameStats& stats, const void* frame_data,
                    size_t frame_size, uint32_t width, uint32_t height,
                    uint32_t pixel_format) {
  time_t current_time = time(nullptr);
  double elapsed_seconds = difftime(current_time, stats.start_time);
  double fps = (elapsed_seconds > 0) ? stats.total_frames / elapsed_seconds : 0;

  printf("\n=== 帧信息 ===\n");
  printf("总帧数: %lu\n", stats.total_frames);
  printf("已保存帧数: %lu\n", stats.saved_frames);
  printf("运行时间: %.0f 秒\n", elapsed_seconds);
  printf("平均帧率: %.2f FPS\n", fps);
  printf("视频尺寸: %ux%u\n", width, height);
  printf("像素格式: %s\n", PixelFormatToString(pixel_format).c_str());
  printf("帧大小: %zu 字节\n", frame_size);
  printf("帧数据地址: %p\n", frame_data);
  printf("==============\n");
}

// 查找前置摄像头设备
// @param devices 设备列表（已经过滤，只包含支持视频捕获的设备）
// @return 设备路径，如果未找到返回空字符串
std::string FindFrontCamera(const std::vector<DeviceInfo>& devices) {
  // 通常前置摄像头在 /dev/video0 或 /dev/video2
  // 这里优先查找 /dev/video0，如果不存在则查找第一个可用设备
  std::string first_device = "";
  
  for (const auto& device : devices) {
    printf("找到视频设备: %s (%s)\n", device.device_path.c_str(),
           device.card_name.c_str());
    
    // 记录第一个设备作为备选
    if (first_device.empty()) {
      first_device = device.device_path;
    }
    
    // 优先选择 /dev/video0
    if (device.device_path == "/dev/video0") {
      return device.device_path;
    }
  }

  // 如果没有找到 /dev/video0，返回第一个可用的捕获设备
  return first_device;
}

// 打印所有支持的格式
// @param formats 格式列表
void PrintSupportedFormats(const std::vector<uint32_t>& formats) {
  printf("设备支持的像素格式 (%zu 种):\n", formats.size());
  for (size_t i = 0; i < formats.size(); ++i) {
    printf("  [%zu] %s (0x%08X)\n", i, PixelFormatToString(formats[i]).c_str(),
           formats[i]);
  }
  printf("\n");
}

// 从支持的格式中选择最合适的格式
// @param supported_formats 设备支持的格式列表
// @return 选中的格式，如果未找到返回 0
uint32_t SelectBestFormat(const std::vector<uint32_t>& supported_formats) {
  // 按优先级查找格式
  for (size_t i = 0; i < kPreferredFormatsCount; ++i) {
    uint32_t preferred_format = kPreferredFormats[i];
    for (uint32_t supported_format : supported_formats) {
      if (supported_format == preferred_format) {
        return preferred_format;
      }
    }
  }

  // 如果没有找到优先格式，返回第一个支持的格式
  if (!supported_formats.empty()) {
    return supported_formats[0];
  }

  return 0;
}

int main(int /* argc */, char* /* argv */[]) {
  printf("=== V4L2 Demo 1: 视频流捕获 ===\n\n");

  // 查找可用的视频设备
  std::vector<DeviceInfo> devices;
  int device_count = FindVideoDevices(&devices);
  if (device_count == 0) {
    fprintf(stderr, "错误: 未找到可用的视频设备\n");
    return EXIT_FAILURE;
  }

  printf("找到 %d 个视频设备:\n", device_count);
  for (const auto& device : devices) {
    printf("  - %s: %s (驱动: %s)\n", device.device_path.c_str(),
           device.card_name.c_str(), device.driver_name.c_str());
  }
  printf("\n");

  // 查找前置摄像头
  std::string device_path = FindFrontCamera(devices);
  if (device_path.empty()) {
    fprintf(stderr, "错误: 未找到可用的视频捕获设备\n");
    return EXIT_FAILURE;
  }

  printf("使用设备: %s\n\n", device_path.c_str());

  // 创建输出目录
  if (!CreateOutputDirectory()) {
    return EXIT_FAILURE;
  }

  // 打开设备
  V4L2Device device;
  if (!device.Open(device_path)) {
    fprintf(stderr, "错误: 无法打开设备 %s\n", device_path.c_str());
    return EXIT_FAILURE;
  }

  // 获取设备信息
  DeviceInfo device_info;
  if (!device.GetDeviceInfo(&device_info)) {
    fprintf(stderr, "错误: 无法获取设备信息\n");
    return EXIT_FAILURE;
  }

  printf("设备信息:\n");
  printf("  设备路径: %s\n", device_info.device_path.c_str());
  printf("  设备名称: %s\n", device_info.card_name.c_str());
  printf("  驱动名称: %s\n", device_info.driver_name.c_str());
  printf("  总线信息: %s\n", device_info.bus_info.c_str());
  printf("  支持的格式数量: %zu\n", device_info.formats.size());
  printf("\n");

  // 打印所有支持的格式
  PrintSupportedFormats(device_info.formats);

  // 自动选择最合适的格式
  uint32_t selected_format = SelectBestFormat(device_info.formats);
  if (selected_format == 0) {
    fprintf(stderr, "错误: 设备不支持任何已知的像素格式\n");
    return EXIT_FAILURE;
  }

  printf("自动选择格式: %s\n\n", PixelFormatToString(selected_format).c_str());

  // 设置视频格式
  printf("设置视频格式: %ux%u, 格式: %s\n", kVideoWidth, kVideoHeight,
         PixelFormatToString(selected_format).c_str());
  if (!device.SetFormat(kVideoWidth, kVideoHeight, selected_format)) {
    fprintf(stderr, "错误: 无法设置视频格式\n");
    return EXIT_FAILURE;
  }

  // 验证设置的格式
  uint32_t actual_width, actual_height, actual_format;
  if (!device.GetFormat(&actual_width, &actual_height, &actual_format)) {
    fprintf(stderr, "错误: 无法获取视频格式\n");
    return EXIT_FAILURE;
  }

  printf("实际视频格式: %ux%u, 格式: %s\n\n", actual_width, actual_height,
         PixelFormatToString(actual_format).c_str());

  // 检查实际格式是否匹配（允许设备调整格式）
  if (actual_format != selected_format) {
    printf("注意: 设备调整了格式，从 %s 变为 %s\n",
           PixelFormatToString(selected_format).c_str(),
           PixelFormatToString(actual_format).c_str());
    // 使用实际设置的格式
    selected_format = actual_format;
  }

  // 初始化内存映射
  printf("初始化内存映射缓冲区...\n");
  if (!device.InitMemoryMapping(4)) {
    fprintf(stderr, "错误: 无法初始化内存映射\n");
    return EXIT_FAILURE;
  }
  printf("内存映射初始化成功\n\n");

  // 启动视频流
  printf("启动视频流捕获...\n");
  if (!device.StartStreaming()) {
    fprintf(stderr, "错误: 无法启动视频流\n");
    return EXIT_FAILURE;
  }
  printf("视频流已启动\n\n");

  // 初始化统计信息
  FrameStats stats = {};
  stats.start_time = time(nullptr);
  stats.last_save_time = stats.start_time;
  stats.current_frame_index = 0;

  printf("开始捕获视频帧 (按 Ctrl+C 退出)...\n\n");

  // 主循环：读取并处理帧
  while (true) {
    void* frame_data = nullptr;
    size_t frame_size = 0;

    // 读取一帧
    if (device.ReadFrame(&frame_data, &frame_size)) {
      stats.total_frames++;

      // 打印帧信息
      PrintFrameInfo(stats, frame_data, frame_size, actual_width,
                     actual_height, actual_format);

      // 检查是否需要保存帧（每秒保存一帧）
      time_t current_time = time(nullptr);
      if (difftime(current_time, stats.last_save_time) >=
          kSaveIntervalSeconds) {
        // 保存帧（使用实际设置的格式）
        if (SaveFrameToFile(frame_data, frame_size,
                            stats.current_frame_index, actual_format)) {
          stats.saved_frames++;
          stats.last_save_time = current_time;

          // 更新帧索引（循环覆盖，0-19）
          stats.current_frame_index =
              (stats.current_frame_index + 1) % kMaxSavedFrames;
        }
      }
    } else {
      // 没有可用的帧，短暂休眠避免 CPU 占用过高
      usleep(10000);  // 10ms
    }
  }

  // 清理资源（通常不会执行到这里，因为循环是无限的）
  device.StopStreaming();
  device.Close();

  return EXIT_SUCCESS;
}


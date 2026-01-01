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
constexpr uint32_t kPixelFormat = V4L2_PIX_FMT_UYVY;  // UYVY422 格式

// 帧保存配置
constexpr int kMaxSavedFrames = 20;  // 最多保存 20 张图片
constexpr int kSaveIntervalSeconds = 1;  // 每秒保存一帧
constexpr const char* kOutputDirectory = "output";  // 输出目录

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

// 生成输出文件名
// @param frame_index 帧索引（0-19，用于循环覆盖）
// @return 文件路径
std::string GenerateOutputFilename(int frame_index) {
  std::ostringstream oss;
  oss << kOutputDirectory << "/frame_" << std::setfill('0') << std::setw(3)
      << frame_index << ".raw";
  return oss.str();
}

// 保存帧数据到文件
// @param frame_data 帧数据指针
// @param frame_size 帧数据大小
// @param frame_index 帧索引
// @return 成功返回 true，失败返回 false
bool SaveFrameToFile(const void* frame_data, size_t frame_size,
                     int frame_index) {
  std::string filename = GenerateOutputFilename(frame_index);
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

  printf("已保存帧到: %s (大小: %zu 字节)\n", filename.c_str(), frame_size);
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
// @param devices 设备列表
// @return 设备路径，如果未找到返回空字符串
std::string FindFrontCamera(const std::vector<DeviceInfo>& devices) {
  // 通常前置摄像头在 /dev/video0 或 /dev/video2
  // 这里优先查找 /dev/video0，如果不存在则查找第一个可用设备
  for (const auto& device : devices) {
    // 检查是否是视频捕获设备
    if (device.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
      printf("找到视频设备: %s (%s)\n", device.device_path.c_str(),
             device.card_name.c_str());
      // 优先选择 /dev/video0
      if (device.device_path == "/dev/video0") {
        return device.device_path;
      }
    }
  }

  // 如果没有找到 /dev/video0，返回第一个可用的捕获设备
  for (const auto& device : devices) {
    if (device.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
      return device.device_path;
    }
  }

  return "";
}

int main(int argc, char* argv[]) {
  printf("=== V4L2 Demo 1: UYVY422 视频流捕获 ===\n\n");

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

  // 检查是否支持 UYVY 格式
  bool supports_uyvy = false;
  for (uint32_t format : device_info.formats) {
    if (format == V4L2_PIX_FMT_UYVY) {
      supports_uyvy = true;
      break;
    }
  }

  if (!supports_uyvy) {
    printf("警告: 设备可能不支持 UYVY422 格式，尝试设置...\n");
  }

  // 设置视频格式
  printf("设置视频格式: %ux%u, 格式: UYVY422\n", kVideoWidth, kVideoHeight);
  if (!device.SetFormat(kVideoWidth, kVideoHeight, kPixelFormat)) {
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

  if (actual_format != kPixelFormat) {
    fprintf(stderr, "错误: 设备不支持 UYVY422 格式\n");
    return EXIT_FAILURE;
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
        // 保存帧
        if (SaveFrameToFile(frame_data, frame_size,
                            stats.current_frame_index)) {
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


#ifndef V4L2_DEMO_SRC_COMMON_V4L2_UTILS_H_
#define V4L2_DEMO_SRC_COMMON_V4L2_UTILS_H_

#include <linux/videodev2.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace v4l2_demo {

// V4L2 设备信息结构体
struct DeviceInfo {
  std::string device_path;      // 设备路径，如 /dev/video0
  std::string driver_name;      // 驱动名称
  std::string card_name;        // 设备名称
  std::string bus_info;         // 总线信息
  uint32_t capabilities;        // 设备能力
  std::vector<uint32_t> formats; // 支持的像素格式列表
};

// 帧缓冲区信息
struct FrameBuffer {
  void* start;        // 缓冲区起始地址
  size_t length;      // 缓冲区长度
  uint32_t index;     // 缓冲区索引
};

// V4L2 设备封装类
class V4L2Device {
 public:
  V4L2Device();
  ~V4L2Device();

  // 打开设备
  // @param device_path 设备路径，如 "/dev/video0"
  // @return 成功返回 true，失败返回 false
  bool Open(const std::string& device_path);

  // 关闭设备
  void Close();

  // 检查设备是否已打开
  // @return 已打开返回 true，否则返回 false
  bool IsOpen() const { return fd_ >= 0; }

  // 获取设备信息
  // @param info 输出参数，设备信息
  // @return 成功返回 true，失败返回 false
  bool GetDeviceInfo(DeviceInfo* info);

  // 设置视频格式
  // @param width 视频宽度
  // @param height 视频高度
  // @param pixel_format 像素格式，如 V4L2_PIX_FMT_UYVY
  // @return 成功返回 true，失败返回 false
  bool SetFormat(uint32_t width, uint32_t height, uint32_t pixel_format);

  // 获取当前视频格式
  // @param width 输出参数，视频宽度
  // @param height 输出参数，视频高度
  // @param pixel_format 输出参数，像素格式
  // @return 成功返回 true，失败返回 false
  bool GetFormat(uint32_t* width, uint32_t* height, uint32_t* pixel_format);

  // 初始化内存映射缓冲区
  // @param buffer_count 缓冲区数量，通常为 4
  // @return 成功返回 true，失败返回 false
  bool InitMemoryMapping(uint32_t buffer_count = 4);

  // 清理内存映射缓冲区
  void CleanupMemoryMapping();

  // 开始视频流捕获
  // @return 成功返回 true，失败返回 false
  bool StartStreaming();

  // 停止视频流捕获
  // @return 成功返回 true，失败返回 false
  bool StopStreaming();

  // 读取一帧数据
  // @param frame_data 输出参数，帧数据指针
  // @param frame_size 输出参数，帧数据大小
  // @return 成功返回 true，失败返回 false
  bool ReadFrame(void** frame_data, size_t* frame_size);

  // 将帧数据入队（用于循环缓冲区）
  // @return 成功返回 true，失败返回 false
  bool QueueBuffer(uint32_t index);

  // 获取文件描述符（用于 select/poll）
  int GetFileDescriptor() const { return fd_; }

 private:
  int fd_;  // 设备文件描述符
  std::vector<FrameBuffer> buffers_;  // 内存映射缓冲区列表
  bool streaming_;  // 是否正在流式传输

  // 查询设备能力
  bool QueryCapabilities(struct v4l2_capability* cap);

  // 查询支持的像素格式
  bool QueryFormats(std::vector<uint32_t>* formats);
};

// 工具函数：查找可用的视频设备
// @param devices 输出参数，找到的设备列表
// @return 找到的设备数量
int FindVideoDevices(std::vector<DeviceInfo>* devices);

// 工具函数：像素格式转字符串
// @param pixel_format 像素格式，如 V4L2_PIX_FMT_UYVY
// @return 格式字符串，如 "UYVY"
std::string PixelFormatToString(uint32_t pixel_format);

}  // namespace v4l2_demo

#endif  // V4L2_DEMO_SRC_COMMON_V4L2_UTILS_H_


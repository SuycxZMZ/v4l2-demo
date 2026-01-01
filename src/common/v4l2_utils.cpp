#include "v4l2_utils.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace v4l2_demo {

V4L2Device::V4L2Device() : fd_(-1), streaming_(false) {}

V4L2Device::~V4L2Device() {
  Close();
}

bool V4L2Device::Open(const std::string& device_path) {
  if (IsOpen()) {
    Close();
  }

  fd_ = open(device_path.c_str(), O_RDWR | O_NONBLOCK, 0);
  if (fd_ < 0) {
    fprintf(stderr, "无法打开设备 %s: %s\n", device_path.c_str(),
            strerror(errno));
    return false;
  }

  return true;
}

void V4L2Device::Close() {
  if (streaming_) {
    StopStreaming();
  }
  CleanupMemoryMapping();

  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool V4L2Device::QueryCapabilities(struct v4l2_capability* cap) {
  if (ioctl(fd_, VIDIOC_QUERYCAP, cap) < 0) {
    fprintf(stderr, "查询设备能力失败: %s\n", strerror(errno));
    return false;
  }
  return true;
}

bool V4L2Device::QueryFormats(std::vector<uint32_t>* formats) {
  formats->clear();

  struct v4l2_fmtdesc fmt_desc;
  memset(&fmt_desc, 0, sizeof(fmt_desc));
  fmt_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt_desc.index = 0;

  while (ioctl(fd_, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
    formats->push_back(fmt_desc.pixelformat);
    fmt_desc.index++;
  }

  return true;
}

bool V4L2Device::GetDeviceInfo(DeviceInfo* info) {
  if (!IsOpen() || !info) {
    return false;
  }

  struct v4l2_capability cap;
  if (!QueryCapabilities(&cap)) {
    return false;
  }

  // device_path 由调用方在调用前设定，这里不覆盖。
  // capabilities 需要考虑 device_caps 标志以获取真实能力位。
  uint32_t capabilities = cap.capabilities;
  if (cap.capabilities & V4L2_CAP_DEVICE_CAPS) {
    capabilities = cap.device_caps;
  }

  info->driver_name = reinterpret_cast<const char*>(cap.driver);
  info->card_name = reinterpret_cast<const char*>(cap.card);
  info->bus_info = reinterpret_cast<const char*>(cap.bus_info);
  info->capabilities = capabilities;

  return QueryFormats(&info->formats);
}

bool V4L2Device::SetFormat(uint32_t width, uint32_t height,
                           uint32_t pixel_format) {
  if (!IsOpen()) {
    return false;
  }

  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = pixel_format;
  fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

  if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
    fprintf(stderr, "设置视频格式失败: %s\n", strerror(errno));
    return false;
  }

  // 检查实际设置的格式
  if (fmt.fmt.pix.pixelformat != pixel_format) {
    fprintf(stderr, "警告: 设备不支持请求的像素格式，实际格式: %s\n",
            PixelFormatToString(fmt.fmt.pix.pixelformat).c_str());
    return false;
  }

  return true;
}

bool V4L2Device::GetFormat(uint32_t* width, uint32_t* height,
                           uint32_t* pixel_format) {
  if (!IsOpen() || !width || !height || !pixel_format) {
    return false;
  }

  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl(fd_, VIDIOC_G_FMT, &fmt) < 0) {
    fprintf(stderr, "获取视频格式失败: %s\n", strerror(errno));
    return false;
  }

  *width = fmt.fmt.pix.width;
  *height = fmt.fmt.pix.height;
  *pixel_format = fmt.fmt.pix.pixelformat;

  return true;
}

bool V4L2Device::InitMemoryMapping(uint32_t buffer_count) {
  if (!IsOpen()) {
    return false;
  }

  CleanupMemoryMapping();

  struct v4l2_requestbuffers req;
  memset(&req, 0, sizeof(req));
  req.count = buffer_count;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
    fprintf(stderr, "请求缓冲区失败: %s\n", strerror(errno));
    return false;
  }

  if (req.count < 2) {
    fprintf(stderr, "缓冲区数量不足\n");
    return false;
  }

  buffers_.resize(req.count);

  // 映射每个缓冲区
  for (uint32_t i = 0; i < req.count; ++i) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
      fprintf(stderr, "查询缓冲区 %u 失败: %s\n", i, strerror(errno));
      CleanupMemoryMapping();
      return false;
    }

    buffers_[i].length = buf.length;
    buffers_[i].index = i;
    buffers_[i].start =
        mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
             buf.m.offset);

    if (buffers_[i].start == MAP_FAILED) {
      fprintf(stderr, "映射缓冲区 %u 失败: %s\n", i, strerror(errno));
      CleanupMemoryMapping();
      return false;
    }
  }

  return true;
}

void V4L2Device::CleanupMemoryMapping() {
  for (auto& buffer : buffers_) {
    if (buffer.start && buffer.start != MAP_FAILED) {
      munmap(buffer.start, buffer.length);
    }
  }
  buffers_.clear();
}

bool V4L2Device::StartStreaming() {
  if (!IsOpen() || buffers_.empty()) {
    return false;
  }

  // 将所有缓冲区入队
  for (uint32_t i = 0; i < buffers_.size(); ++i) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;

    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
      fprintf(stderr, "缓冲区 %u 入队失败: %s\n", i, strerror(errno));
      return false;
    }
  }

  // 开始流式传输
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
    fprintf(stderr, "启动流式传输失败: %s\n", strerror(errno));
    return false;
  }

  streaming_ = true;
  return true;
}

bool V4L2Device::StopStreaming() {
  if (!streaming_) {
    return true;
  }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) {
    fprintf(stderr, "停止流式传输失败: %s\n", strerror(errno));
    return false;
  }

  streaming_ = false;
  return true;
}

bool V4L2Device::ReadFrame(void** frame_data, size_t* frame_size) {
  if (!IsOpen() || !streaming_) {
    return false;
  }

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  // 从队列中取出一个已填充的缓冲区
  if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
    if (errno == EAGAIN) {
      // 没有可用的帧，非阻塞模式正常情况
      return false;
    }
    fprintf(stderr, "出队缓冲区失败: %s\n", strerror(errno));
    return false;
  }

  if (buf.index >= buffers_.size()) {
    fprintf(stderr, "缓冲区索引超出范围\n");
    return false;
  }

  *frame_data = buffers_[buf.index].start;
  *frame_size = buf.bytesused;

  // 将缓冲区重新入队
  if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
    fprintf(stderr, "重新入队缓冲区失败: %s\n", strerror(errno));
    return false;
  }

  return true;
}

bool V4L2Device::QueueBuffer(uint32_t index) {
  if (index >= buffers_.size()) {
    return false;
  }

  struct v4l2_buffer buf;
  memset(&buf, 0, sizeof(buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = index;

  if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
    fprintf(stderr, "缓冲区 %u 入队失败: %s\n", index, strerror(errno));
    return false;
  }

  return true;
}

int FindVideoDevices(std::vector<DeviceInfo>* devices) {
  devices->clear();

  const char* dev_dir = "/dev";
  DIR* dir = opendir(dev_dir);
  if (!dir) {
    return 0;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strncmp(entry->d_name, "video", 5) == 0) {
      std::string device_path = std::string(dev_dir) + "/" + entry->d_name;

      V4L2Device device;
      if (device.Open(device_path)) {
        DeviceInfo info;
        info.device_path = device_path;
        if (device.GetDeviceInfo(&info)) {
          devices->push_back(info);
        }
        device.Close();
      }
    }
  }

  closedir(dir);
  return devices->size();
}

std::string PixelFormatToString(uint32_t pixel_format) {
  char format[5];
  format[0] = (pixel_format) & 0xFF;
  format[1] = (pixel_format >> 8) & 0xFF;
  format[2] = (pixel_format >> 16) & 0xFF;
  format[3] = (pixel_format >> 24) & 0xFF;
  format[4] = '\0';
  return std::string(format);
}

}  // namespace v4l2_demo

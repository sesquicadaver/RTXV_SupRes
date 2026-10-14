#pragma once

#include <memory>
#include <string>

extern "C" {
struct AVFrame;
struct AVBufferRef;
}

namespace rtx {

class FrameProcessor {
 public:
  virtual ~FrameProcessor() = default;
  virtual bool open(AVBufferRef* hw_device, int src_w, int src_h, int scale, int quality) = 0;
  virtual int output_width() const = 0;
  virtual int output_height() const = 0;
  virtual bool process(AVFrame* input, AVFrame* output, double& process_ms) = 0;
  virtual void set_quality(int quality) = 0;
  virtual int quality() const = 0;
  virtual const std::string& last_error() const = 0;
  virtual void close() = 0;
};

std::unique_ptr<FrameProcessor> create_frame_processor();

}  // namespace rtx

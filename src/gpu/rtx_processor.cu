#include "rtx_processor.hpp"

#include "color.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/pixfmt.h>
}

#include <cuda.h>
#include <cuda_runtime.h>

#include "rtx_video_api.h"

namespace rtx {
namespace {

void launch_nv12_to_bgra(const uint8_t* y, int y_pitch, const uint8_t* uv, int uv_pitch, uint8_t* bgra, int bgra_pitch,
                         int width, int height, ColorCoeffs color, CUstream stream);
void launch_bgra_to_nv12(const uint8_t* bgra, int bgra_pitch, uint8_t* y, int y_pitch, uint8_t* uv, int uv_pitch,
                         int width, int height, ColorCoeffs color, CUstream stream);
void launch_nv12_scale(const uint8_t* src_y, int src_y_pitch, const uint8_t* src_uv, int src_uv_pitch, int src_w,
                       int src_h, uint8_t* dst_y, int dst_y_pitch, uint8_t* dst_uv, int dst_uv_pitch, int dst_w,
                       int dst_h, CUstream stream);

__global__ void nv12_to_bgra_kernel(const uint8_t* y, int y_pitch, const uint8_t* uv, int uv_pitch, uint8_t* bgra,
                                    int bgra_pitch, int width, int height, ColorCoeffs color) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int row = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || row >= height) {
    return;
  }
  const int luma = static_cast<int>(y[row * y_pitch + x]);
  const int uv_x = x & ~1;
  const int chroma_u = static_cast<int>(uv[(row / 2) * uv_pitch + uv_x]);
  const int chroma_v = static_cast<int>(uv[(row / 2) * uv_pitch + uv_x + 1]);
  int red = 0;
  int green = 0;
  int blue = 0;
  yuv_to_rgb(color, luma, chroma_u, chroma_v, red, green, blue);
  uint8_t* pixel = bgra + row * bgra_pitch + x * 4;
  pixel[0] = static_cast<uint8_t>(blue);
  pixel[1] = static_cast<uint8_t>(green);
  pixel[2] = static_cast<uint8_t>(red);
  pixel[3] = 255;
}

__global__ void bgra_to_nv12_kernel(const uint8_t* bgra, int bgra_pitch, uint8_t* y, int y_pitch, uint8_t* uv,
                                    int uv_pitch, int width, int height, ColorCoeffs color) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int row = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || row >= height) {
    return;
  }
  const uint8_t* pixel = bgra + row * bgra_pitch + x * 4;
  int luma = 0;
  int chroma_u = 0;
  int chroma_v = 0;
  rgb_to_yuv(color, pixel[2], pixel[1], pixel[0], luma, chroma_u, chroma_v);
  y[row * y_pitch + x] = static_cast<uint8_t>(luma);
  if ((x & 1) != 0 || (row & 1) != 0) {
    return;
  }
  int red = 0;
  int green = 0;
  int blue = 0;
  for (int dy = 0; dy < 2; ++dy) {
    for (int dx = 0; dx < 2; ++dx) {
      const uint8_t* sample = bgra + (row + dy) * bgra_pitch + (x + dx) * 4;
      blue += sample[0];
      green += sample[1];
      red += sample[2];
    }
  }
  red /= 4;
  green /= 4;
  blue /= 4;
  rgb_to_yuv(color, red, green, blue, luma, chroma_u, chroma_v);
  uint8_t* chroma = uv + (row / 2) * uv_pitch + x;
  chroma[0] = static_cast<uint8_t>(chroma_u);
  chroma[1] = static_cast<uint8_t>(chroma_v);
}

__global__ void nv12_scale_kernel(const uint8_t* src_y, int src_y_pitch, const uint8_t* src_uv, int src_uv_pitch,
                                  int src_w, int src_h, uint8_t* dst_y, int dst_y_pitch, uint8_t* dst_uv,
                                  int dst_uv_pitch, int dst_w, int dst_h) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int row = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= dst_w || row >= dst_h || src_w < 2 || src_h < 2) {
    return;
  }
  const int sx = x * src_w / dst_w;
  const int sy = row * src_h / dst_h;
  dst_y[row * dst_y_pitch + x] = src_y[sy * src_y_pitch + sx];
  if ((x & 1) != 0 || (row & 1) != 0) {
    return;
  }
  const int su = sx & ~1;
  const int sv = sy / 2;
  const uint8_t* src_pair = src_uv + sv * src_uv_pitch + su;
  uint8_t* dst_pair = dst_uv + (row / 2) * dst_uv_pitch + x;
  dst_pair[0] = src_pair[0];
  dst_pair[1] = src_pair[1];
}

void launch_nv12_to_bgra(const uint8_t* y, int y_pitch, const uint8_t* uv, int uv_pitch, uint8_t* bgra, int bgra_pitch,
                         int width, int height, ColorCoeffs color, CUstream stream) {
  const dim3 block(16, 16);
  const dim3 grid((width + 15) / 16, (height + 15) / 16);
  nv12_to_bgra_kernel<<<grid, block, 0, stream>>>(y, y_pitch, uv, uv_pitch, bgra, bgra_pitch, width, height, color);
}

void launch_bgra_to_nv12(const uint8_t* bgra, int bgra_pitch, uint8_t* y, int y_pitch, uint8_t* uv, int uv_pitch,
                         int width, int height, ColorCoeffs color, CUstream stream) {
  const dim3 block(16, 16);
  const dim3 grid((width + 15) / 16, (height + 15) / 16);
  bgra_to_nv12_kernel<<<grid, block, 0, stream>>>(bgra, bgra_pitch, y, y_pitch, uv, uv_pitch, width, height, color);
}

void launch_nv12_scale(const uint8_t* src_y, int src_y_pitch, const uint8_t* src_uv, int src_uv_pitch, int src_w,
                       int src_h, uint8_t* dst_y, int dst_y_pitch, uint8_t* dst_uv, int dst_uv_pitch, int dst_w,
                       int dst_h, CUstream stream) {
  const dim3 block(16, 16);
  const dim3 grid((dst_w + 15) / 16, (dst_h + 15) / 16);
  nv12_scale_kernel<<<grid, block, 0, stream>>>(src_y, src_y_pitch, src_uv, src_uv_pitch, src_w, src_h, dst_y,
                                                dst_y_pitch, dst_uv, dst_uv_pitch, dst_w, dst_h);
}

std::string cuda_text(CUresult result) {
  const char* text = nullptr;
  cuGetErrorString(result, &text);
  return text != nullptr ? text : "CUDA";
}

class ContextGuard {
 public:
  explicit ContextGuard(CUcontext context) : active_(cuCtxPushCurrent(context) == CUDA_SUCCESS) {}
  ~ContextGuard() {
    if (active_) {
      cuCtxPopCurrent(nullptr);
    }
  }
  bool active() const { return active_; }

 private:
  bool active_;
};

class RtxProcessor final : public FrameProcessor {
 public:
  ~RtxProcessor() override { close(); }

  bool open(AVBufferRef* hw_device, int src_w, int src_h, int scale, int quality) override {
    close();
    degraded_ = false;
    if (hw_device == nullptr || src_w < 2 || src_h < 2 || (scale != 2 && scale != 3)) {
      error_ = "некоректні параметри RTX VSR";
      return false;
    }
    if (src_w % 2 != 0 || src_h % 2 != 0) {
      error_ = "вхідний кадр має парні ширину й висоту";
      return false;
    }
    quality_ = clamp_quality(quality);
    src_w_ = src_w;
    src_h_ = src_h;
    dst_w_ = src_w * scale;
    dst_h_ = src_h * scale;
    auto* device = reinterpret_cast<AVHWDeviceContext*>(hw_device->data);
    auto* cuda = reinterpret_cast<AVCUDADeviceContext*>(device->hwctx);
    context_ = cuda->cuda_ctx;
    stream_ = cuda->stream;
    external_stream_ = stream_ != nullptr;
    ContextGuard guard(context_);
    if (!guard.active()) {
      error_ = "не вдалося увійти в CUDA-контекст FFmpeg";
      return false;
    }
    if (!external_stream_ && cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING) != CUDA_SUCCESS) {
      error_ = "не вдалося створити CUDA-потік";
      return false;
    }
    if (!allocate_surfaces() || !rtx_video_api_cuda_create(context_, stream_, 0, 0, 1)) {
      if (error_.empty()) {
        error_ = "RTX Video SDK не створив ефект Video Super Resolution";
      }
      close();
      return false;
    }
    opened_ = true;
    return true;
  }

  int output_width() const override { return dst_w_; }
  int output_height() const override { return dst_h_; }

  bool process(AVFrame* input, AVFrame* output, double& process_ms) override {
    if (!ready(input, output)) {
      return false;
    }
    ContextGuard guard(context_);
    if (!guard.active()) {
      error_ = "втрачено CUDA-контекст";
      return false;
    }
    const auto started = std::chrono::steady_clock::now();
    if (degraded_) {
      if (!scale_nv12(input, output)) {
        return false;
      }
      process_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
      return true;
    }
    const ColorCoeffs color = coeffs_for(choose_color(input->colorspace, input->color_range, input->width, input->height));
    launch_nv12_to_bgra(input->data[0], input->linesize[0], input->data[1], input->linesize[1], src_bgra_,
                        static_cast<int>(src_pitch_), src_w_, src_h_, color, stream_);
    if (!copy_device_to_array(src_bgra_, src_pitch_, src_array_, src_w_, src_h_)) {
      return false;
    }
    API_RECT source{0u, 0u, static_cast<uint32_t>(src_w_), static_cast<uint32_t>(src_h_)};
    API_RECT destination{0u, 0u, static_cast<uint32_t>(dst_w_), static_cast<uint32_t>(dst_h_)};
    API_VSR_Setting vsr{};
    vsr.QualityLevel = quality_;
    if (!rtx_video_api_cuda_evaluate(src_tex_, dst_surf_, source, destination, &vsr, nullptr)) {
      error_ = "rtx_video_api_cuda_evaluate повернув помилку";
      degraded_ = true;
      if (!scale_nv12(input, output)) {
        return false;
      }
      process_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
      return true;
    }
    if (!copy_array_to_device(dst_array_, dst_bgra_, dst_pitch_, dst_w_, dst_h_)) {
      return false;
    }
    launch_bgra_to_nv12(dst_bgra_, static_cast<int>(dst_pitch_), output->data[0], output->linesize[0], output->data[1],
                        output->linesize[1], dst_w_, dst_h_, color, stream_);
    if (cuStreamSynchronize(stream_) != CUDA_SUCCESS || cudaGetLastError() != cudaSuccess) {
      error_ = "CUDA-потік RTX VSR завершився з помилкою";
      return false;
    }
    stamp_color(input, output);
    process_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return true;
  }

  bool fallback(AVFrame* input, AVFrame* output) override {
    if (!ready(input, output)) {
      return false;
    }
    ContextGuard guard(context_);
    if (!guard.active()) {
      error_ = "втрачено CUDA-контекст";
      return false;
    }
    degraded_ = true;
    return scale_nv12(input, output);
  }

  bool degraded() const override { return degraded_; }

  void set_quality(int quality) override { quality_ = clamp_quality(quality); }
  int quality() const override { return quality_; }
  const std::string& last_error() const override { return error_; }

  void close() override {
    if (context_ != nullptr) {
      cuCtxPushCurrent(context_);
    }
    if (opened_) {
      rtx_video_api_cuda_shutdown();
      opened_ = false;
    }
    if (src_tex_ != 0) {
      cuTexObjectDestroy(src_tex_);
      src_tex_ = 0;
    }
    if (dst_surf_ != 0) {
      cuSurfObjectDestroy(dst_surf_);
      dst_surf_ = 0;
    }
    if (src_array_ != nullptr) {
      cuArrayDestroy(src_array_);
      src_array_ = nullptr;
    }
    if (dst_array_ != nullptr) {
      cuArrayDestroy(dst_array_);
      dst_array_ = nullptr;
    }
    if (src_bgra_ != nullptr) {
      cuMemFree(reinterpret_cast<CUdeviceptr>(src_bgra_));
      src_bgra_ = nullptr;
    }
    if (dst_bgra_ != nullptr) {
      cuMemFree(reinterpret_cast<CUdeviceptr>(dst_bgra_));
      dst_bgra_ = nullptr;
    }
    if (!external_stream_ && stream_ != nullptr) {
      cuStreamDestroy(stream_);
    }
    stream_ = nullptr;
    external_stream_ = false;
    degraded_ = false;
    if (context_ != nullptr) {
      cuCtxPopCurrent(nullptr);
      context_ = nullptr;
    }
  }

 private:
  bool ready(AVFrame* input, AVFrame* output) {
    if (!opened_ || input == nullptr || output == nullptr || input->hw_frames_ctx == nullptr) {
      error_ = "RTX VSR не готовий";
      return false;
    }
    auto* frames = reinterpret_cast<AVHWFramesContext*>(input->hw_frames_ctx->data);
    if (frames->sw_format != AV_PIX_FMT_NV12) {
      error_ = "RTX-шлях очікує NV12 у пам'яті GPU";
      return false;
    }
    if (input->width != src_w_ || input->height != src_h_ || output->width != dst_w_ || output->height != dst_h_) {
      error_ = "розмір кадру не збігається з відкритим сеансом RTX VSR";
      return false;
    }
    return true;
  }

  void stamp_color(const AVFrame* input, AVFrame* output) const {
    const ColorChoice choice = choose_color(input->colorspace, input->color_range, input->width, input->height);
    output->colorspace = static_cast<AVColorSpace>(av_space(choice));
    output->color_range = static_cast<AVColorRange>(av_range(choice));
    output->color_primaries = static_cast<AVColorPrimaries>(av_primaries(choice));
    output->color_trc = static_cast<AVColorTransferCharacteristic>(av_trc(choice));
  }

  bool scale_nv12(AVFrame* input, AVFrame* output) {
    launch_nv12_scale(input->data[0], input->linesize[0], input->data[1], input->linesize[1], src_w_, src_h_,
                      output->data[0], output->linesize[0], output->data[1], output->linesize[1], dst_w_, dst_h_,
                      stream_);
    if (cuStreamSynchronize(stream_) != CUDA_SUCCESS || cudaGetLastError() != cudaSuccess) {
      error_ = "резервний масштаб CUDA завершився з помилкою";
      return false;
    }
    stamp_color(input, output);
    return true;
  }

  static int clamp_quality(int quality) {
    if (quality < 1) {
      return 1;
    }
    if (quality > 4) {
      return 4;
    }
    return quality;
  }

  bool allocate_surfaces() {
    CUDA_ARRAY_DESCRIPTOR source{};
    source.Width = static_cast<size_t>(src_w_);
    source.Height = static_cast<size_t>(src_h_);
    source.Format = CU_AD_FORMAT_UNSIGNED_INT8;
    source.NumChannels = 4;
    if (cuArrayCreate(&src_array_, &source) != CUDA_SUCCESS) {
      error_ = "не вдалося виділити вхідний CUDA-масив";
      return false;
    }
    CUDA_RESOURCE_DESC source_resource{};
    source_resource.resType = CU_RESOURCE_TYPE_ARRAY;
    source_resource.res.array.hArray = src_array_;
    CUDA_TEXTURE_DESC texture{};
    texture.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
    texture.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
    texture.filterMode = CU_TR_FILTER_MODE_LINEAR;
    texture.flags = CU_TRSF_NORMALIZED_COORDINATES;
    if (cuTexObjectCreate(&src_tex_, &source_resource, &texture, nullptr) != CUDA_SUCCESS) {
      error_ = "не вдалося створити вхідну текстуру RTX";
      return false;
    }
    CUDA_ARRAY3D_DESCRIPTOR destination{};
    destination.Width = static_cast<size_t>(dst_w_);
    destination.Height = static_cast<size_t>(dst_h_);
    destination.Depth = 0;
    destination.Format = CU_AD_FORMAT_UNSIGNED_INT8;
    destination.NumChannels = 4;
    destination.Flags = CUDA_ARRAY3D_SURFACE_LDST;
    if (cuArray3DCreate(&dst_array_, &destination) != CUDA_SUCCESS) {
      error_ = "не вдалося виділити вихідний CUDA-масив із surface load/store";
      return false;
    }
    CUDA_RESOURCE_DESC destination_resource{};
    destination_resource.resType = CU_RESOURCE_TYPE_ARRAY;
    destination_resource.res.array.hArray = dst_array_;
    if (cuSurfObjectCreate(&dst_surf_, &destination_resource) != CUDA_SUCCESS) {
      error_ = "не вдалося створити вихідну поверхню RTX";
      return false;
    }
    if (!allocate_pitched(&src_bgra_, src_pitch_, src_w_, src_h_) ||
        !allocate_pitched(&dst_bgra_, dst_pitch_, dst_w_, dst_h_)) {
      error_ = "не вдалося виділити проміжний буфер BGRA";
      return false;
    }
    return true;
  }

  static bool allocate_pitched(uint8_t** pointer, size_t& pitch, int width, int height) {
    CUdeviceptr device = 0;
    const size_t row = static_cast<size_t>(width) * 4;
    if (cuMemAllocPitch(&device, &pitch, row, static_cast<size_t>(height), 16) != CUDA_SUCCESS) {
      return false;
    }
    *pointer = reinterpret_cast<uint8_t*>(device);
    return true;
  }

  bool copy_device_to_array(uint8_t* source, size_t pitch, CUarray array, int width, int height) {
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.srcDevice = reinterpret_cast<CUdeviceptr>(source);
    copy.srcPitch = pitch;
    copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copy.dstArray = array;
    copy.WidthInBytes = static_cast<size_t>(width) * 4;
    copy.Height = static_cast<size_t>(height);
    const CUresult result = cuMemcpy2DAsync(&copy, stream_);
    if (result != CUDA_SUCCESS) {
      error_ = cuda_text(result);
      return false;
    }
    return true;
  }

  bool copy_array_to_device(CUarray array, uint8_t* destination, size_t pitch, int width, int height) {
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    copy.srcArray = array;
    copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.dstDevice = reinterpret_cast<CUdeviceptr>(destination);
    copy.dstPitch = pitch;
    copy.WidthInBytes = static_cast<size_t>(width) * 4;
    copy.Height = static_cast<size_t>(height);
    const CUresult result = cuMemcpy2DAsync(&copy, stream_);
    if (result != CUDA_SUCCESS) {
      error_ = cuda_text(result);
      return false;
    }
    return true;
  }

  CUcontext context_ = nullptr;
  CUstream stream_ = nullptr;
  bool external_stream_ = false;
  bool opened_ = false;
  CUarray src_array_ = nullptr;
  CUarray dst_array_ = nullptr;
  CUtexObject src_tex_ = 0;
  CUsurfObject dst_surf_ = 0;
  uint8_t* src_bgra_ = nullptr;
  uint8_t* dst_bgra_ = nullptr;
  size_t src_pitch_ = 0;
  size_t dst_pitch_ = 0;
  int src_w_ = 0;
  int src_h_ = 0;
  int dst_w_ = 0;
  int dst_h_ = 0;
  int quality_ = 2;
  bool degraded_ = false;
  std::string error_;
};

}  // namespace

std::unique_ptr<FrameProcessor> create_rtx_processor() { return std::make_unique<RtxProcessor>(); }

std::string probe_rtx_sdk(int gpu_index) {
#if defined(RTX_SDK_COMPILE_CHECK)
  (void)gpu_index;
  return "перевірка компіляції CUDA не є NVIDIA RTX Video SDK";
#else
  if (cuInit(0) != CUDA_SUCCESS) {
    return "cuInit не вдався";
  }
  int count = 0;
  if (cuDeviceGetCount(&count) != CUDA_SUCCESS || gpu_index < 0 || gpu_index >= count) {
    return "обраний GPU недоступний для RTX Video SDK";
  }
  CUdevice device = 0;
  CUcontext context = nullptr;
  if (cuDeviceGet(&device, gpu_index) != CUDA_SUCCESS || cuDevicePrimaryCtxRetain(&context, device) != CUDA_SUCCESS ||
      cuCtxSetCurrent(context) != CUDA_SUCCESS) {
    return "не вдалося відкрити CUDA-контекст для перевірки RTX Video SDK";
  }
  const bool created = rtx_video_api_cuda_create(context, nullptr, 0, 0, 1);
  if (created) {
    rtx_video_api_cuda_shutdown();
  }
  cuDevicePrimaryCtxRelease(device);
  if (!created) {
    return "RTX Video SDK не підтримує цей GPU або не знайшов nvngx_vsr";
  }
  return {};
#endif
}

}  // namespace rtx

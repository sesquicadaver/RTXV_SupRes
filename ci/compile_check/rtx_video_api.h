#pragma once

// Заголовок лише для перевірки компіляції rtx_processor.cu.
// Це не NVIDIA RTX Video SDK: create() завжди відмовляє, ефект VSR не виконується.

#include <cuda.h>

#include <cstdint>

struct API_RECT {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t w = 0;
  uint32_t h = 0;
};

struct API_VSR_Setting {
  int QualityLevel = 0;
};

struct API_THDR_Setting;

bool rtx_video_api_cuda_create(CUcontext context, CUstream stream, int device, int thdr, int vsr);
void rtx_video_api_cuda_shutdown();
bool rtx_video_api_cuda_evaluate(CUtexObject source, CUsurfObject destination, API_RECT source_rect,
                                 API_RECT destination_rect, API_VSR_Setting* vsr, API_THDR_Setting* thdr);

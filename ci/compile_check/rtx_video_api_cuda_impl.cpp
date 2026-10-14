#include "rtx_video_api.h"

bool rtx_video_api_cuda_create(CUcontext, CUstream, int, int, int) { return false; }

void rtx_video_api_cuda_shutdown() {}

bool rtx_video_api_cuda_evaluate(CUtexObject, CUsurfObject, API_RECT, API_RECT, API_VSR_Setting*, API_THDR_Setting*) {
  return false;
}

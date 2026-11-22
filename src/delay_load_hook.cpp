// MSVC /DELAYLOAD:nvcuda.dll — без хука відсутній драйвер дає жорсткий abort до doctor.
#if defined(_WIN32) && defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <delayimp.h>

namespace {

FARPROC WINAPI rtx_delay_load_hook(unsigned notify, PDelayLoadInfo info) {
  if (notify == dliFailLoadLib || notify == dliFailGetProc) {
    // Повертаємо nullptr: викликач CUDA/NVDEC бачить помилку завантаження, процес живе для doctor.
    (void)info;
    return nullptr;
  }
  return nullptr;
}

}  // namespace

extern "C" const PfnDliHook __pfnDliFailureHook2 = rtx_delay_load_hook;

#endif

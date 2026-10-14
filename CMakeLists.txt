cmake_minimum_required(VERSION 3.16)
project(rtx-vsr-proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil)
endif()
if(NOT TARGET PkgConfig::FFMPEG)
  set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}" CACHE PATH "Корінь зібраного FFmpeg, якщо немає pkg-config")
  find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h HINTS "${FFMPEG_ROOT}/include")
  find_library(AVFORMAT_LIBRARY NAMES avformat HINTS "${FFMPEG_ROOT}/lib" "${FFMPEG_ROOT}/lib/x64")
  find_library(AVCODEC_LIBRARY NAMES avcodec HINTS "${FFMPEG_ROOT}/lib" "${FFMPEG_ROOT}/lib/x64")
  find_library(AVUTIL_LIBRARY NAMES avutil HINTS "${FFMPEG_ROOT}/lib" "${FFMPEG_ROOT}/lib/x64")
  if(NOT FFMPEG_INCLUDE_DIR OR NOT AVFORMAT_LIBRARY OR NOT AVCODEC_LIBRARY OR NOT AVUTIL_LIBRARY)
    message(FATAL_ERROR "FFmpeg не знайдено. Потрібен pkg-config із libavformat/libavcodec/libavutil або FFMPEG_ROOT.")
  endif()
  add_library(ffmpeg_manual INTERFACE)
  target_include_directories(ffmpeg_manual INTERFACE "${FFMPEG_INCLUDE_DIR}")
  target_link_libraries(ffmpeg_manual INTERFACE "${AVFORMAT_LIBRARY}" "${AVCODEC_LIBRARY}" "${AVUTIL_LIBRARY}")
endif()
if(TARGET PkgConfig::FFMPEG)
  set(RTX_FFMPEG PkgConfig::FFMPEG)
else()
  set(RTX_FFMPEG ffmpeg_manual)
endif()
find_package(Threads REQUIRED)

add_library(rtx_core STATIC
  src/types.cpp
  src/adaptive.cpp
  src/metrics.cpp
  src/timestamps.cpp
  src/color.cpp
  src/endpoint.cpp
  src/catalog.cpp
  src/gpu_probe.cpp
  src/processor_factory.cpp
  src/hls_server.cpp
  src/pipeline.cpp
  src/cli.cpp
)
target_include_directories(rtx_core PUBLIC src)
target_link_libraries(rtx_core PUBLIC ${RTX_FFMPEG} Threads::Threads)
if(MSVC)
  target_compile_options(rtx_core PRIVATE /W3)
else()
  target_compile_options(rtx_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
  target_compile_definitions(rtx_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(rtx_core PUBLIC ws2_32)
endif()

set(RTX_VIDEO_SDK_ROOT "$ENV{RTX_VIDEO_SDK_ROOT}" CACHE PATH "Корінь NVIDIA RTX Video SDK")
find_package(CUDAToolkit QUIET)
find_file(RTX_API_H rtx_video_api.h
  PATHS "${RTX_VIDEO_SDK_ROOT}"
  PATH_SUFFIXES samples/RTX_Video_API samples/rtx_video_api include
  NO_DEFAULT_PATH)
find_file(RTX_API_CPP rtx_video_api_cuda_impl.cpp
  PATHS "${RTX_VIDEO_SDK_ROOT}"
  PATH_SUFFIXES samples/RTX_Video_API samples/rtx_video_api src
  NO_DEFAULT_PATH)

if(CUDAToolkit_FOUND AND RTX_API_H AND RTX_API_CPP)
  enable_language(CUDA)
  set(CMAKE_CUDA_STANDARD 17)
  get_filename_component(RTX_API_DIR "${RTX_API_H}" DIRECTORY)
  target_sources(rtx_core PRIVATE src/gpu/rtx_processor.cu "${RTX_API_CPP}")
  target_include_directories(rtx_core PRIVATE "${RTX_API_DIR}")
  target_compile_definitions(rtx_core PUBLIC RTX_WITH_SDK=1)
  target_link_libraries(rtx_core PUBLIC CUDA::cuda_driver CUDA::cudart)
  find_library(NVSDK_NGX NAMES nvsdk_ngx_s nvsdk_ngx
    PATHS "${RTX_VIDEO_SDK_ROOT}"
    PATH_SUFFIXES lib lib/x64 lib/Windows/x64)
  if(NVSDK_NGX)
    target_link_libraries(rtx_core PUBLIC "${NVSDK_NGX}")
  else()
    message(STATUS "nvsdk_ngx не знайдено поруч із SDK; лінкування ефекту треба доповнити на цільовій машині")
  endif()
  message(STATUS "RTX Video SDK увімкнено: ${RTX_API_H}")
else()
  message(STATUS "RTX Video SDK не зібрано. Керування, каталог протоколів і відмова без GPU працюють; ефект VSR компілюється на цільовій машині з CUDA 12.x.")
endif()

add_executable(rtx-vsr-proxy src/main.cpp)
target_link_libraries(rtx-vsr-proxy PRIVATE rtx_core)

enable_testing()
foreach(test_name adaptive mailbox metrics endpoint catalog run_without_sdk timestamps color transport)
  add_executable(test_${test_name} tests/test_${test_name}.cpp)
  target_link_libraries(test_${test_name} PRIVATE rtx_core)
  add_test(NAME ${test_name} COMMAND test_${test_name})
endforeach()
set_tests_properties(run_without_sdk transport PROPERTIES TIMEOUT 20)

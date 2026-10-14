cmake_minimum_required(VERSION 3.16)
project(rtx-vsr-proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
find_package(Threads REQUIRED)

add_library(rtx_core STATIC
  src/types.cpp
  src/adaptive.cpp
  src/metrics.cpp
  src/endpoint.cpp
  src/catalog.cpp
  src/gpu_probe.cpp
  src/processor_factory.cpp
  src/hls_server.cpp
  src/pipeline.cpp
  src/cli.cpp
)
target_include_directories(rtx_core PUBLIC src)
target_link_libraries(rtx_core PUBLIC PkgConfig::FFMPEG Threads::Threads)
target_compile_options(rtx_core PRIVATE -Wall -Wextra -Wpedantic)

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
foreach(test_name adaptive mailbox metrics endpoint catalog run_without_sdk)
  add_executable(test_${test_name} tests/test_${test_name}.cpp)
  target_link_libraries(test_${test_name} PRIVATE rtx_core)
  add_test(NAME ${test_name} COMMAND test_${test_name})
endforeach()
set_tests_properties(run_without_sdk PROPERTIES TIMEOUT 20)

cmake_minimum_required(VERSION 3.16)
project(rtx-vsr-proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Цільова платформа продукту — лише Windows x64. Це не залежить від RTX_PRODUCT_BUILD.
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Збірка лише для Windows x64 (Windows 11). Інші платформи не підтримуються.")
endif()
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

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
  get_filename_component(AVFORMAT_LIB_DIR "${AVFORMAT_LIBRARY}" DIRECTORY)
  target_link_directories(ffmpeg_manual INTERFACE "${AVFORMAT_LIB_DIR}")
  target_link_libraries(ffmpeg_manual INTERFACE "${AVFORMAT_LIBRARY}" "${AVCODEC_LIBRARY}" "${AVUTIL_LIBRARY}")
  if(WIN32)
    target_link_libraries(ffmpeg_manual INTERFACE bcrypt ws2_32 secur32 ole32)
  endif()
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
  src/latency.cpp
  src/json_mini.cpp
  src/profile.cpp
  src/report.cpp
  src/install_check.cpp
)
target_include_directories(rtx_core PUBLIC src)
target_link_libraries(rtx_core PUBLIC ${RTX_FFMPEG} Threads::Threads)
if(MSVC)
  target_compile_options(rtx_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3>)
else()
  target_compile_options(rtx_core PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
endif()
if(WIN32)
  target_compile_definitions(rtx_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(rtx_core PUBLIC ws2_32)
endif()

set(RTX_VIDEO_SDK_ROOT "$ENV{RTX_VIDEO_SDK_ROOT}" CACHE PATH "Корінь NVIDIA RTX Video SDK")
option(RTX_CI_COMPILE_CHECK "Зібрати .cu проти заголовка перевірки компіляції, без ефекту NVIDIA" OFF)
option(RTX_PRODUCT_BUILD "Реліз Windows x64: Qt, справжній SDK, CUDA, без compile-check" OFF)
if(RTX_PRODUCT_BUILD)
  if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD збирається лише як Windows x64, MSVC і nvcc")
  endif()
  if(RTX_CI_COMPILE_CHECK)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD забороняє RTX_CI_COMPILE_CHECK")
  endif()
endif()
find_package(CUDAToolkit QUIET)
find_file(RTX_API_H rtx_video_api.h
  PATHS "${RTX_VIDEO_SDK_ROOT}"
  PATH_SUFFIXES samples/RTX_Video_API samples/rtx_video_api include
  NO_DEFAULT_PATH)
find_file(RTX_API_CPP rtx_video_api_cuda_impl.cpp
  PATHS "${RTX_VIDEO_SDK_ROOT}"
  PATH_SUFFIXES samples/RTX_Video_API samples/rtx_video_api src
  NO_DEFAULT_PATH)

set(RTX_EFFECT_HEADER "")
set(RTX_EFFECT_SOURCE "")
if(RTX_API_H AND RTX_API_CPP)
  set(RTX_EFFECT_HEADER "${RTX_API_H}")
  set(RTX_EFFECT_SOURCE "${RTX_API_CPP}")
endif()
set(RTX_COMPILE_CHECK_ONLY FALSE)
set(RTX_REAL_SDK FALSE)
if(RTX_CI_COMPILE_CHECK AND NOT (CUDAToolkit_FOUND AND RTX_API_H AND RTX_API_CPP))
  find_package(CUDAToolkit REQUIRED)
  set(RTX_EFFECT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/ci/compile_check/rtx_video_api.h")
  set(RTX_EFFECT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ci/compile_check/rtx_video_api_cuda_impl.cpp")
  set(RTX_COMPILE_CHECK_ONLY TRUE)
elseif(RTX_CI_COMPILE_CHECK)
  message(STATUS "RTX_CI_COMPILE_CHECK проігноровано: знайдено справжній RTX Video SDK")
endif()

if(CUDAToolkit_FOUND AND RTX_EFFECT_HEADER AND RTX_EFFECT_SOURCE)
  if(RTX_PRODUCT_BUILD AND (NOT DEFINED CMAKE_CUDA_ARCHITECTURES OR "${CMAKE_CUDA_ARCHITECTURES}" STREQUAL ""
     OR "${CMAKE_CUDA_ARCHITECTURES}" STREQUAL "native" OR "${CMAKE_CUDA_ARCHITECTURES}" STREQUAL "OFF"))
    message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує явний список CMAKE_CUDA_ARCHITECTURES")
  endif()
  if(RTX_COMPILE_CHECK_ONLY AND NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 75)
  endif()
  enable_language(CUDA)
  set(CMAKE_CUDA_STANDARD 17)
  get_filename_component(RTX_API_DIR "${RTX_EFFECT_HEADER}" DIRECTORY)
  target_sources(rtx_core PRIVATE src/gpu/rtx_processor.cu "${RTX_EFFECT_SOURCE}")
  target_include_directories(rtx_core PRIVATE "${RTX_API_DIR}")
  target_link_libraries(rtx_core PUBLIC CUDA::cuda_driver CUDA::cudart)
  if(RTX_COMPILE_CHECK_ONLY)
    target_compile_definitions(rtx_core PUBLIC RTX_WITH_SDK=1 RTX_SDK_COMPILE_CHECK=1)
    message(STATUS "Перевірка компіляції CUDA: ${RTX_EFFECT_HEADER}. Це не NVIDIA RTX Video SDK; doctor має завершуватися кодом 3.")
  else()
    set(RTX_REAL_SDK TRUE)
    target_compile_definitions(rtx_core PUBLIC RTX_WITH_SDK=1)
    find_library(NVSDK_NGX NAMES nvsdk_ngx_s nvsdk_ngx
      PATHS "${RTX_VIDEO_SDK_ROOT}"
      PATH_SUFFIXES lib lib/x64 lib/Windows/x64)
    if(NVSDK_NGX)
      target_link_libraries(rtx_core PUBLIC "${NVSDK_NGX}")
    else()
      message(STATUS "nvsdk_ngx не знайдено поруч із SDK; до лінкування треба додати nvsdk_ngx з цього SDK")
    endif()
    message(STATUS "RTX Video SDK увімкнено: ${RTX_EFFECT_HEADER}")
  endif()
else()
  message(STATUS "RTX Video SDK не зібрано. Керування, каталог протоколів і відмова без GPU працюють. Ефект VSR збирається з CUDA Toolkit 12.x і RTX_VIDEO_SDK_ROOT.")
endif()

add_executable(rtx-vsr-proxy src/main.cpp)
target_link_libraries(rtx-vsr-proxy PRIVATE rtx_core)

add_library(rtx_precheck STATIC src/precheck_rules.cpp src/json_mini.cpp)
target_include_directories(rtx_precheck PUBLIC src)
if(MSVC)
  set_target_properties(rtx_precheck PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(rtx_precheck PRIVATE /W3)
else()
  target_compile_options(rtx_precheck PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
  add_executable(rtx-vsr-precheck src/precheck_main.cpp)
  target_compile_definitions(rtx-vsr-precheck PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(rtx-vsr-precheck PRIVATE rtx_precheck setupapi advapi32)
  if(MSVC)
    set_target_properties(rtx-vsr-precheck PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()

enable_testing()
foreach(test_name adaptive mailbox metrics endpoint catalog run_without_sdk timestamps color transport rtp_mpegts profile install_check)
  add_executable(test_${test_name} tests/test_${test_name}.cpp)
  target_link_libraries(test_${test_name} PRIVATE rtx_core)
  add_test(NAME ${test_name} COMMAND test_${test_name})
endforeach()
add_executable(test_precheck tests/test_precheck.cpp)
target_link_libraries(test_precheck PRIVATE rtx_precheck)
if(MSVC)
  set_target_properties(test_precheck PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_test(NAME precheck COMMAND test_precheck)
set_tests_properties(run_without_sdk transport rtp_mpegts PROPERTIES TIMEOUT 20)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(TARGET Qt6::Widgets)
  set(CMAKE_AUTOMOC ON)
  add_executable(rtx-vsr-gui src/gui/main.cpp src/gui/window.cpp src/gui/window.hpp)
  target_link_libraries(rtx-vsr-gui PRIVATE rtx_core Qt6::Widgets)
  add_test(NAME gui_self_check COMMAND rtx-vsr-gui --self-check)
  set_tests_properties(gui_self_check PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
endif()

if(RTX_PRODUCT_BUILD)
  if(RTX_COMPILE_CHECK_ONLY OR NOT RTX_REAL_SDK)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує справжній RTX Video SDK і CUDA, без заголовка compile-check")
  endif()
  if(NOT NVSDK_NGX)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує бібліотеку nvsdk_ngx зі SDK")
  endif()
  if(NOT TARGET Qt6::Widgets)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує Qt 6 Widgets")
  endif()
  if(NOT TARGET rtx-vsr-precheck)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує rtx-vsr-precheck")
  endif()
  install(TARGETS rtx-vsr-proxy rtx-vsr-gui rtx-vsr-precheck RUNTIME DESTINATION .)
  file(WRITE "${CMAKE_BINARY_DIR}/rtx-product-build.txt"
    "product=1\ncompile_check=0\ncuda_architectures=${CMAKE_CUDA_ARCHITECTURES}\n")
  install(FILES
    "${CMAKE_BINARY_DIR}/rtx-product-build.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/components.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/NOTICES.txt"
    DESTINATION .)
endif()

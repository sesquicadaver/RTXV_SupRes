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
  src/audio_copy.cpp
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
  # Ціль rtx_core створена раніше за enable_language(CUDA), тож властивість задається явно.
  # Shared прибирає cudart_static (/MT) і лишає cudart64_12.dll разом із /MD.
  set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)
  enable_language(CUDA)
  set(CMAKE_CUDA_STANDARD 17)
  get_filename_component(RTX_API_DIR "${RTX_EFFECT_HEADER}" DIRECTORY)
  target_sources(rtx_core PRIVATE src/gpu/rtx_processor.cu "${RTX_EFFECT_SOURCE}")
  target_include_directories(rtx_core PRIVATE "${RTX_API_DIR}")
  set_target_properties(rtx_core PROPERTIES CUDA_RUNTIME_LIBRARY Shared)
  target_link_libraries(rtx_core PUBLIC CUDA::cuda_driver CUDA::cudart)
  if(MSVC)
    # nvcuda.dll ставить драйвер NVIDIA, не CUDA Toolkit. Без /DELAYLOAD завантажувач
    # завершує процес кодом 0xc0000135 ще до main, і doctor не встигає повернути 3.
    target_link_libraries(rtx_core PUBLIC delayimp)
    target_link_options(rtx_core INTERFACE
      "/DELAYLOAD:nvcuda.dll"
      "/NODEFAULTLIB:cudart_static.lib"
      "/IGNORE:4199")
  endif()
  if(RTX_COMPILE_CHECK_ONLY)
    target_compile_definitions(rtx_core PUBLIC RTX_WITH_SDK=1 RTX_SDK_COMPILE_CHECK=1)
    message(STATUS "Перевірка компіляції CUDA: ${RTX_EFFECT_HEADER}. Це не NVIDIA RTX Video SDK; doctor має завершуватися кодом 3.")
  else()
    set(RTX_REAL_SDK TRUE)
    target_compile_definitions(rtx_core PUBLIC RTX_WITH_SDK=1)
    find_path(NVSDK_NGX_INCLUDE_DIR nvsdk_ngx_defs.h
      PATHS "${RTX_VIDEO_SDK_ROOT}"
      PATH_SUFFIXES include
      NO_DEFAULT_PATH)
    if(NVSDK_NGX_INCLUDE_DIR)
      target_include_directories(rtx_core PRIVATE "${NVSDK_NGX_INCLUDE_DIR}")
    elseif(RTX_PRODUCT_BUILD)
      message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує ${RTX_VIDEO_SDK_ROOT}/include/nvsdk_ngx_defs.h")
    else()
      message(STATUS "nvsdk_ngx_defs.h не знайдено в ${RTX_VIDEO_SDK_ROOT}/include")
    endif()
    # Попередній кеш міг запам'ятати nvsdk_ngx_s.lib (/MT). Для /MD шукаємо d-варіант заново.
    unset(NVSDK_NGX CACHE)
    find_library(NVSDK_NGX_MD NAMES nvsdk_ngx_d
      PATHS "${RTX_VIDEO_SDK_ROOT}"
      PATH_SUFFIXES lib/Windows/x64 lib/x64 lib
      NO_DEFAULT_PATH)
    if(NVSDK_NGX_MD)
      set(NVSDK_NGX "${NVSDK_NGX_MD}" CACHE FILEPATH "nvsdk_ngx_d.lib для збірки /MD" FORCE)
      target_link_libraries(rtx_core PUBLIC "${NVSDK_NGX}")
      message(STATUS "NGX: ${NVSDK_NGX}")
      message(STATUS "CUDA runtime: shared (cudart64_12.dll), без cudart_static")
    else()
      set(NVSDK_NGX "")
      message(STATUS "nvsdk_ngx_d.lib не знайдено в ${RTX_VIDEO_SDK_ROOT}/lib/Windows/x64")
    endif()
    message(STATUS "RTX Video SDK увімкнено: ${RTX_EFFECT_HEADER}")
  endif()
else()
  message(STATUS "RTX Video SDK не зібрано. Керування, каталог протоколів і відмова без GPU працюють. Ефект VSR збирається з CUDA Toolkit 12.x і RTX_VIDEO_SDK_ROOT.")
endif()

# Прапорці лінкера статичної бібліотеки VS не завжди переносить у vcxproj споживача.
function(rtx_link_rtx_core target)
  target_link_libraries(${target} PRIVATE rtx_core)
  if(MSVC)
    target_link_libraries(${target} PRIVATE delayimp)
    target_link_options(${target} PRIVATE
      "/DELAYLOAD:nvcuda.dll"
      "/NODEFAULTLIB:cudart_static.lib"
      "/IGNORE:4199")
  endif()
endfunction()

# Єдиний застосунок: вікно і конвеєр в одному rtx-vsr.exe. Окремого проксі немає.

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
foreach(test_name adaptive mailbox metrics endpoint catalog run_without_sdk timestamps color transport rtp_mpegts profile install_check audio_copy)
  add_executable(test_${test_name} tests/test_${test_name}.cpp)
  rtx_link_rtx_core(test_${test_name})
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
  set(CMAKE_AUTORCC ON)
  enable_language(RC)
  set(RTX_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/RTXV_SupRes-icon/rtx-vsr.ico")
  if(NOT EXISTS "${RTX_APP_ICON}")
    message(FATAL_ERROR "Немає іконки ${RTX_APP_ICON}")
  endif()
  # Ярлик робочого столу і Провідник беруть першу іконку з ресурсів exe, не з вікна Qt.
  # У рядку .rc послідовність \r — це перенос, тож icon\rtx-vsr.ico rc.exe розрізає навпіл.
  file(TO_CMAKE_PATH "${RTX_APP_ICON}" RTX_APP_ICON_RC)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/rtx-vsr.rc" "1 ICON \"${RTX_APP_ICON_RC}\"\n")
  # WIN32: підсистема Windows, не консоль. Подвійний клік відкриває лише вікно.
  # Точка входу лишається main. Qt6::EntryPoint тягне WinMain і на Qt 6.8 ламає лінк.
  add_executable(rtx-vsr WIN32
    src/main.cpp
    src/gui/main.cpp
    src/gui/window.cpp
    src/gui/window.hpp
    src/gui/app.hpp
    src/gui/icons.qrc
    "${CMAKE_CURRENT_BINARY_DIR}/rtx-vsr.rc")
  rtx_link_rtx_core(rtx-vsr)
  if(MSVC)
    target_link_options(rtx-vsr PRIVATE "/ENTRY:mainCRTStartup")
  endif()
  target_compile_definitions(rtx-vsr PRIVATE RTX_WITH_GUI)
  target_link_libraries(rtx-vsr PRIVATE Qt6::Widgets)
  if(NOT FFMPEG_ROOT OR NOT EXISTS "${FFMPEG_ROOT}/bin")
    message(FATAL_ERROR "Потрібен FFMPEG_ROOT/bin, щоб rtx-vsr.exe запускався без окремого PATH")
  endif()
  set(RTX_CUDA_BIN "")
  if(CUDAToolkit_BIN_DIR)
    set(RTX_CUDA_BIN "${CUDAToolkit_BIN_DIR}")
  elseif(NOT "$ENV{CUDA_PATH}" STREQUAL "")
    set(RTX_CUDA_BIN "$ENV{CUDA_PATH}/bin")
  endif()
  get_target_property(RTX_QT_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
  if(NOT RTX_QT_DLL)
    get_target_property(RTX_QT_DLL Qt6::Core IMPORTED_LOCATION)
  endif()
  if(NOT RTX_QT_DLL)
    message(FATAL_ERROR "Не знайдено Qt6Core.dll для windeployqt")
  endif()
  get_filename_component(RTX_QT_BIN "${RTX_QT_DLL}" DIRECTORY)
  set(RTX_WINDEPLOYQT "${RTX_QT_BIN}/windeployqt.exe")
  if(NOT EXISTS "${RTX_WINDEPLOYQT}")
    message(FATAL_ERROR "Немає ${RTX_WINDEPLOYQT}")
  endif()
  add_custom_command(TARGET rtx-vsr POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DEXE=$<TARGET_FILE:rtx-vsr>
      -DDEST=$<TARGET_FILE_DIR:rtx-vsr>
      -DCONFIG=$<CONFIG>
      -DWINDEPLOYQT=${RTX_WINDEPLOYQT}
      -DFFMPEG_BIN=${FFMPEG_ROOT}/bin
      -DCUDA_BIN=${RTX_CUDA_BIN}
      -DSDK=${RTX_VIDEO_SDK_ROOT}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/packaging/deploy_runtime.cmake
    COMMENT "Qt, FFmpeg і cudart поруч із rtx-vsr.exe")
  add_test(NAME gui_self_check COMMAND rtx-vsr --self-check)
  set_tests_properties(gui_self_check PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
endif()

# Крапка з комою в PATH — роздільник списку CMake. Без \; ctest лишає тестам лише перший каталог.
function(rtx_apply_test_runtime_path)
  if(NOT WIN32)
    return()
  endif()
  set(dirs "")
  if(FFMPEG_ROOT)
    list(APPEND dirs "${FFMPEG_ROOT}/bin")
  endif()
  if(CUDAToolkit_BIN_DIR)
    list(APPEND dirs "${CUDAToolkit_BIN_DIR}")
  elseif(NOT "$ENV{CUDA_PATH}" STREQUAL "")
    list(APPEND dirs "$ENV{CUDA_PATH}/bin")
  endif()
  set(qt_plugins "")
  if(TARGET Qt6::Core)
    get_target_property(qt_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT qt_dll)
      get_target_property(qt_dll Qt6::Core IMPORTED_LOCATION)
    endif()
    if(qt_dll)
      get_filename_component(qt_bin "${qt_dll}" DIRECTORY)
      list(APPEND dirs "${qt_bin}")
      get_filename_component(qt_root "${qt_bin}" DIRECTORY)
      set(qt_plugins "${qt_root}/plugins")
    endif()
  endif()
  set(entries ${dirs})
  list(APPEND entries $ENV{PATH})
  string(JOIN "\\;" escaped ${entries})
  get_property(tests DIRECTORY PROPERTY TESTS)
  foreach(test_name IN LISTS tests)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT "PATH=${escaped}")
  endforeach()
  if(qt_plugins AND TEST gui_self_check)
    set_property(TEST gui_self_check APPEND PROPERTY ENVIRONMENT "QT_PLUGIN_PATH=${qt_plugins}")
  endif()
  message(STATUS "ctest PATH: ${dirs}")
endfunction()
rtx_apply_test_runtime_path()

if(RTX_PRODUCT_BUILD)
  if(RTX_COMPILE_CHECK_ONLY OR NOT RTX_REAL_SDK)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує справжній RTX Video SDK і CUDA, без заголовка compile-check")
  endif()
  if(NOT NVSDK_NGX)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує бібліотеку nvsdk_ngx зі SDK")
  endif()
  if(NOT TARGET Qt6::Widgets OR NOT TARGET rtx-vsr)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує один застосунок rtx-vsr з Qt 6 Widgets")
  endif()
  if(NOT TARGET rtx-vsr-precheck)
    message(FATAL_ERROR "RTX_PRODUCT_BUILD потребує rtx-vsr-precheck")
  endif()
  install(TARGETS rtx-vsr rtx-vsr-precheck RUNTIME DESTINATION .)
  file(WRITE "${CMAKE_BINARY_DIR}/rtx-product-build.txt"
    "product=1\ncompile_check=0\ncuda_architectures=${CMAKE_CUDA_ARCHITECTURES}\n")
  install(FILES
    "${CMAKE_BINARY_DIR}/rtx-product-build.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/components.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/NOTICES.txt"
    DESTINATION .)
endif()

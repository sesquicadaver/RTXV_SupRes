# Матриця відповідності

| Вимога | Модуль | Перевірка |
|---|---|---|
| Лише фактично доступні протоколи й ролі | `src/catalog.cpp` | `tests/test_catalog.cpp`, `capabilities` |
| Окремі порти клієнта, слухача, сервера, RTP/RTCP | `src/endpoint.cpp` | `tests/test_endpoint.cpp` |
| HTTP `listen` і RTMP `rtmp_listen` як AVOption, не query URL | `src/endpoint.cpp`, `src/pipeline.cpp` | `tests/test_endpoint.cpp`, `tests/test_transport.cpp` |
| MPEG-TS over RTP: муксер `rtp_mpegts`, демуксер `rtp`, пакет PT 33 | `src/catalog.cpp`, `src/pipeline.cpp` | `tests/test_catalog.cpp`, `tests/test_rtp_mpegts.cpp` |
| Вхід і вихід незалежні, протоколи можуть відрізнятися | `src/cli.cpp`, `src/pipeline.cpp` | `tests/test_run_without_sdk.cpp` |
| NVDEC → RTX Video SDK → NVENC, кадр лишається на GPU | `src/pipeline.cpp`, `src/gpu/rtx_processor.cu` | збірка з `RTX_VIDEO_SDK_ROOT` (`include/nvsdk_ngx_defs.h`, `nvsdk_ngx_d.lib`) і CUDA 12.x, спільний `cudart`; виконання на GPU NVIDIA |
| Вихідний CUDA-масив із `CUDA_ARRAY3D_SURFACE_LDST` | `src/gpu/rtx_processor.cu` | компіляція `.cu` з CUDA Toolkit; виконання на GPU NVIDIA |
| Перерахунок PTS між time_base, пропуски й розриви | `src/timestamps.cpp` | `tests/test_timestamps.cpp` |
| Перевірка GPU можливостями SDK, не зашитим списком | `src/gpu_probe.cpp`, `probe_rtx_sdk` | `doctor` створює ефект лише коли SDK зібрано; інакше код 3. `nvcuda.dll` відкладено, тож без драйвера немає `0xc0000135` |
| Адаптивна якість за повним часом кадру | `src/adaptive.cpp`, `src/pipeline.cpp` | `tests/test_adaptive.cpp` |
| Масштаб 3 → 2 до відкриття виходу, якщо перший кадр не вкладається в бюджет | `src/pipeline.cpp` | виконання на GPU; без GPU шлях недоступний |
| Перевантаження без росту черги | `src/mailbox.hpp` | `tests/test_mailbox.cpp` |
| Відмова VSR не обриває потік: резервний масштаб того ж розміру | `src/gpu/rtx_processor.cu`, `src/pipeline.cpp` | виконання на GPU; `vsr_state=bypass` |
| Колір з метаданих кадру | `src/color.cpp` | `tests/test_color.cpp` |
| NVENC без B-кадрів і lookahead | `configure_nvenc` у `src/pipeline.cpp` | параметри кодера; виконання на GPU NVIDIA |
| Аудіо копіюється, якщо муксер виходу приймає кодек | `src/audio_copy.cpp`, `src/pipeline.cpp` | `tests/test_audio_copy.cpp` |
| Метрики FPS, пропуски, повний час, стан VSR | `src/metrics.cpp`, цикл у `src/pipeline.cpp` | `tests/test_metrics.cpp` |
| Відмова без SDK замість підміни ефекту | `src/processor_factory.cpp` | `tests/test_run_without_sdk.cpp` |
| Збірка Win32 без POSIX-сокетів | `src/platform.hpp`, `src/hls_server.cpp` | job `windows` у `.github/workflows/ci.yml` |
| Перевірки тестів не залежать від `NDEBUG` | `tests/check.hpp` | `ctest` у Release |
| `AVFrame::duration` лише з libavutil 58 | `src/pipeline.cpp` | компіляція на FFmpeg 4/5 (`pkt_duration`) і 6 |
| Прапори C++ не потрапляють у nvcc; перевірка компіляції `.cu` не видає себе за SDK | `CMakeLists.txt`, `ci/compile_check/` | job `windows-cuda` (`RTX_CI_COMPILE_CHECK`). Окремого job `cuda-compile` немає |
| CUDA-контекст процесора закривається до звільнення `hw_device` | `src/pipeline.cpp` | порядок у `~Session()` |
| Профіль JSON схеми 1, атомарний запис; відео з паролем відхиляється | `src/profile.cpp`, `src/pipeline.cpp` | `tests/test_profile.cpp` |
| `doctor --json`; події стану й метрики на stdout; `stop` на stdin | `src/report.cpp`, `src/cli.cpp`, `src/pipeline.cpp` | `tests/test_profile.cpp`; `doctor --json` без SDK завершується кодом 3 |
| Обраний GPU є в списку за індексом; перевірка списку окремо від створення SDK | `src/report.cpp`, `src/processor_factory.cpp` | `tests/test_profile.cpp` (`gpu_list_blocker`) |
| Лише адреса і порт: без імені користувача, пароля і протоколів з обов'язковою авторизацією | `src/profile.cpp`, `src/catalog.cpp`, `src/cli.cpp`, `src/gui/window.cpp` | `tests/test_profile.cpp`, `tests/test_catalog.cpp` |
| Елементарний RTP-вхід шукає режим SDP, MPEG-TS — демуксер `rtp` | `src/endpoint.cpp` | `tests/test_profile.cpp` (`rtp_container`) |
| Валідний бенчмарк: код 0, `ok` і хоча б один кадр | `src/latency.hpp`, `src/pipeline.cpp`, `src/gui/window.cpp` | `tests/test_profile.cpp` (`benchmark_run_valid`) |
| Час черги без паузи `--pace`; нуль зразків не входить у бюджет | `src/latency.hpp`, `src/pipeline.cpp` | `tests/test_profile.cpp` |
| HLS для споживача — HTTP плейлиста, не `hls://` | `src/endpoint.cpp` | `tests/test_profile.cpp` |
| Час обробки проксі p95/p99 за кадрами, не згладжене середнє | `src/latency.cpp`, `src/pipeline.cpp`, `src/metrics.cpp` | `tests/test_profile.cpp`, `tests/test_metrics.cpp` |
| Один застосунок `rtx-vsr.exe`: вікно і конвеєр, без окремого проксі | `src/main.cpp`, `src/gui/` | `gui_self_check` (`rtx-vsr --self-check`, `QT_QPA_PLATFORM=offscreen`). Інсталятор — `packaging/output/rtx-vsr-setup.exe` |
| Іконка робочого столу, ярлика і вікна — `rtx-vsr.ico` (16…256) | `RTXV_SupRes-icon/rtx-vsr.ico`, ресурс exe, `src/gui/icons.qrc`, `packaging/rtx-vsr.iss` | `gui_self_check` перевіряє кадри 16 і 256 у вікні; Провідник читає іконку з `rtx-vsr.exe` |
| Інсталятор бачить відсутні Qt, FFmpeg, nvngx і VC++ і ставить їх лише після підтвердження | `packaging/rtx-vsr.iss` | діалог зі списком `Qt6Widgets.dll`, `avformat-61.dll`, `avcodec-61.dll`, `avutil-59.dll`; драйвер NVIDIA не завантажується |
| Реліз Windows 11 x64: один інсталятор ставить `rtx-vsr.exe` і DLL з пакета; драйвер NVIDIA не входить | `packaging/rtx-vsr.iss`, `packaging/build_setup.ps1`, `src/precheck_rules.cpp` | `tests/test_precheck.cpp`; `packaging\output\rtx-vsr-setup.exe` |
| Пакети збірки ставляться лише коли їх ще немає; драйвер NVIDIA і PowerShell не чіпаються | `packaging/setup_build.ps1` | запуск на Windows 11; `-Plan` друкує відсутні пакети і нічого не ставить |
| Збірка: VS 2022, `-T cuda=`, архітектури в лапках, `RTX_PRODUCT_BUILD=ON`, потім `build_setup.ps1` | `README.md`, `CMakeLists.txt`, `packaging/build_setup.ps1` | `ctest -C Release`; файл `packaging\output\rtx-vsr-setup.exe` |

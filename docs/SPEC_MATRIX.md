# Матриця відповідності

| Вимога | Модуль | Перевірка |
|---|---|---|
| Лише фактично доступні протоколи й ролі | `src/catalog.cpp` | `tests/test_catalog.cpp`, `capabilities` |
| Окремі порти клієнта, слухача, сервера, RTP/RTCP | `src/endpoint.cpp` | `tests/test_endpoint.cpp` |
| HTTP `listen` і RTMP `rtmp_listen` як AVOption, не query URL | `src/endpoint.cpp`, `src/pipeline.cpp` | `tests/test_endpoint.cpp`, `tests/test_transport.cpp` |
| MPEG-TS over RTP: муксер `rtp_mpegts`, демуксер `rtp`, пакет PT 33 | `src/catalog.cpp`, `src/pipeline.cpp` | `tests/test_catalog.cpp`, `tests/test_rtp_mpegts.cpp` |
| Вхід і вихід незалежні, протоколи можуть відрізнятися | `src/cli.cpp`, `src/pipeline.cpp` | `tests/test_run_without_sdk.cpp` |
| NVDEC → RTX Video SDK → NVENC, кадр лишається на GPU | `src/pipeline.cpp`, `src/gpu/rtx_processor.cu` | збірка з `RTX_VIDEO_SDK_ROOT` на цільовій машині |
| Вихідний CUDA-масив із `CUDA_ARRAY3D_SURFACE_LDST` | `src/gpu/rtx_processor.cu` | компіляція `.cu` на машині з CUDA; виконання на GPU цільової машини |
| Перерахунок PTS між time_base, пропуски й розриви | `src/timestamps.cpp` | `tests/test_timestamps.cpp` |
| Перевірка GPU можливостями SDK, не зашитим списком | `src/gpu_probe.cpp`, `probe_rtx_sdk` | `doctor` створює ефект лише коли SDK зібрано; інакше код 3 |
| Адаптивна якість за повним часом кадру | `src/adaptive.cpp`, `src/pipeline.cpp` | `tests/test_adaptive.cpp` |
| Масштаб 3 → 2 до відкриття виходу, якщо перший кадр не вкладається в бюджет | `src/pipeline.cpp` | виконання на GPU; без GPU шлях недоступний |
| Перевантаження без росту черги | `src/mailbox.hpp` | `tests/test_mailbox.cpp` |
| Відмова VSR не обриває потік: резервний масштаб того ж розміру | `src/gpu/rtx_processor.cu`, `src/pipeline.cpp` | виконання на GPU; `vsr_state=bypass` |
| Колір з метаданих кадру | `src/color.cpp` | `tests/test_color.cpp` |
| NVENC без B-кадрів і lookahead | `configure_nvenc` у `src/pipeline.cpp` | перегляд параметрів; виконання на GPU цільової машини |
| Метрики FPS, пропуски, повний час, стан VSR | `src/metrics.cpp`, цикл у `src/pipeline.cpp` | `tests/test_metrics.cpp` |
| Відмова без SDK замість підміни ефекту | `src/processor_factory.cpp` | `tests/test_run_without_sdk.cpp` |
| Збірка Win32 без POSIX-сокетів | `src/platform.hpp`, `src/hls_server.cpp` | job `windows` у `.github/workflows/ci.yml` |
| Перевірки тестів не залежать від `NDEBUG` | `tests/check.hpp` | `ctest` у Release |
| `AVFrame::duration` лише з libavutil 58 | `src/pipeline.cpp` | компіляція на FFmpeg 4/5 (`pkt_duration`) і 6 |
| Прапори C++ не потрапляють у nvcc; перевірка компіляції `.cu` не видає себе за SDK | `CMakeLists.txt`, `ci/compile_check/` | job `cuda-compile` (doctor код 3), job `windows-cuda` (лінк без GPU) |
| CUDA-контекст процесора закривається до звільнення `hw_device` | `src/pipeline.cpp` | порядок у `~Session()` |

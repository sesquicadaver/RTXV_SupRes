# Матриця відповідності

| Вимога | Модуль | Перевірка |
|---|---|---|
| Лише фактично доступні протоколи й ролі | `src/catalog.cpp` | `tests/test_catalog.cpp`, `capabilities` |
| Окремі порти клієнта, слухача, сервера, RTP/RTCP | `src/endpoint.cpp` | `tests/test_endpoint.cpp` |
| Вхід і вихід незалежні, протоколи можуть відрізнятися | `src/cli.cpp`, `src/pipeline.cpp` | `tests/test_run_without_sdk.cpp` |
| NVDEC → RTX Video SDK → NVENC, кадр лишається на GPU | `src/pipeline.cpp`, `src/gpu/rtx_processor.cu` | збірка з `RTX_VIDEO_SDK_ROOT` на цільовій машині |
| Перевірка GPU можливостями SDK, не зашитим списком | `src/gpu_probe.cpp`, `probe_rtx_sdk` | `doctor`; на цій машині SDK немає, код виходу 3 |
| Адаптивна якість, пріоритет затримки | `src/adaptive.cpp` | `tests/test_adaptive.cpp` |
| Перевантаження без росту черги | `src/mailbox.hpp` | `tests/test_mailbox.cpp` |
| NVENC без B-кадрів і lookahead | `configure_nvenc` у `src/pipeline.cpp` | перегляд параметрів; виконання на GPU цільової машини |
| Метрики FPS, пропуски, час, стан VSR | `src/metrics.cpp`, цикл у `src/pipeline.cpp` | `tests/test_metrics.cpp` |
| Відмова без SDK замість підміни ефекту | `src/processor_factory.cpp` | `tests/test_run_without_sdk.cpp` |

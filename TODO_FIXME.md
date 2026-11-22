# TODO / FIXME

- Немає 10-бітного шляху і TrueHDR. Вхід, який NVDEC віддає не як NV12, зупиняє сеанс.
- Після відкриття кодера роздільність не змінюється. Масштаб 3→2 до `avio_open2` за пробою кадрів і лімітом NVENC; бенчмарк — окремий сеанс на комбінацію.
- `--self-check` вікна перевіряє наявність віджетів `start`, `gpu`, `profile`, `benchmark`. Збереження профілю, старт і зупинка сеансу цим режимом не покриті.
- CUDA 13 несумісна з RTX Video SDK 1.1: ламає `NVSDK_NGX_CUDA_Init`. Потрібен CUDA Toolkit 12.x. `setup_build.ps1` не завантажує сам RTX Video SDK: його беруть з кабінету NVIDIA.
- Job `windows-cuda` ставить CUDA через `Jimver/cuda-toolkit@v0.2.36` (`12.6.3`). Версії `0.2.25`…`0.2.28` на Windows падали на `Path Validation Error` у кеші ще до `nvcc`.
- Hosted GitHub Actions зараз не стартують (`account is locked due to a billing issue`). Pin CUDA і product smoke перевіряються після розблокування білінгу або на VM.
- `packaging/ffmpeg-lgpl.sha256` опційний: після першої довіреної загрузки FFmpeg збережіть туди SHA-256, інакше `setup_build.ps1` лише попереджає.
- Windows CI не ставить Qt; ISCC/GUI smoke — на VM. HIGH/MEDIUM і ключові LOW (json `\u`, bench scan O(N), async close, metrics flush, delay-load hook) — у коді. God-object `window.cpp` і dual deploy/stage списки DLL лишаються WATCH.
- Hosted CI ігноруємо за домовленістю; product smoke — локально на Windows 11 після `build_setup.ps1`.
- SRT caller не має окремого локального порту, якщо його немає у зібраному FFmpeg. Rendezvous використовує один порт у URL.
- Резервний шлях без VSR — найближчий сусід по NV12, не якісний масштабувальник. Колір при цьому лишається матрицею джерела.
- Візуальне порівняння бенчмарку виконується у програмі-споживачі. Час у таблиці — обробка проксі, без камери, мережі й показу.

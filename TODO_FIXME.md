# TODO / FIXME

- Немає 10-бітного шляху і TrueHDR. Вхід, який NVDEC віддає не як NV12, зупиняє сеанс.
- Після відкриття кодера роздільність не змінюється. Масштаб може впасти з 3 до 2 лише за виміром першого кадру, до `avio_open2`. Бенчмарк запускає окремий сеанс на кожну комбінацію.
- `--self-check` вікна перевіряє наявність віджетів `start`, `gpu`, `profile`, `benchmark`. Збереження профілю, старт і зупинка сеансу цим режимом не покриті.
- CUDA 13 несумісна з RTX Video SDK 1.1: ламає `NVSDK_NGX_CUDA_Init`. Потрібен CUDA Toolkit 12.x. `setup_build.ps1` не завантажує сам RTX Video SDK: його беруть з кабінету NVIDIA.
- Job `windows-cuda` ставить CUDA через `Jimver/cuda-toolkit@v0.2.36` (`12.6.3`). Версії `0.2.25`…`0.2.28` на Windows падали на `Path Validation Error` у кеші ще до `nvcc`.
- Hosted GitHub Actions зараз не стартують (`account is locked due to a billing issue`). Pin CUDA і product smoke перевіряються після розблокування білінгу або на VM.
- `packaging/ffmpeg-lgpl.sha256` опційний: після першої довіреної загрузки FFmpeg збережіть туди SHA-256, інакше `setup_build.ps1` лише попереджає.
- Windows CI не ставить Qt, тож `rtx-vsr.exe` там не збирається. У workflow немає ISCC і перевірок встановлення, оновлення, відновлення й видалення. Вікно й GPU-конвеєр на Windows 11 цим репозиторієм не підтверджені. Кириличні шляхи (UTF-8 → wide FS), Stop UX (`stopping_`), HLS (stream+concurrency), Inno backup restore — у коді; ISCC/GUI smoke на VM після білінгу.
- Hosted CI і product smoke все ще блоковані білінгом GitHub Actions; відкат інсталятора перевірити ISCC на Windows 11 після `build_setup.ps1`.
- SRT caller не має окремого локального порту, якщо його немає у зібраному FFmpeg. Rendezvous використовує один порт у URL.
- Резервний шлях без VSR — найближчий сусід по NV12, не якісний масштабувальник. Колір при цьому лишається матрицею джерела.
- Візуальне порівняння бенчмарку виконується у програмі-споживачі. Час у таблиці — обробка проксі, без камери, мережі й показу.

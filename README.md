# RTX VSR

Ліцензія: freeware from sesquicadaver. https://github.com/sesquicadaver/RTXV_SupRes

Один застосунок Windows 11 x64: **FFmpeg/NVDEC → RTX Video SDK → NVENC → транспорт**. Вікно і конвеєр — це `rtx-vsr.exe`. Окремого проксі немає.

Без аргументів відкривається вікно: профіль, джерело, вихід, GPU, згорнутий бенчмарк. Команди `doctor`, `run`, `capabilities`, `install-check` ідуть у той самий файл. Користувачу ставиться один інсталятор `packaging\output\rtx-vsr-setup.exe`.

Вхід і вихід обираються незалежно. Програма показує лише ті протоколи й ролі, які є в поточній збірці FFmpeg. `doctor` підтверджує ефект VSR лише після створення ефекту SDK на GPU NVIDIA. Без GPU або без `nvngx_vsr.dll` він завершується кодом 3.

Відповідність вимог і тестів: [docs/SPEC_MATRIX.md](docs/SPEC_MATRIX.md). Відкриті обмеження: [TODO_FIXME.md](TODO_FIXME.md).

## Збірка

Лише Windows 11 x64, PowerShell, генератор `Visual Studio 17 2022`. Каталог `build` створюється цією командою. Чужий `build` (з іншого комп'ютера чи іншої платформи) перед цим видаляється.

RTX Video SDK скрипт не завантажує. Його розпаковують з кабінету NVIDIA так, щоб у корені були `include\nvsdk_ngx_defs.h`, `samples\RTX_Video_API\` і десь у дереві `nvngx_vsr.dll`. Корінь задається як `RTX_VIDEO_SDK_ROOT`.

```powershell
Set-Location C:\RTXV_SupRes
powershell -NoProfile -ExecutionPolicy Bypass -File .\packaging\setup_build.ps1
```

Повторний запуск нічого не перевстановлює, якщо інструменти вже є. Код 0 — інструменти і заголовок SDK знайдені. Код 2 — інструменти є, а `RTX_VIDEO_SDK_ROOT` ще не вказує на SDK. Драйвер NVIDIA цей скрипт не ставить.

Далі в тому самому вікні. Шляхи FFmpeg, CUDA і Qt мають збігатися з тим, що щойно знайшов або поставив `setup_build.ps1`. Після успішного проходу їх можна взяти з `%LOCALAPPDATA%\rtx-vsr-build\build-env.ps1`.

```powershell
$cuda = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6"
$env:CUDA_PATH = $cuda
$env:CUDA_PATH_V12_6 = $cuda
$env:CudaToolkitDir = $cuda
$env:RTX_VIDEO_SDK_ROOT = "C:\RTX"
$env:FFMPEG_ROOT = "$env:LOCALAPPDATA\rtx-vsr-build\ffmpeg\ffmpeg-n7.1.5-12-g1fdbca85aa-win64-lgpl-shared-7.1"
$env:CMAKE_PREFIX_PATH = "$env:LOCALAPPDATA\rtx-vsr-build\Qt\6.8.3\msvc2022_64"
$env:Path = "C:\Program Files\CMake\bin;$cuda\bin;$env:FFMPEG_ROOT\bin;$env:CMAKE_PREFIX_PATH\bin;" + $env:Path
if (Test-Path .\build) { Remove-Item -Recurse -Force .\build }
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T "cuda=$cuda" "-DCMAKE_CUDA_ARCHITECTURES=75;86;89" -DRTX_PRODUCT_BUILD=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File .\packaging\build_setup.ps1
```

`-T "cuda=..."` обов'язковий: без нього MSBuild шукає каталог CUDA як порожній рядок. Список архітектур у PowerShell береться в лапки, інакше `;` розриває команду. `CMAKE_CUDA_RUNTIME_LIBRARY` у `CMakeLists.txt` стоїть `Shared`: лінкер бере `cudart64_12.dll`, не `cudart_static.lib`. `nvcuda.dll` підвантажується відкладено і в пакет не входить — її дає вже встановлений драйвер NVIDIA.

`RTX_PRODUCT_BUILD=ON` вимагає MSVC, Qt 6 Widgets, справжній SDK, `nvsdk_ngx_d.lib` і явний список архітектур. `RTX_CI_COMPILE_CHECK` разом із ним заборонений.

Результат:

| Файл | Що це |
|---|---|
| `build\Release\rtx-vsr.exe` | програма до пакування; іконка ярлика — `RTXV_SupRes-icon\rtx-vsr.ico` у ресурсі exe |
| `packaging\output\rtx-vsr-setup.exe` | інсталятор для користувача; та сама іконка на файлі setup і на ярликах |

Інсталятор копіює `rtx-vsr.exe`, DLL FFmpeg, `cudart64_*.dll`, файли Qt від `windeployqt` і `nvngx_vsr.dll` з дерева SDK. `vc_redist.x64.exe` береться зі встановленої Visual Studio 2022 і ставиться лише якщо системна збірка x64 відсутня або старіша. Драйвер NVIDIA в пакет не входить. `rtx-vsr-precheck.exe` ярлика не має: це внутрішній крок інсталятора. `ISCC.exe` шукається в `%LOCALAPPDATA%\Programs\Inno Setup 6` і в Program Files.

Перевірка вікна без дисплея: `build\Release\rtx-vsr.exe --self-check` при `QT_QPA_PLATFORM=offscreen`. `ctest` сам додає в `PATH` каталоги FFmpeg, CUDA і Qt.

## Поведінка

Масштаб 2 або 3. Якість 1..4, за замовчуванням `auto` зі стартом на 2. Адаптація дивиться на повний час кадру. Якщо перший кадр на масштабі 3 не вкладається в інтервал, до відкриття виходу масштаб падає до 2. Далі роздільність не змінюється.

Між декодером і VSR один слот. Якщо `rtx_video_api_cuda_evaluate` падає, потік не обривається: кадр масштабується на GPU до вже відкритого розміру, `vsr_state` стає `bypass`.

NVENC без B-кадрів, `rc-lookahead=0`, `delay=0`, `zerolatency`. Колір береться з метаданих кадру. Аудіо копіюється без перекодування, якщо муксер виходу приймає кодек. Елементарний RTP і сирий H.264/H.265 аудіо не несуть.

Події стану й метрики — рядки JSON на stdout. Журнал FFmpeg — на stderr. Зупинка: рядок `stop` на stdin. `doctor --json` друкує `ok`, `effect`, `blockers`, `gpus` з `uuid` і `vram_bytes`. Профіль зберігає UUID GPU. `--gpu N` під час запуску скидає UUID і бере індекс.

Сеанс приймає лише адресу і порт. Ім'я користувача, пароль і параметри `passphrase`, `password`, `secret`, `token` відхиляються. Протоколи з обов'язковим обліковим записом (`ftp`, `sftp`, `smb`, `icecast`) у каталог не входять.

| Роль | Порт |
|---|---|
| `client` | порт віддаленого джерела або отримувача |
| `listener` | локальна адреса й порт прийому |
| `server` | локальна адреса й порт, де слухає вихід |
| `rendezvous` | режим SRT, якщо він є в FFmpeg |

`rtp` без payload — MPEG-TS over RTP (муксер `rtp_mpegts`, демуксер `rtp`). `--output-payload h264|hevc|es` пише SDP. HTTP-сервер — `http` + `server`. Адреса HLS для споживача — `http://хост:порт/index.m3u8`.

```bat
rtx-vsr.exe run ^
  --input-protocol udp --input-role listener --input-local-port 5600 ^
  --output-protocol http --output-role server --output-local-host 127.0.0.1 --output-local-port 8080
```

```bat
rtx-vsr.exe run ^
  --input-protocol rtsp --input-role client --input-url rtsp://192.168.1.20:554/live ^
  --output-protocol srt --output-role listener --output-local-port 9000
```

Бенчмарк у вікні згорнутий. Валідний рядок — код виходу 0, `"ok":true` і хоча б один кадр після прогріву. Адреса споживача HLS у таблиці — HTTP-плейлист, не `hls://`.

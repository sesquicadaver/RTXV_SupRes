# RTX VSR Proxy

Локальний відеопроксі: **FFmpeg/NVDEC → RTX Video SDK → NVENC → транспорт**.
Вхід і вихід обираються незалежно. Програма показує лише ті протоколи й ролі, які є в поточній збірці FFmpeg.

Ефект VSR з'являється у збірці з `RTX_VIDEO_SDK_ROOT` і CUDA Toolkit 12.x. `doctor` підтверджує його лише після створення ефекту SDK на GPU NVIDIA.

Ціль — лише **Windows 11 x64**. CMake відхиляє іншу платформу незалежно від `RTX_PRODUCT_BUILD`. Linux не збирається і не є перевіркою цього продукту.

## Збірка

Штатний продуктовий компілятор — MSVC x64. Для розробки без пакета потрібні Qt 6 Widgets, FFmpeg і, для ефекту VSR, CUDA Toolkit 12.x. Без pkg-config задайте `FFMPEG_ROOT` з `include/` та `lib/`.

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\rtx-vsr-proxy.exe capabilities
build\Release\rtx-vsr-proxy.exe doctor --json
```

Якщо є Qt 6 Widgets, збирається `rtx-vsr-gui`: одне вікно з профілем, джерелом, виходом, GPU поруч із покращенням і згорнутою панеллю бенчмарку. Вікно керує `rtx-vsr-proxy` через `QProcess`; кадри лишаються в конвеєрі CLI. Перевірка без дисплея: `set QT_QPA_PLATFORM=offscreen` і `build\Release\rtx-vsr-gui.exe --self-check`.

Тести не використовують `assert`, тож Release із `NDEBUG` їх не вимикає.

MSYS2 UCRT64 у CI — лише перевірка компіляції Windows-коду без інсталятора. Це не шлях релізу. Реліз вимагає MSVC.

Окрема перевірка `RTX_CI_COMPILE_CHECK=ON` збирає `rtx_processor.cu` компілятором CUDA 12.x і MSVC+nvcc. Заголовок у `ci/compile_check/` — не NVIDIA SDK: `doctor` завершується кодом 3 і друкує «ефект RTX VSR: не підтверджено». Прапори попереджень C++ стоять лише на `COMPILE_LANGUAGE:CXX`, тож nvcc їх не бачить. Для релізного пакета цей прапорець заборонений.

## Пакет Windows 11 x64

Один інсталятор Inno Setup, не WiX. Збірка пакета:

```bat
cmake -S . -B build -DRTX_PRODUCT_BUILD=ON -DCMAKE_CUDA_ARCHITECTURES=75;86;89
cmake --build build --config Release
powershell -File packaging\stage_product.ps1 -BuildDir build -FfmpegBin C:\ffmpeg\bin -CudaBin C:\cuda\bin -SdkBin C:\RTX_Video_SDK -VcRedist C:\redist\vc_redist.x64.exe
```

`RTX_PRODUCT_BUILD` завершується помилкою без Windows x64, MSVC, Qt 6 Widgets, справжнього RTX Video SDK, `nvsdk_ngx`, CUDA і явного списку архітектур. `CMAKE_CUDA_ARCHITECTURES=OFF` теж відхиляється. `RTX_CI_COMPILE_CHECK` разом із ним заборонений. Маркер `rtx-product-build.txt` лежить у каталозі CMake (`build`), виконувані файли скрипт бере звідти або з `build\Release`. `ISCC packaging\rtx-vsr.iss` збирає `packaging\output\rtx-vsr-setup.exe`; рядок `SignTool` у скрипті закоментований, доки немає сертифіката.

Перед копіюванням файлів `rtx-vsr-precheck.exe` дивиться версію Windows, x64, місце на диску і запущені копії програми. Адаптер і версію його драйвера він читає через SetupAPI (`ProviderName` і `DriverVersion`), без CUDA і без припущення про `System32\drivers`. Збірка precheck лінкує статичний CRT (`/MT`), тож на чистій Windows вона стартує до встановлення VC++ Runtime. Runtime x64 ставиться лише якщо збірка відсутня або старіша; новіша сумісна лишається. Сумісний драйвер не чіпається, і тоді мережа не потрібна. Якщо драйвер треба поставити, достатньо `driver_url` і `driver_sha256` у маніфесті або локального пакета `-NvidiaDriver`. Невідомий або OEM-адаптер поза `supported_device_ids` дає повідомлення і не отримує приблизний драйвер. Завантажений або локальний драйвер і VC++ Runtime перевіряються підписом і SHA-256. Код 3010 повертає текст і `NeedsRestart`. Inno зупиняє копіювання і перезапускає комп'ютер. Повторний запуск setup після входу дає ключ RunOnce `RTXVSRSetupResume`; сам прапорець перезапуску інсталятор не запускає. `ResumePhase` не дає зациклити ту саму фазу. Помилка `doctor` або `install-check` після копіювання ставить код виходу 1 через `GetCustomSetupExitCode`. Якщо коротка перевірка після копіювання не проходить, повертаються файли саме цієї спроби з `{commonappdata}\RTX VSR\backup-<id>`, а встановлення завершується помилкою. Якщо копія не збіглася з ідентифікатором спроби, успіх не повідомляється. Профілі в каталозі користувача не входять у каталог програми.

`rtx-vsr-proxy install-check --clip install-check.h264` для кожного GPU за UUID декодує запис, проганяє VSR, кодує і приймає результат лише коли є кадр VSR і не було `bypass`. Кадр після резервного масштабу перевірку не проходить. Це не вимір FPV-затримки. `doctor` лишається перевіркою залежностей; наявність `h264_nvenc` у FFmpeg його не замінює.

У `packaging/components.json` перелік PCI device id (десяткові числа), `min_driver` (572.16 записується як 57216) і `vc_redist_build` заповнюються з приміток тієї версії SDK і офіційних пакетів. Порожні поля зупиняють `stage_product.ps1`. Повідомлення ліцензій — `packaging/NOTICES.txt`. FFmpeg лише динамічний LGPL, без `--enable-gpl` і `--enable-nonfree`. Кліп перевірки кодується `h264_nvenc` або передається готовим файлом, не `libx264`.

## Збірка з RTX Video SDK

Потрібні Windows 11 x64, драйвер NVIDIA, CUDA Toolkit 12.x (SDK 1.1 несумісний із CUDA 13) і розпакований [RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk).

```bat
set RTX_VIDEO_SDK_ROOT=C:\path\to\RTX_Video_SDK
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

CMake підхоплює `samples/RTX_Video_API/rtx_video_api.h` і `rtx_video_api_cuda_impl.cpp`. Поруч має лежати `nvngx_vsr.dll`.

`doctor` повертає успіх лише після створення й закриття ефекту SDK. Без зібраного SDK або без CUDA-пристрою він завершується кодом 3 і не вважає VSR робочим. `doctor --json` друкує той самий висновок структурою: `ok`, `effect`, `blockers`, `gpus` з `uuid` і `vram_bytes`. Наявність CUDA або `h264_nvenc` у FFmpeg не замінює створення ефекту на конкретній карті.

Обраний GPU перевіряється за індексом у списку виявлених пристроїв, а не за довжиною списку: зонд може пропускати окремі номери. Профіль зберігає UUID; `--gpu N` під час запуску скидає UUID і бере числовий індекс.

## Порти

| Роль | Що означає порт |
|---|---|
| `client` | порт віддаленого джерела або отримувача |
| `listener` | локальна адреса й порт прийому |
| `server` | локальна адреса й порт, де слухає вихід |
| `rendezvous` | режим SRT, якщо він є в бібліотеці |

RTSP додатково має `rtp_port_min` і `rtp_port_max` (локальний діапазон RTP/RTCP). RTP має окремий `rtcp_port`.

`rtp` без payload — це MPEG-TS over RTP. Вихід відкриває муксер `rtp_mpegts` (RTP payload type 33, далі синхробайт TS `0x47`). Вхід відкриває демуксер `rtp`. Протокол `rtp://` сам заголовки RTP не додає. `--output-payload h264|hevc|es` лишає елементарний муксер `rtp` і пише SDP у `--output-sdp`. Вхід елементарного RTP — `--input-payload h264` і `--input-path file.sdp`.

HTTP-сервер — режим `http` + `server`. `listen=1` передається словником AVOption в `avio_open2`, не рядком запиту URL. HLS-сервер — окремий режим `hls` + `server`; читання запиту має таймаут 2 с.

```bat
rtx-vsr-proxy.exe run ^
  --input-protocol udp --input-role listener --input-local-port 5600 ^
  --output-protocol http --output-role server --output-local-host 127.0.0.1 --output-local-port 8080
```

```bat
rtx-vsr-proxy.exe run ^
  --input-protocol rtsp --input-role client --input-url rtsp://192.168.1.20:554/live ^
  --output-protocol srt --output-role listener --output-local-port 9000
```

## Якість і черга

Масштаб 2 або 3. Якість 1..4, за замовчуванням `auto` зі стартом на 2. Адаптація дивиться на повний час кадру: декод, очікування в слоті, VSR, NVENC і запис. Два кадри довші за ~85% інтервалу знижують якість; вісім коротших за ~45% підвищують. Якщо перший кадр на масштабі 3 не вкладається в інтервал, до відкриття виходу масштаб падає до 2. Далі роздільність не змінюється.

Між декодером і VSR один слот: новий кадр замінює ще не оброблений. Мітки часу перераховуються з time_base входу в time_base кодера; пропуск і розрив дають один крок кадру, а не чужі одиниці.

Якщо `rtx_video_api_cuda_evaluate` падає, потік не обривається: кадр масштабується на GPU до вже відкритого розміру кодера, `vsr_state` стає `bypass`.

NVENC: без B-кадрів, `rc-lookahead=0`, `delay=0`, `zerolatency`, preset `llhp` або `ull`. Матриця кольору береться з метаданих кадру (інакше BT.709 для HD і BT.601 для SD), limited або full.

Події стану й метрики — окремі рядки JSON на stdout. Журнал FFmpeg лишається на stderr. Штатна зупинка: рядок `stop` на stdin, або SIGINT/SIGTERM. Стан: `connecting`, `waiting`, `running`, `overloaded`, `bypass`, `error`, `stopped`.

Метрики, опційно ще й `--metrics-file`:

`fps`, `frames_in`, `frames_out`, `frames_dropped`, час кадру, `vsr_quality`, `vsr_scale`, `vsr_state`, `vsr_frames`, `proxy_p95_ms`, `proxy_p99_ms`, `overload_frames`, `bypass_entries`, фактичні вхідна й вихідна роздільність. `vsr_frames` рахує кадри, які пройшли ефект VSR, а не резервний масштаб. `proxy_p95_ms` — час обробки проксі за кожним кадром після прогріву, без затримки камери, мережі, показу у споживачі й без штучної паузи `--pace`. FPS рахується за виміряними кадрами навіть коли `--metrics-interval 0`. Якщо після прогріву не лишилось жодного кадру, перцентилі не вважаються вкладенням у бюджет.

`vsr_state`: `running`, `degraded` (якість знижено), `overloaded` (якість вже мінімальна, час кадру ще великий), `bypass` (VSR вимкнено, розмір кадру збережено).

## Профіль

Спільний JSON схеми 1 для CLI (`--profile`) і вікна. Запис атомарний. Сеанс відкривається лише за адресою і портом. Ім'я користувача, пароль і параметри `passphrase`, `password`, `secret`, `token` відхиляються. Протоколи, яким для з'єднання обов'язково потрібен обліковий запис (`ftp`, `sftp`, `smb`, `icecast`), у каталог не входять. Елементарний RTP на вході зберігає `payload=es`, тож CLI відкриває SDP, а не MPEG-TS. Роль, задана для джерела з URL, відновлюється під час завантаження. Профіль тримає SDP, `app`, `playpath`, обидва кінці SRT rendezvous, RTCP і додаткові параметри. Явний прапорець `--input-*` або `--output-*` перекриває лише це поле профілю. Протокол, роль і решта збереженого кінця лишаються. URL і окремі поля адреси разом валідатор відхиляє. Відсутній UUID карти лишається невирішеним, доки користувач не обере іншу.

## Бенчмарк

Панель у вікні згорнута. Порівняння йде окремими сеансами, бо зміна масштабу потребує нового запуску. Фіксуються GPU, кодек, бітрейт і транспорт виходу. До восьми комбінацій ×2/×3 і якість 1–4, адаптація вимкнена явним `--quality`. Однаковий запис відтворюється `--pace` у темпі джерела після `--warmup`. У бюджет потрапляє лише вимір із хоча б одним кадром після прогріву. Подія `benchmark` з `"ok":true` публікується лише після сеансу без помилки; рядок стає застосовним, коли процес завершився кодом 0 і є виміряні кадри. «Застосувати вибране» переносить масштаб, якість і GPU лише з такого рядка. «Порівняти GPU» перевіряє карти послідовно. Очікування споживача має таймаут, прогін можна скасувати. Зупинка сеансу і бенчмарку йде `stop`, потім `terminate` і `kill`, і таймер діє лише для того запуску, який його поставив. Результат кожної карти пишеться окремим JSON поруч із параметрами джерела і версією FFmpeg. Адреса HLS для споживача — `http://хост:порт/index.m3u8`.

Відповідність вимог і тестів: [docs/SPEC_MATRIX.md](docs/SPEC_MATRIX.md).

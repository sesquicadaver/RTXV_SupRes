# RTX VSR Proxy

Локальний відеопроксі: **FFmpeg/NVDEC → RTX Video SDK → NVENC → транспорт**.
Вхід і вихід обираються незалежно. Програма показує лише ті протоколи й ролі, які є в поточній збірці FFmpeg.

На цій робочій машині немає GPU NVIDIA і CUDA. Ланцюг VSR зібраний у коді й компілюється, коли задано `RTX_VIDEO_SDK_ROOT` і CUDA Toolkit 12.x. Перевірка з живим SDK — на цільовому комп'ютері.

## Збірка тут

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
./build/rtx-vsr-proxy capabilities
./build/rtx-vsr-proxy doctor
```

## Збірка з RTX Video SDK

Потрібні Windows 10+, драйвер NVIDIA, CUDA Toolkit 12.x (SDK 1.1 несумісний із CUDA 13) і розпакований [RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk).

```bash
set RTX_VIDEO_SDK_ROOT=C:\path\to\RTX_Video_SDK
cmake -S . -B build
cmake --build build --config Release
```

CMake підхоплює `samples/RTX_Video_API/rtx_video_api.h` і `rtx_video_api_cuda_impl.cpp`. Поруч має лежати `nvngx_vsr.dll`.

`doctor` і старт `run` викликають SDK. Список відеокарт не фільтрується вручну: придатність визначає створення ефекту.

## Порти

| Роль | Що означає порт |
|---|---|
| `client` | порт віддаленого джерела або отримувача |
| `listener` | локальна адреса й порт прийому |
| `server` | локальна адреса й порт, де слухає вихід |
| `rendezvous` | режим SRT, якщо він є в бібліотеці |

RTSP додатково має `rtp_port_min` і `rtp_port_max` (локальний діапазон RTP/RTCP). RTP має окремий `rtcp_port`. HTTP-сервер — це режим `http` + `server` (FFmpeg `listen`), не клієнтське вивантаження. HLS-сервер — окремий режим `hls` + `server`; у нього вища затримка.

```bash
./build/rtx-vsr-proxy run \
  --input-protocol udp --input-role listener --input-local-port 5600 \
  --output-protocol http --output-role server --output-local-host 127.0.0.1 --output-local-port 8080
```

```bash
./build/rtx-vsr-proxy run \
  --input-protocol rtsp --input-role client --input-url rtsp://192.168.1.20:554/live \
  --output-protocol srt --output-role listener --output-local-port 9000
```

## Якість і черга

Масштаб 2 або 3. Якість 1..4, за замовчуванням `auto` зі стартом на 2. Якщо обробка кадру довша за ~85% інтервалу кадру, рівень знижується. Роздільність не змінюється. Між декодером і VSR один слот: новий кадр замінює ще не оброблений, лічильник `frames_dropped` росте, черга не накопичується.

NVENC: без B-кадрів, `rc-lookahead=0`, `delay=0`, `zerolatency`, preset `llhp` або `ull`.

Метрики у stderr, опційно `--metrics-file`:

`fps`, `frames_in`, `frames_out`, `frames_dropped`, `last_process_ms`, `avg_process_ms`, `vsr_quality`, `vsr_scale`, `vsr_state`.

`vsr_state`: `running`, `degraded` (якість знижено), помилка зупиняє процес.

Відповідність вимог і тестів: [docs/SPEC_MATRIX.md](docs/SPEC_MATRIX.md).

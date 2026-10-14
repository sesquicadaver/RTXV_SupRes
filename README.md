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

Тести не використовують `assert`, тож Release із `NDEBUG` їх не вимикає.

## Збірка на Windows

Штатний шлях CI — MSYS2 UCRT64: `gcc`, `cmake`, `pkgconf`, `ffmpeg`. Сокети HLS, `process_id` і `SIGPIPE` мають окремі гілки для Win32; для MSVC без pkg-config задайте `FFMPEG_ROOT` з `include/` та `lib/`.

## Збірка з RTX Video SDK

Потрібні Windows 10+, драйвер NVIDIA, CUDA Toolkit 12.x (SDK 1.1 несумісний із CUDA 13) і розпакований [RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk).

```bash
set RTX_VIDEO_SDK_ROOT=C:\path\to\RTX_Video_SDK
cmake -S . -B build
cmake --build build --config Release
```

CMake підхоплює `samples/RTX_Video_API/rtx_video_api.h` і `rtx_video_api_cuda_impl.cpp`. Поруч має лежати `nvngx_vsr.dll`.

`doctor` повертає успіх лише після створення й закриття ефекту SDK. Без зібраного SDK або без CUDA-пристрою він завершується кодом 3 і не вважає VSR робочим. Список відеокарт не фільтрується вручну.

## Порти

| Роль | Що означає порт |
|---|---|
| `client` | порт віддаленого джерела або отримувача |
| `listener` | локальна адреса й порт прийому |
| `server` | локальна адреса й порт, де слухає вихід |
| `rendezvous` | режим SRT, якщо він є в бібліотеці |

RTSP додатково має `rtp_port_min` і `rtp_port_max` (локальний діапазон RTP/RTCP). RTP має окремий `rtcp_port`.

`rtp` без payload — це MPEG-TS over RTP. `--output-payload h264|hevc|es` вмикає елементарний RTP-муксер і пише SDP у `--output-sdp`. Вхід елементарного RTP — `--input-payload h264` і `--input-path file.sdp`.

HTTP-сервер — режим `http` + `server`. `listen=1` передається словником AVOption в `avio_open2`, не рядком запиту URL. HLS-сервер — окремий режим `hls` + `server`; читання запиту має таймаут 2 с.

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

Масштаб 2 або 3. Якість 1..4, за замовчуванням `auto` зі стартом на 2. Адаптація дивиться на повний час кадру: декод, очікування в слоті, VSR, NVENC і запис. Два кадри довші за ~85% інтервалу знижують якість; вісім коротших за ~45% підвищують. Якщо перший кадр на масштабі 3 не вкладається в інтервал, до відкриття виходу масштаб падає до 2. Далі роздільність не змінюється.

Між декодером і VSR один слот: новий кадр замінює ще не оброблений. Мітки часу перераховуються з time_base входу в time_base кодера; пропуск і розрив дають один крок кадру, а не чужі одиниці.

Якщо `rtx_video_api_cuda_evaluate` падає, потік не обривається: кадр масштабується на GPU до вже відкритого розміру кодера, `vsr_state` стає `bypass`.

NVENC: без B-кадрів, `rc-lookahead=0`, `delay=0`, `zerolatency`, preset `llhp` або `ull`. Матриця кольору береться з метаданих кадру (інакше BT.709 для HD і BT.601 для SD), limited або full.

Метрики у stderr, опційно `--metrics-file`:

`fps`, `frames_in`, `frames_out`, `frames_dropped`, `last_decode_ms`, `last_wait_ms`, `last_process_ms`, `last_encode_ms`, `last_write_ms`, `last_total_ms`, `vsr_quality`, `vsr_scale`, `vsr_state`.

`vsr_state`: `running`, `degraded` (якість знижено), `overloaded` (якість вже мінімальна, час кадру ще великий), `bypass` (VSR вимкнено, розмір кадру збережено).

Відповідність вимог і тестів: [docs/SPEC_MATRIX.md](docs/SPEC_MATRIX.md).

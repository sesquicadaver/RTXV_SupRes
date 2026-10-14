# Пакує реліз Windows x64. Не підставляє SDK, драйвер і перелік GPU: порожні поля зупиняють скрипт.
param(
  [Parameter(Mandatory = $true)][string]$BuildDir,
  [Parameter(Mandatory = $true)][string]$FfmpegBin,
  [Parameter(Mandatory = $true)][string]$CudaBin,
  [Parameter(Mandatory = $true)][string]$SdkBin,
  [Parameter(Mandatory = $true)][string]$VcRedist,
  [string]$NvidiaDriver = "",
  [string]$InstallClip = "",
  [string]$Dist = ""
)

$ErrorActionPreference = "Stop"
if ($Dist -eq "") {
  $Dist = Join-Path $PSScriptRoot "dist\windows"
}

function Fail([string]$Message) {
  Write-Error $Message
  exit 1
}

function Hash-File([string]$Path) {
  return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

function Assert-Signed([string]$Path) {
  $signature = Get-AuthenticodeSignature -FilePath $Path
  if ($signature.Status -ne "Valid") {
    Fail "немає чинного підпису: $Path"
  }
}

$markerPath = Join-Path $BuildDir "rtx-product-build.txt"
if (-not (Test-Path $markerPath)) {
  Fail "немає rtx-product-build.txt. Збірка має бути RTX_PRODUCT_BUILD=ON"
}
$marker = Get-Content -Path $markerPath
if ($marker -notcontains "product=1" -or $marker -notcontains "compile_check=0") {
  Fail "маркер збірки не є релізом без RTX_CI_COMPILE_CHECK"
}
$archLine = $marker | Where-Object { $_ -like "cuda_architectures=*" } | Select-Object -First 1
$architectures = ""
if ($archLine) {
  $architectures = $archLine.Substring("cuda_architectures=".Length).Trim()
}
if ($architectures -eq "" -or $architectures -eq "native") {
  Fail "реліз не зафіксував CMAKE_CUDA_ARCHITECTURES"
}

$template = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "components.json") | ConvertFrom-Json
if ([int]$template.min_driver -le 0 -or @($template.supported_device_ids).Count -lt 1) {
  Fail "components.json має містити min_driver і supported_device_ids з приміток використаного SDK"
}
if ([int]$template.vc_redist_build -le 0) {
  Fail "components.json має містити vc_redist_build офіційного пакета"
}

$offers = Join-Path $PSScriptRoot "third-party"
foreach ($name in @("QT_SOURCE_OFFER.txt", "FFMPEG_SOURCE_OFFER.txt", "NVIDIA_RUNTIME.txt")) {
  $offer = Get-Content -Raw -Encoding UTF8 (Join-Path $offers $name)
  if ($offer -match "PLACEHOLDER") {
    Fail "$name ще містить PLACEHOLDER"
  }
}
$ffmpegNote = Get-Content -Raw -Encoding UTF8 (Join-Path $offers "FFMPEG_SOURCE_OFFER.txt")
if ($ffmpegNote -match "--enable-gpl" -or $ffmpegNote -match "--enable-nonfree") {
  Fail "FFmpeg для пакета має бути динамічною збіркою LGPL без --enable-gpl і --enable-nonfree"
}
$nvidiaNote = Get-Content -Raw -Encoding UTF8 (Join-Path $offers "NVIDIA_RUNTIME.txt")
if ($nvidiaNote -notmatch "SDK_VERSION=\S+") {
  Fail "NVIDIA_RUNTIME.txt має містити SDK_VERSION використаного SDK"
}

New-Item -ItemType Directory -Force -Path $Dist | Out-Null
foreach ($exe in @("rtx-vsr-proxy.exe", "rtx-vsr-gui.exe", "rtx-vsr-precheck.exe")) {
  $source = Join-Path $BuildDir $exe
  if (-not (Test-Path $source)) {
    $source = Join-Path $BuildDir "Release\$exe"
  }
  if (-not (Test-Path $source)) {
    Fail "немає $exe у $BuildDir"
  }
  Copy-Item $source (Join-Path $Dist $exe) -Force
}

$deploy = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $deploy) {
  Fail "потрібен windeployqt тієї самої збірки Qt"
}
& windeployqt --release --no-translations (Join-Path $Dist "rtx-vsr-gui.exe")
if (-not (Test-Path (Join-Path $Dist "platforms\qwindows.dll"))) {
  Fail "windeployqt не поклав platforms\qwindows.dll"
}

Get-ChildItem -Path $FfmpegBin -Filter "*.dll" | Copy-Item -Destination $Dist -Force
foreach ($stem in @("avcodec", "avformat", "avutil")) {
  if (-not (Get-ChildItem $Dist -Filter "$stem*.dll")) {
    Fail "немає $stem DLL з перевіреної збірки FFmpeg"
  }
}
$cudart = Get-ChildItem $CudaBin -Filter "cudart64_*.dll" -Recurse | Select-Object -First 1
if (-not $cudart) {
  Fail "немає redistributable cudart64_*.dll. Toolkit на ПК користувача не потрібен і в пакет не входить"
}
Copy-Item $cudart.FullName (Join-Path $Dist $cudart.Name) -Force
$ngx = Get-ChildItem $SdkBin -Filter "nvngx_vsr.dll" -Recurse | Select-Object -First 1
if (-not $ngx) {
  Fail "немає nvngx_vsr.dll використаного SDK"
}
Copy-Item $ngx.FullName (Join-Path $Dist "nvngx_vsr.dll") -Force

Assert-Signed $VcRedist
Copy-Item $VcRedist (Join-Path $Dist "vc_redist.x64.exe") -Force
$vcHash = Hash-File (Join-Path $Dist "vc_redist.x64.exe")

$driverFile = ""
$driverHash = ""
if ($NvidiaDriver -ne "") {
  Assert-Signed $NvidiaDriver
  Copy-Item $NvidiaDriver (Join-Path $Dist "nvidia-driver.exe") -Force
  $driverFile = "nvidia-driver.exe"
  $driverHash = Hash-File (Join-Path $Dist "nvidia-driver.exe")
}

$clip = Join-Path $Dist "install-check.h264"
if ($InstallClip -ne "") {
  Copy-Item $InstallClip $clip -Force
} else {
  $ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
  if (-not $ffmpeg) {
    Fail "передайте -InstallClip або ffmpeg з h264_nvenc. libx264 для цього кліпу не використовується"
  }
  & ffmpeg -y -hide_banner -loglevel error -f lavfi -i "color=c=black:s=640x360:r=25:d=0.2" -frames:v 5 -c:v h264_nvenc -pix_fmt yuv420p $clip
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $clip)) {
    Fail "не вдалося зібрати install-check.h264 через h264_nvenc. Передайте -InstallClip"
  }
}

Copy-Item (Join-Path $PSScriptRoot "NOTICES.txt") $Dist -Force
New-Item -ItemType Directory -Force -Path (Join-Path $Dist "third-party") | Out-Null
Copy-Item (Join-Path $offers "*") (Join-Path $Dist "third-party") -Force

$deviceText = (@($template.supported_device_ids) | ForEach-Object { [int]$_ }) -join ","
$lines = Get-ChildItem $Dist -Recurse -File | ForEach-Object {
  $relative = $_.FullName.Substring($Dist.Length).TrimStart("\", "/")
  if ($relative -eq "components.json") { return }
  '    {"name":"' + ($relative -replace "\\", "/") + '","sha256":"' + (Hash-File $_.FullName) + '"}'
}
$components = $lines -join ",`n"
@"
{
  "windows_build_min": $([int]$template.windows_build_min),
  "disk_bytes": $([int64]$template.disk_bytes),
  "vc_redist_build": $([int]$template.vc_redist_build),
  "vc_file": "vc_redist.x64.exe",
  "vc_sha256": "$vcHash",
  "min_driver": $([int]$template.min_driver),
  "supported_device_ids": [$deviceText],
  "driver_file": "$driverFile",
  "driver_url": "$($template.driver_url)",
  "driver_sha256": "$driverHash",
  "cuda_architectures": "$architectures",
  "components": [
$components
  ]
}
"@ | Set-Content -Encoding utf8 (Join-Path $Dist "components.json")

Write-Host "Комплект зібрано: $Dist"

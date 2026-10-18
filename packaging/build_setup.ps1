# Збирає packaging\output\rtx-vsr-setup.exe з уже зібраного Release.
# Драйвер NVIDIA в пакет не входить.
param(
  [string]$BuildDir = "C:\RTXV_SupRes\build",
  [string]$FfmpegBin = "$env:LOCALAPPDATA\rtx-vsr-build\ffmpeg\ffmpeg-n7.1.5-12-g1fdbca85aa-win64-lgpl-shared-7.1\bin",
  [string]$CudaBin = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin",
  [string]$SdkBin = "C:\RTX",
  [string]$QtBin = "$env:LOCALAPPDATA\rtx-vsr-build\Qt\6.8.3\msvc2022_64\bin"
)

$ErrorActionPreference = "Stop"
$env:Path = "$QtBin;$FfmpegBin;$CudaBin;" + $env:Path

& (Join-Path $PSScriptRoot "stage_product.ps1") `
  -BuildDir $BuildDir `
  -FfmpegBin $FfmpegBin `
  -CudaBin $CudaBin `
  -SdkBin $SdkBin

$iscc = @(
  (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
  (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 7\ISCC.exe"),
  (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
  (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe")
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) {
  $command = Get-Command ISCC -ErrorAction SilentlyContinue
  if ($command) { $iscc = $command.Source }
}
if (-not $iscc) {
  Write-Error "не знайдено ISCC.exe"
  exit 1
}

$dist = Join-Path $PSScriptRoot "dist\windows"
& $iscc "/DDist=$dist" (Join-Path $PSScriptRoot "rtx-vsr.iss")
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
Write-Host "Інсталятор: $(Join-Path $PSScriptRoot 'output\rtx-vsr-setup.exe')"

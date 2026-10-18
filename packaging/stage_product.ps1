# Пакує реліз Windows x64. Не підставляє SDK, драйвер і перелік GPU: порожні поля зупиняють скрипт.
param(
  [Parameter(Mandatory = $true)][string]$BuildDir,
  [Parameter(Mandatory = $true)][string]$FfmpegBin,
  [Parameter(Mandatory = $true)][string]$CudaBin,
  [Parameter(Mandatory = $true)]  [string]$SdkBin,
  [string]$QtPrefix = "",
  [string]$VcRedist = "",
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
  $markerPath = Join-Path (Split-Path -Parent $BuildDir) "rtx-product-build.txt"
}
if (-not (Test-Path $markerPath)) {
  Fail "немає rtx-product-build.txt. Збірка має бути RTX_PRODUCT_BUILD=ON, -BuildDir — каталог CMake"
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
if ($architectures -eq "" -or $architectures -eq "native" -or $architectures -eq "OFF") {
  Fail "реліз не зафіксував явний список CMAKE_CUDA_ARCHITECTURES"
}

function Find-VcRedist {
  $roots = @(
    (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Community\VC\Redist\MSVC"),
    (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"),
    (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC")
  )
  $found = @()
  foreach ($root in $roots) {
    if (Test-Path $root) {
      $found += Get-ChildItem $root -Recurse -Filter "vc_redist.x64.exe" -ErrorAction SilentlyContinue
    }
  }
  return $found | Sort-Object FullName -Descending | Select-Object -First 1
}

function Vc-BuildNumber([string]$Path) {
  $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
  $parts = @($info.FileVersion -split '\.')
  if ($parts.Count -ge 3) {
    return [int]$parts[2]
  }
  Fail "не вдалося прочитати номер збірки VC++ Runtime: $Path"
}

$template = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "components.json") | ConvertFrom-Json
if ($VcRedist -eq "") {
  $redistItem = Find-VcRedist
  if (-not $redistItem) {
    Fail "не знайдено vc_redist.x64.exe у Visual Studio 2022"
  }
  $VcRedist = $redistItem.FullName
}
$vcBuild = Vc-BuildNumber $VcRedist

if (Test-Path -LiteralPath $Dist) {
  Remove-Item -LiteralPath $Dist -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
foreach ($exe in @("rtx-vsr.exe", "rtx-vsr-precheck.exe")) {
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
& windeployqt --release --no-translations (Join-Path $Dist "rtx-vsr.exe")
if ($LASTEXITCODE -ne 0) {
  Fail "windeployqt завершився з кодом $LASTEXITCODE"
}
if ($QtPrefix -eq "") {
  $QtPrefix = Split-Path -Parent (Split-Path -Parent $deploy.Source)
}
$qwindows = Join-Path $QtPrefix "plugins\platforms\qwindows.dll"
if (-not (Test-Path -LiteralPath $qwindows)) {
  Fail "немає $qwindows. platforms\qwindows.dll має лежати в пакеті"
}
$platforms = Join-Path $Dist "platforms"
New-Item -ItemType Directory -Force -Path $platforms | Out-Null
Copy-Item -LiteralPath $qwindows (Join-Path $platforms "qwindows.dll") -Force

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
$ngx = Get-ChildItem $SdkBin -Filter "nvngx_vsr.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $ngx) {
  Fail "немає nvngx_vsr.dll у $SdkBin"
}
Copy-Item $ngx.FullName (Join-Path $Dist "nvngx_vsr.dll") -Force

Assert-Signed $VcRedist
Copy-Item $VcRedist (Join-Path $Dist "vc_redist.x64.exe") -Force
$vcHash = Hash-File (Join-Path $Dist "vc_redist.x64.exe")

$driverFile = ""
$driverUrl = ([string]$template.driver_url).Trim()
$driverHash = ([string]$template.driver_sha256).Trim().ToLowerInvariant()
if ($NvidiaDriver -ne "") {
  Assert-Signed $NvidiaDriver
  Copy-Item $NvidiaDriver (Join-Path $Dist "nvidia-driver.exe") -Force
  $driverFile = "nvidia-driver.exe"
  $driverHash = Hash-File (Join-Path $Dist "nvidia-driver.exe")
} elseif ($driverUrl -ne "" -or $driverHash -ne "") {
  if ($driverUrl -eq "" -or $driverHash -eq "") {
    Fail "онлайн-драйвер потребує driver_url і driver_sha256 разом"
  }
  if ($driverUrl -notmatch '^https://') {
    Fail "driver_url має бути https"
  }
} else {
  $driverHash = ""
}

$clip = Join-Path $Dist "install-check.h264"
if ($InstallClip -eq "") {
  $InstallClip = Join-Path $PSScriptRoot "install-check.h264"
}
if (-not (Test-Path -LiteralPath $InstallClip)) {
  Fail "немає вхідного кліпу $InstallClip"
}
Copy-Item -LiteralPath $InstallClip $clip -Force
if (-not (Test-Path -LiteralPath $clip) -or (Get-Item -LiteralPath $clip).Length -le 0) {
  Fail "install-check.h264 порожній"
}
foreach ($name in @("Qt6Widgets.dll", "platforms\qwindows.dll", "avformat-61.dll", "avcodec-61.dll", "avutil-59.dll", "nvngx_vsr.dll")) {
  if (-not (Test-Path -LiteralPath (Join-Path $Dist $name))) {
    Fail "у комплекті немає $name"
  }
}
if (-not (Get-ChildItem $Dist -Filter "cudart64_*.dll")) {
  Fail "у комплекті немає cudart64_*.dll"
}
# Той самий комплект, що й у інсталяторі: інакше подвійний клік по build\Release\rtx-vsr.exe
# показує відсутні Qt6Widgets.dll і avformat-61.dll.
$stagedExe = Join-Path $BuildDir "rtx-vsr.exe"
if (-not (Test-Path -LiteralPath $stagedExe)) {
  $stagedExe = Join-Path $BuildDir "Release\rtx-vsr.exe"
}
$exeDir = Split-Path -Parent $stagedExe
Get-ChildItem -LiteralPath $Dist -Recurse -File | ForEach-Object {
  if ($_.Name -in @("vc_redist.x64.exe", "nvidia-driver.exe", "components.json", "rtx-product-build.txt")) {
    return
  }
  $relative = $_.FullName.Substring($Dist.Length).TrimStart("\", "/")
  $target = Join-Path $exeDir $relative
  $parent = Split-Path -Parent $target
  if (-not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
  }
  Copy-Item -LiteralPath $_.FullName -Destination $target -Force
}

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
  "vc_redist_build": $vcBuild,
  "vc_file": "vc_redist.x64.exe",
  "vc_sha256": "$vcHash",
  "min_driver": $([int]$template.min_driver),
  "supported_device_ids": [$deviceText],
  "driver_file": "$driverFile",
  "driver_url": "$driverUrl",
  "driver_sha256": "$driverHash",
  "cuda_architectures": "$architectures",
  "components": [
$components
  ]
}
"@ | Set-Content -Encoding utf8 (Join-Path $Dist "components.json")

Write-Host "Комплект зібрано: $Dist"

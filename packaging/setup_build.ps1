# Готує машину збірки Windows 11 x64. Ставить лише те, чого ще немає.
# Не встановлює PowerShell, winget, Git, драйвер NVIDIA, CUDA 13 і RTX Video SDK.
param(
  [switch]$Plan,
  [string]$FfmpegRoot = "",
  [string]$QtPrefix = "",
  [string]$SdkRoot = ""
)

$ErrorActionPreference = "Stop"

function Stop-Setup([int]$Code, [string]$Message) {
  [Console]::Error.WriteLine($Message)
  exit $Code
}

function Note([string]$Message) {
  Write-Host $Message
}

function Test-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = New-Object Security.Principal.WindowsPrincipal($identity)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Require-Administrator([string]$Reason) {
  if (-not (Test-Administrator)) {
    Stop-Setup 1 "$Reason Запустіть цей самий скрипт у вже встановленому PowerShell від адміністратора."
  }
}

function Refresh-Path {
  $machine = [Environment]::GetEnvironmentVariable("Path", "Machine")
  $user = [Environment]::GetEnvironmentVariable("Path", "User")
  $env:Path = "$machine;$user"
}

function Find-CommandPath([string]$Name, [string]$Fallback) {
  $command = Get-Command $Name -ErrorAction SilentlyContinue
  if ($command -and $command.Source) {
    return $command.Source
  }
  if ($Fallback -and (Test-Path $Fallback)) {
    return $Fallback
  }
  return $null
}

function Install-WingetPackage([string]$Id, [string]$Override) {
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Stop-Setup 1 "немає winget. На Windows 11 він входить до App Installer; цей скрипт його не встановлює."
  }
  $arguments = @(
    "install", "-e", "--id", $Id,
    "--accept-package-agreements", "--accept-source-agreements", "--disable-interactivity"
  )
  if ($Override) {
    $arguments += @("--override", $Override)
  }
  & winget @arguments
  Refresh-Path
}

function Assert-Windows11x64 {
  if (-not [Environment]::Is64BitOperatingSystem) {
    Stop-Setup 1 "потрібна 64-бітна Windows."
  }
  $current = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
  $build = 0
  if ($current.CurrentBuild) {
    $build = [int]$current.CurrentBuild
  }
  if ($build -lt 22000) {
    Stop-Setup 1 "потрібна Windows 11 (збірка 22000 або новіша)."
  }
}

function Get-Vs2022Path {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) {
    return $null
  }
  $path = & $vswhere -products * -version "[17.0,18.0)" -property installationPath
  if (-not $path) {
    return $null
  }
  return ($path | Select-Object -First 1).ToString().Trim()
}

function Test-VcTools([string]$InstallPath) {
  if (-not $InstallPath) {
    return $false
  }
  $msvc = Join-Path $InstallPath "VC\Tools\MSVC"
  if (-not (Test-Path $msvc)) {
    return $false
  }
  $tool = Get-ChildItem $msvc -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
  return $null -ne $tool
}

function Find-Cuda12Root {
  $base = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA"
  if (-not (Test-Path $base)) {
    return $null
  }
  $dirs = Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "v12.*" } |
    Sort-Object Name -Descending
  foreach ($dir in $dirs) {
    $nvcc = Join-Path $dir.FullName "bin\nvcc.exe"
    if (Test-Path $nvcc) {
      return $dir.FullName
    }
  }
  return $null
}

function Test-FfmpegTree([string]$Root) {
  if (-not $Root -or -not (Test-Path $Root)) {
    return $false
  }
  $header = Join-Path $Root "include\libavformat\avformat.h"
  $libDir = Join-Path $Root "lib"
  if (-not (Test-Path $header) -or -not (Test-Path $libDir)) {
    return $false
  }
  $lib = Get-ChildItem $libDir -Filter "avformat*" -ErrorAction SilentlyContinue | Select-Object -First 1
  return $null -ne $lib
}

function Find-QtPrefix([string]$Preferred) {
  $roots = @()
  if ($Preferred) {
    $roots += $Preferred
  }
  if ($env:CMAKE_PREFIX_PATH) {
    $roots += $env:CMAKE_PREFIX_PATH.Split(";")
  }
  $roots += (Join-Path $env:LOCALAPPDATA "rtx-vsr-build\Qt")
  $roots += "C:\Qt"
  $roots += (Join-Path $env:ProgramFiles "Qt")
  foreach ($root in $roots) {
    if (-not $root -or -not (Test-Path $root)) {
      continue
    }
    $hit = Get-ChildItem -Path $root -Filter "Qt6WidgetsConfig.cmake" -Recurse -ErrorAction SilentlyContinue |
      Select-Object -First 1
    if ($hit) {
      return $hit.Directory.Parent.Parent.Parent.FullName
    }
  }
  return $null
}

function Find-SdkHeader([string]$Root) {
  if (-not $Root -or -not (Test-Path $Root)) {
    return $null
  }
  $relative = @(
    "samples\RTX_Video_API\rtx_video_api.h",
    "samples\rtx_video_api\rtx_video_api.h",
    "include\rtx_video_api.h"
  )
  foreach ($rel in $relative) {
    $full = Join-Path $Root $rel
    if (Test-Path $full) {
      return $full
    }
  }
  return $null
}

function Find-Iscc {
  $command = Find-CommandPath "ISCC" $null
  if ($command) {
    return $command
  }
  $candidates = @(
    (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:ProgramFiles "Inno Setup 7\ISCC.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 7\ISCC.exe"),
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 7\ISCC.exe")
  )
  foreach ($candidate in $candidates) {
    if (Test-Path $candidate) {
      return $candidate
    }
  }
  return $null
}

function Test-RealExecutable([string]$Path) {
  if (-not $Path) {
    return $false
  }
  return $Path -notlike "*\WindowsApps\*"
}

function Get-PythonArgs {
  $py = Get-Command py -ErrorAction SilentlyContinue
  if ($py -and (Test-RealExecutable $py.Source)) {
    return @("py", "-3")
  }
  $python = Get-Command python -ErrorAction SilentlyContinue
  if ($python -and (Test-RealExecutable $python.Source)) {
    return @("python")
  }
  return $null
}

function Invoke-Python([string[]]$Arguments) {
  $launcher = Get-PythonArgs
  if (-not $launcher) {
    Stop-Setup 1 "немає Python 3."
  }
  $executable = $launcher[0]
  $prefix = @()
  if ($launcher.Length -gt 1) {
    $prefix = $launcher[1..($launcher.Length - 1)]
  }
  & $executable @prefix @Arguments
  return $LASTEXITCODE
}

function Ensure-Msvc {
  $installed = Get-Vs2022Path
  if (Test-VcTools $installed) {
    Note "вже є: MSVC 2022 ($installed)"
    return
  }
  if ($Plan) {
    Note "немає: MSVC 2022 з інструментами C++"
    return
  }
  Require-Administrator "немає MSVC 2022 з інструментами C++."
  $override = "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  if ($installed) {
    Note "додаю інструменти C++ до вже встановленого Visual Studio 2022"
    $setup = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\setup.exe"
    if (-not (Test-Path $setup)) {
      Stop-Setup 1 "немає Visual Studio Installer."
    }
    $proc = Start-Process -FilePath $setup -ArgumentList "modify --installPath `"$installed`" --quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" -Wait -PassThru
    if ($proc.ExitCode -ne 0 -and $proc.ExitCode -ne 3010) {
      Stop-Setup 1 "Visual Studio Installer завершився з кодом $($proc.ExitCode)."
    }
    if ($proc.ExitCode -eq 3010) {
      Stop-Setup 3010 "інструменти C++ додано. Після перезапуску Windows запустіть скрипт ще раз."
    }
  } else {
    Note "ставлю Visual Studio 2022 Build Tools"
    Install-WingetPackage "Microsoft.VisualStudio.2022.BuildTools" $override
  }
  if (-not (Test-VcTools (Get-Vs2022Path))) {
    Stop-Setup 1 "MSVC 2022 після встановлення не знайдено. Відкрийте нове вікно PowerShell і запустіть скрипт ще раз."
  }
}

function Ensure-Cmake {
  $cmake = Find-CommandPath "cmake" (Join-Path $env:ProgramFiles "CMake\bin\cmake.exe")
  if ($cmake) {
    Note "вже є: CMake ($cmake)"
    return $cmake
  }
  if ($Plan) {
    Note "немає: CMake"
    return $null
  }
  Note "ставлю CMake"
  Install-WingetPackage "Kitware.CMake" $null
  $cmake = Find-CommandPath "cmake" (Join-Path $env:ProgramFiles "CMake\bin\cmake.exe")
  if (-not $cmake) {
    Stop-Setup 1 "CMake після встановлення не знайдено. Відкрийте нове вікно PowerShell і запустіть скрипт ще раз."
  }
  return $cmake
}

function Ensure-Cuda12 {
  $root = Find-Cuda12Root
  if ($root) {
    Note "вже є: CUDA Toolkit 12.x ($root)"
    return $root
  }
  if ($Plan) {
    Note "немає: CUDA Toolkit 12.6 (без драйвера дисплея)"
    return $null
  }
  Require-Administrator "немає CUDA Toolkit 12.x."
  $cache = Join-Path $env:LOCALAPPDATA "rtx-vsr-build\cache"
  New-Item -ItemType Directory -Force -Path $cache | Out-Null
  $exe = Join-Path $cache "cuda_12.6.3_windows_network.exe"
  $url = "https://developer.download.nvidia.com/compute/cuda/12.6.3/network_installers/cuda_12.6.3_windows_network.exe"
  if (-not (Test-Path $exe) -or (Get-Item $exe).Length -lt 1000000) {
    Note "завантажую CUDA Toolkit 12.6.3"
    & curl.exe -L --fail --retry 3 -o $exe $url
    if ($LASTEXITCODE -ne 0) {
      Stop-Setup 1 "не вдалося завантажити CUDA Toolkit 12.6.3."
    }
  }
  # Лише компілятор і runtime. Display.Driver сюди не входить: наявний драйвер не замінюється.
  $packages = @("nvcc_12.6", "cudart_12.6", "nvrtc_12.6", "nvrtc_dev_12.6", "visual_studio_integration_12.6", "thrust_12.6")
  Note "ставлю CUDA Toolkit 12.6 без драйвера дисплея"
  $argumentLine = "-s -n " + ($packages -join " ")
  $proc = Start-Process -FilePath $exe -ArgumentList $argumentLine -Wait -PassThru
  if ($proc.ExitCode -eq 3010) {
    Stop-Setup 3010 "CUDA Toolkit 12.6 поставлено. Драйвер дисплея не змінювався. Після перезапуску Windows запустіть скрипт ще раз."
  }
  if ($proc.ExitCode -ne 0) {
    Stop-Setup 1 "інсталятор CUDA завершився з кодом $($proc.ExitCode)."
  }
  $root = Find-Cuda12Root
  if (-not $root) {
    Stop-Setup 1 "CUDA Toolkit 12.6 після встановлення не знайдено."
  }
  return $root
}

function Ensure-Ffmpeg {
  if (Test-FfmpegTree $FfmpegRoot) {
    Note "вже є: FFmpeg ($FfmpegRoot)"
    return $FfmpegRoot
  }
  if (Test-FfmpegTree $env:FFMPEG_ROOT) {
    Note "вже є: FFmpeg ($env:FFMPEG_ROOT)"
    return $env:FFMPEG_ROOT
  }
  $dest = Join-Path $env:LOCALAPPDATA "rtx-vsr-build\ffmpeg"
  if (Test-Path $dest) {
    $existing = Get-ChildItem $dest -Recurse -Filter "avformat.h" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($existing) {
      $root = $existing.Directory.Parent.Parent.FullName
      if (Test-FfmpegTree $root) {
        Note "вже є: FFmpeg ($root)"
        return $root
      }
    }
  }
  if ($Plan) {
    Note "немає: FFmpeg LGPL (include і lib)"
    return $null
  }
  Note "завантажую FFmpeg LGPL"
  $releaseJson = (& curl.exe -fsSL -H "Accept: application/vnd.github+json" "https://api.github.com/repos/BtbN/FFmpeg-Builds/releases?per_page=30") -join "`n"
  if ($LASTEXITCODE -ne 0 -or -not $releaseJson) {
    Stop-Setup 1 "не вдалося знайти збірку FFmpeg LGPL."
  }
  $url = $null
  foreach ($release in ($releaseJson | ConvertFrom-Json)) {
    foreach ($asset in $release.assets) {
      if ($asset.name -like "*-win64-lgpl-shared-7.1.zip") {
        $url = $asset.browser_download_url
        break
      }
    }
    if ($url) { break }
  }
  if (-not $url) {
    Stop-Setup 1 "немає архіву win64-lgpl-shared-7.1."
  }
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  $zip = Join-Path $dest "ffmpeg-lgpl.zip"
  & curl.exe -L --fail --retry 3 -o $zip $url
  if ($LASTEXITCODE -ne 0) {
    Stop-Setup 1 "не вдалося завантажити FFmpeg."
  }
  Expand-Archive -Path $zip -DestinationPath $dest -Force
  Remove-Item $zip -Force
  $header = Get-ChildItem $dest -Recurse -Filter "avformat.h" -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $header) {
    Stop-Setup 1 "у архіві FFmpeg немає заголовків."
  }
  $root = $header.Directory.Parent.Parent.FullName
  if (-not (Test-FfmpegTree $root)) {
    Stop-Setup 1 "у архіві FFmpeg немає бібліотек імпорту."
  }
  return $root
}

function Ensure-Qt {
  $prefix = Find-QtPrefix $QtPrefix
  $deploy = $null
  if ($prefix) {
    $deploy = Join-Path $prefix "bin\windeployqt.exe"
  }
  if ($prefix -and (Test-Path $deploy)) {
    Note "вже є: Qt 6 ($prefix)"
    return $prefix
  }
  if ($Plan) {
    if ($prefix) {
      Note "вже є: Qt 6 ($prefix); немає windeployqt"
    } else {
      Note "немає: Qt 6 Widgets для MSVC 2022"
    }
    return $prefix
  }
  if (-not (Get-PythonArgs)) {
    Note "ставлю Python 3 для aqtinstall"
    Install-WingetPackage "Python.Python.3.12" $null
    if (-not (Get-PythonArgs)) {
      Stop-Setup 1 "Python 3 після встановлення не знайдено."
    }
  } else {
    Note "вже є: Python"
  }
  if ((Invoke-Python @("-c", "import aqt")) -ne 0) {
    Note "ставлю aqtinstall"
    if ((Invoke-Python @("-m", "pip", "install", "--user", "--disable-pip-version-check", "aqtinstall")) -ne 0) {
      Stop-Setup 1 "aqtinstall не встановився."
    }
  }
  $dest = Join-Path $env:LOCALAPPDATA "rtx-vsr-build\Qt"
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  Note "ставлю Qt 6.8 для MSVC 2022"
  if ((Invoke-Python @("-m", "aqt", "install-qt", "windows", "desktop", "6.8.3", "win64_msvc2022_64", "--outputdir", $dest, "--archives", "qtbase", "qttools")) -ne 0) {
    Stop-Setup 1 "Qt 6 не встановився."
  }
  $prefix = Find-QtPrefix $dest
  if (-not $prefix -or -not (Test-Path (Join-Path $prefix "bin\windeployqt.exe"))) {
    Stop-Setup 1 "Qt 6 після встановлення не знайдено."
  }
  return $prefix
}

function Ensure-Inno {
  $iscc = Find-Iscc
  if ($iscc) {
    Note "вже є: Inno Setup ($iscc)"
    return $iscc
  }
  if ($Plan) {
    Note "немає: Inno Setup"
    return $null
  }
  Note "ставлю Inno Setup"
  Install-WingetPackage "JRSoftware.InnoSetup" $null
  $iscc = Find-Iscc
  if (-not $iscc) {
    Stop-Setup 1 "Inno Setup після встановлення не знайдено."
  }
  return $iscc
}

function Write-BuildEnv([string]$Ffmpeg, [string]$Cuda, [string]$Qt, [string]$Cmake, [string]$Iscc) {
  $dir = Join-Path $env:LOCALAPPDATA "rtx-vsr-build"
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  $path = Join-Path $dir "build-env.ps1"
  $qtBin = ""
  $cudaBin = ""
  $cmakeBin = ""
  $isccDir = ""
  if ($Qt) { $qtBin = Join-Path $Qt "bin" }
  if ($Cuda) { $cudaBin = Join-Path $Cuda "bin" }
  if ($Cmake) { $cmakeBin = Split-Path -Parent $Cmake }
  if ($Iscc) { $isccDir = Split-Path -Parent $Iscc }
  $prefix = $Qt
  @(
    "# Згенеровано packaging/setup_build.ps1. Підключення в цьому вікні PowerShell:",
    "# . `"$path`"",
    "`$env:FFMPEG_ROOT = '$Ffmpeg'",
    "`$env:CUDA_PATH = '$Cuda'",
    "`$env:CUDA_PATH_V12_6 = '$Cuda'",
    "`$env:CudaToolkitDir = '$Cuda'",
    "`$env:CMAKE_PREFIX_PATH = '$prefix'",
    "`$env:Path = '$qtBin;$cudaBin;$cmakeBin;$isccDir;' + `$env:Path"
  ) | Set-Content -Path $path -Encoding UTF8
  Note "середовище збірки: $path"
  return $path
}

Assert-Windows11x64
Ensure-Msvc
$cmake = Ensure-Cmake
$cuda = Ensure-Cuda12
$ffmpeg = Ensure-Ffmpeg
$qt = Ensure-Qt
$iscc = Ensure-Inno
if (-not $Plan) {
  Write-BuildEnv $ffmpeg $cuda $qt $cmake $iscc
}

$sdk = $env:RTX_VIDEO_SDK_ROOT
if ($SdkRoot) {
  $sdk = $SdkRoot
}
$header = Find-SdkHeader $sdk
if ($header) {
  Note "вже є: RTX Video SDK ($header)"
  exit 0
}
if ($Plan) {
  Note "вручну: RTX Video SDK з https://developer.nvidia.com/rtx-video-sdk і змінна RTX_VIDEO_SDK_ROOT"
  exit 0
}
Stop-Setup 2 "RTX Video SDK цей скрипт не завантажує. Розпакуйте його з https://developer.nvidia.com/rtx-video-sdk, задайте RTX_VIDEO_SDK_ROOT і запустіть скрипт ще раз."

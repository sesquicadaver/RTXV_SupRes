# Пише нижній SHA-256 у -OutFile. Шляхи лише з параметрів.
param(
  [Parameter(Mandatory = $true)][string]$Path,
  [Parameter(Mandatory = $true)][string]$OutFile
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Path)) {
  exit 1
}
$hash = (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
[IO.File]::WriteAllBytes($OutFile, [Text.Encoding]::ASCII.GetBytes($hash))
exit 0

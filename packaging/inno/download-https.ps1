# Завантажує лише https:// без query-ін'єкцій у -Command.
param(
  [Parameter(Mandatory = $true)][string]$Uri,
  [Parameter(Mandatory = $true)][string]$OutFile
)
$ErrorActionPreference = "Stop"
if ($Uri -notmatch '^https://[A-Za-z0-9._~:/?#\[\]@!$&''()*+,;=%-]+$') {
  Write-Error "дозволено лише https URL"
  exit 1
}
if ($Uri -match '[`\$;|]') {
  Write-Error "недопустимі символи в URL"
  exit 1
}
Invoke-WebRequest -Uri $Uri -OutFile $OutFile
exit 0
